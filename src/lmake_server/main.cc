// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh" // /!\ must be first to include Python.h first

#include <pwd.h>

#include "process.hh"
#include "rpc_client.hh"
#include "rpc_job_exec.hh"
#include "autodep/record.hh"
#include "cmd.hh"
#include "makefiles.hh"

using namespace Disk   ;
using namespace Engine ;
using namespace Time   ;

static constexpr Delay StatsRefresh { 1 } ;

struct LmakeServer : AutoServer<LmakeServer> {
	using Item = ReqRpcReq ;
	static constexpr uint64_t Magic = LmakeServerMagic ;             // any random improbable value!=0 used as a sanity check when client connect to server
	// cxtors & casts
	using AutoServer<LmakeServer>::AutoServer ;
	// injection
	bool/*done*/ interrupt() {
		Trace trace("interrupt") ;
		if (stop.stop_requested()) {
			trace("stop_requested") ;
			return true/*done*/ ;
		}
		{	Lock lock { Req::s_reqs_mutex } ;
			for( Req r : Req::s_reqs_by_start() ) {
				trace("all_zombie",r) ;
				r.zombie(true) ;
			}
		}
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		g_engine_queue.emplace_urgent(GlobalProc::Int) ;             // this will close ofd when done writing to it, urgent to ensure good reactivity
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		return false/*done*/ ;
	}
	Bool3/*done*/ process_item( Fd fd , ReqRpcReq const& rrr ) {     // Maybe means there may be further output to fd, close_slave_out will be called
		Trace trace("process_item",fd,rrr) ;
		switch (rrr.proc) {
			case ReqProc::Kill :
			case ReqProc::None : {
				Req r = slaves.at(fd) ;
				trace("eof",fd,r) ;
				if (+r) { trace("zombie",r) ; r.zombie(true) ; }     // make req zombie immediately to optimize reaction time
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				g_engine_queue.emplace_urgent( rrr.proc , r , fd ) ; // this will close ofd when done writing to it, urgent to ensure good reactivity
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				return Maybe/*done*/ ;
			}
			case ReqProc::Collect :                                  // PER_CMD : handle request coming from receiving thread, just add your Proc here if the request is answered immediately
			case ReqProc::Debug   :
			case ReqProc::Forget  :
			case ReqProc::Mark    :
				SWEAR(writable) ;
			[[fallthrough]] ;
			case ReqProc::Show :
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				g_engine_queue.emplace_urgent( rrr.proc , fd , rrr.files , rrr.options ) ;                                          // urgent to ensure in order Kill/None
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				return Maybe/*done*/ ;
			case ReqProc::Make : {
				SWEAR(writable) ;
				Req& r = slaves.at(fd) ;
				try {
					r = New ;
				} catch (::string const& e) {
					audit( fd , rrr.options , Color::None , e , true/*as_is*/ ) ;
					try                       { OMsgBuf( ReqRpcReply(ReqRpcReplyProc::Status,Rc::Fail) ).send( fd , {}/*key*/ ) ; }
					catch (::string const& e) { trace("lost_client",e) ;                                                          } // we cant do much if we cant communicate
					return Yes/*done_input*/ ;
				}
				r.zombie(false) ;
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				g_engine_queue.emplace_urgent( rrr.proc , r , fd , rrr.files , rrr.options ) ;                                      // urgent to ensure in order Kill/None
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				trace("make",r) ;
				return No/*done*/ ;
			}
		DF}                                                                                                                         // NO_COV
	}
	void start_connection(Fd fd) { Trace trace("start_connection",fd) ; slaves.try_emplace(fd) ; }
	void end_connection  (Fd fd) { Trace trace("end_connection"  ,fd) ; slaves.erase      (fd) ; }
	// data
	::umap<Fd,Req> slaves ;
	::stop_token   stop   ;
} ;

static LmakeServer  _g_server    { ServerMrkr } ;
static Atomic<bool> _g_done      = false        ;
static bool         _g_seen_make = false        ;

static ::string _os_compat(::string const& os_id) {
	::string res = os_id ;
	switch (res[0]) {
		case 'c' : if (res.starts_with("centos/"       )) res = "rhel"+res.substr(res.find('/')) ; break ; // centos       is inter-operable with rhel
		case 'o' : if (res.starts_with("opensuse-leap/")) res = "suse"+res.substr(res.find('/')) ; break ; // openSUSE     is inter-operable with all SUSE
		case 'r' : if (res.starts_with("rocky/"        )) res = "rhel"+res.substr(res.find('/')) ; break ; // rocky        is inter-operable with rhel
		case 's' : if (res.starts_with("sled/"         )) res = "suse"+res.substr(res.find('/')) ;         // SUSE desktop is inter-operable with all SUSE
		/**/       if (res.starts_with("sles/"         )) res = "suse"+res.substr(res.find('/')) ; break ; // SUSE server  is inter-operable with all SUSE
	DN}
	switch (res[0]) {
		case 'r' : if (res.starts_with("rhel/")) res = res.substr(0,res.find('.')) ; break ;               // ignore minor
		case 's' :                                                                   break ;               // XXX/ : suse 15.[45] does not support LD_AUDIT while 15.6 does, so minor cannot be ignored
	DN}
	return res ;
}
static void _chk_os() {
	static constexpr const char* ReleaseFile = "/etc/os-release" ;
	::vector_s lines      = AcFd(ReleaseFile,{.err_ok=true}).read_lines() ;
	::string   id         ;
	::string   version_id ;
	if (!lines) exit(Rc::System,"cannot find",ReleaseFile) ;
	for( ::string const& l : lines ) {
		if      (l.starts_with("ID="        )) id         = l.substr( 3/*ID=*/        ) ;
		else if (l.starts_with("VERSION_ID=")) version_id = l.substr(11/*VERSION_ID=*/) ;
	}
	if ( id        .starts_with('"') && id        .ends_with('"') ) id         = id        .substr(1,id        .size()-2) ;
	if ( version_id.starts_with('"') && version_id.ends_with('"') ) version_id = version_id.substr(1,version_id.size()-2) ;
	if ( !id                                                      ) exit(Rc::System,"cannot find ID in"        ,ReleaseFile) ;
	if ( !version_id                                              ) exit(Rc::System,"cannot find VERSION_ID in",ReleaseFile) ;
	//
	id << '/' << version_id ;
	if (_os_compat(id)!=_os_compat(OS_ID)) exit(Rc::System,"bad OS in ",ReleaseFile," : ",id,"!=",OS_ID) ;
}

static void _record_targets(Job job) {
	::string   targets_file  = cat(AdminDirS,"targets")                       ;
	::vector_s known_targets = AcFd(targets_file,{.err_ok=true}).read_lines() ;
	for( Node t : job->deps ) {
		::string tn = t->name() ;
		for( ::string& ktn : known_targets ) if (ktn==tn) ktn.clear() ;
		known_targets.push_back(tn) ;
	}
	::string content ; for( ::string tn : known_targets ) if (+tn) content << tn <<'\n' ;
	AcFd( targets_file , {O_WRONLY|O_TRUNC|O_CREAT} ).write( content ) ;
}

struct ReqEntry {
	IMsgBuf     buf = {} ;
	Req         req = {} ;
	SockFd::Key key = {} ;
} ;

static void _reqs_thread_func(::stop_token stop) {
	t_thread_key = 'Q' ;
	Trace trace("_reqs_thread_func",STR(_g_server.is_daemon)) ;
	//
	::stop_callback stop_cb { stop , [&](){ trace("stop") ; kill_self(SIGINT) ; } } ; // transform request_stop into an event we wait for
	_g_server.stop = stop ;
	//vvvvvvvvvvvvvvvvvvvv
	_g_server.event_loop() ;
	//^^^^^^^^^^^^^^^^^^^^
	_g_done = true ;
	g_engine_queue.emplace( GlobalProc::Wakeup ) ;                                    // ensure engine loop sees we are done
	trace("done") ;
}

static bool/*interrupted*/ _engine_loop() {
	Trace trace("_engine_loop") ;
	::umap<Req,Bool3/*out_active*/> fd_tab          ;                              // Maybe means both input and output are active, Yes means output is active, No means input is active
	Pdate                           next_stats_date = New ;
	for (;;) {
		bool empty = !g_engine_queue ;
		if (empty) {                                                               // we are about to block, do some book-keeping
			trace("wait") ;
			//vvvvvvvvvvvvvvvvv
			Backend::s_launch() ;                                                  // we are going to wait, tell backend as it may be retaining jobs to process them with as much info as possible
			//^^^^^^^^^^^^^^^^^
		}
	Retry :
		Pdate now           = New                 ;
		bool  refresh_stats = now>next_stats_date ;
		if (refresh_stats) {
			for( auto const& [r,_] : fd_tab ) if (+r->audit_fd) r->audit_stats() ; // refresh title
			next_stats_date = now+StatsRefresh ;
		}
		if ( empty && _g_done && !Req::s_n_reqs() && !g_engine_queue ) break ;
		::optional<EngineClosure> popped_closure ;
		if (refresh_stats) popped_closure = g_engine_queue.pop    (            ) ;
		else               popped_closure = g_engine_queue.pop_for(StatsRefresh) ;
		if (!popped_closure) goto Retry ;
		EngineClosure& closure = *popped_closure ;
		switch (closure.kind()) {
			case EngineClosureKind::Global : {
				switch (closure.ecg().proc) {
					case GlobalProc::Int :
						trace("int") ;
						//vvvvvvvvvvvvvvvvvvv
						Backend::s_kill_all() ;
						//^^^^^^^^^^^^^^^^^^^
						return true/*interrupted*/ ;
					case GlobalProc::Wakeup :
						trace("wakeup") ;
					break ;
				DF}                         // NO_COV
			} break ;
			case EngineClosureKind::Req : {
				EngineClosureReq& ecr           = closure.ecr()             ;
				Req               req           = ecr.req                   ;
				::string const&   startup_dir_s = ecr.options.startup_dir_s ;
				switch (ecr.proc) {
					case ReqProc::Collect : // PER_CMD : handle request coming from receiving thread, just add your Proc here if the request is answered immediately
					case ReqProc::Debug   :
					case ReqProc::Forget  :
					case ReqProc::Mark    :
					case ReqProc::Show    : {
						trace(ecr) ;
						bool ok = true/*garbage*/ ;
						if ( !ecr.options.flags[ReqFlag::Quiet] && +startup_dir_s )
							audit( ecr.fd , ecr.options , Color::Note , cat("startup dir : ",startup_dir_s,rm_slash) , true/*as_is*/ ) ;
						try                        { ok = g_cmd_tab[+ecr.proc](ecr) ;                              }
						catch (::string  const& e) { ok = false ; if (+e) audit(ecr.fd,ecr.options,Color::Err,e) ; }
						try                       { OMsgBuf( ReqRpcReply(ReqRpcReplyProc::Status,ok?Rc::Ok:Rc::Fail) ).send( ecr.fd , {}/*key*/ ) ; }
						catch (::string const& e) { trace("lost_client",e) ;                                                                        } // we cant do much if we cant communicate
						_g_server.close_slave_out(ecr.fd) ;
					} break ;
					// 2 possible orders : Make-Kill-Close or Make-Close-Kill
					// None counts as Kill
					// read  side is closed upon Kill  (cannot be upon Close as epoll.del must be called before close)
					// write side is closed upon Close (cannot be upon Kill  as this may trigger lmake command termination, which, in turn, will trigger eof on the read side
					case ReqProc::Make : {
						bool     allocated = false ;
						::string msg       ;
						if (req.zombie()) {                                                                  // if already zombie, dont make req
							trace("zombie_when_make",req) ;
							goto NoMake ;
						}
						try {
							try {
								Makefiles::refresh( /*out*/msg , ecr.options.user_env , false/*rescue*/  , true/*refresh*/ , startup_dir_s ) ;
								if (+msg) audit_err( ecr.fd , ecr.options , msg ) ;
								trace("new_req",req) ;
								req.alloc() ; allocated = true ;
								//vvvvvvvvvvv
								req.make(ecr) ;
								//^^^^^^^^^^^
								_g_seen_make = true ;
							} catch(::string const& e) { throw ::pair(e,Rc::BadState) ; }
						} catch(::pair_s<Rc> const& e) {
							if (allocated) req.dealloc() ;
							if (+msg) audit_err   ( ecr.fd , ecr.options , msg                  ) ;
							/**/      audit_err   ( ecr.fd , ecr.options , Color::Err , e.first ) ;
							/**/      audit_status( ecr.fd , ecr.options , e.second             ) ;
							trace("cannot_refresh",req) ;
							goto NoMake ;
						}
						if (!ecr.is_job()) _record_targets(req->job) ;
						SWEAR( +ecr.fd , ecr.fd ) ;
						fd_tab[req] = Maybe ;                                                                // in and out are both active
						break ;
					NoMake :
						_g_server.close_slave_out(ecr.fd) ;
					} break ;
					case ReqProc::Close : {
						auto   it         = fd_tab.find(req) ; SWEAR(it!=fd_tab.end()) ;
						Bool3& out_active = it->second       ;
						trace("close_req",ecr,out_active) ;
						_g_server.close_slave_out(req->audit_fd) ;
						//vvvvvvvvv
						req.close() ;
						//^^^^^^^^^
						if (out_active==Maybe)   out_active = No ;                                           // mark req is closed
						else                   { fd_tab.erase(it) ; req.dealloc() ; }                        // dealloc when req can be reused, i.e. after Kill and Close
					} break ;
					case ReqProc::Kill :
					case ReqProc::None : {
						auto   it         = fd_tab.find(req) ; if (it==fd_tab.end()) { trace("was_zombie_when_make",ecr) ; break ; }
						Bool3& out_active = it->second       ;
						trace("kill_req",ecr,out_active) ;
						//                                             vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
						if (out_active!=No) { SWEAR( +req && +*req ) ; req.kill(ecr.proc==ReqProc::Kill) ; } // kill req if not already closed
						//                                             ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
						if (out_active==Maybe)   out_active = Yes ;                                          // mark req is killed
						else                   { fd_tab.erase(it) ; req.dealloc() ; }                        // dealloc when req can be reused, i.e. after Kill and Close
					} break ;
				DF}                                                                                          // NO_COV
			} break ;
			case EngineClosureKind::Job : {
				EngineClosureJob& ecj = closure.ecj() ;
				JobExec         & je  = ecj.job_exec  ;
				trace("job",ecj.proc(),je) ;
				Req::s_new_etas() ;                                                                          // regularly adjust queued job priorities if necessary
				switch (ecj.proc()) {
					//                             vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					case JobRpcProc::Start       : je.started     ( ecj.start().report , ecj.start().report_unlnks , ecj.start().msg_stderr ) ; break ;
					case JobRpcProc::ReportStart : je.report_start(                                                                         ) ; break ;
					case JobRpcProc::GiveUp      : je.give_up     ( ecj.give_up().req , ecj.give_up().report                                ) ; break ;
					case JobRpcProc::End         : je.end         ( ::move(ecj.end())                                                       ) ; break ;
					//                             ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				DF}                                                                                          // NO_COV
			} break ;
			case EngineClosureKind::JobMngt : {
				EngineClosureJobMngt& ecjm = closure.ecjm() ;
				JobExec             & je   = ecjm.job_exec  ;
				trace("job_mngt",ecjm.proc,je) ;
				switch (ecjm.proc) {
					//                             vvvvvvvvvvvvvvvvvvvvvvvvv
					case JobMngtProc::LiveOut    : je.live_out    (ecjm.txt) ; break ;
					case JobMngtProc::AddLiveOut : je.add_live_out(ecjm.txt) ; break ;
					//                             ^^^^^^^^^^^^^^^^^^^^^^^^^
					case JobMngtProc::ChkDeps    :
					case JobMngtProc::DepDirect  :
					case JobMngtProc::DepVerbose : {
						JobMngtRpcReply jmrr = je.manage(ecjm) ;
						jmrr.fd     = ecjm.fd     ;
						jmrr.seq_id = ecjm.seq_id ;
						//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
						Backends::send_reply( +je , ::move(jmrr) ) ;
						//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					} break ;
				DF}                                                                                          // NO_COV
			} break ;
		DF}                                                                                                  // NO_COV
	}
	trace("done") ;
	return false/*interrupted*/ ;
}

int main( int argc , char** argv ) {
	//
	Trace::s_backup_trace = true                                                    ;
	g_writable            = !repo_app_init({ .cd_root=false  ,.chk_version=Maybe }) ;                                                // server is always launched at root
	if (Record::s_is_simple(*g_repo_root_s)) exit(Rc::Usage,"cannot use lmake inside a system directory ",*g_repo_root_s,rm_slash) ; // all local files would be seen as simple, defeating autodep
	_chk_os() ;
	::umap_ss user_env = Makefiles::clean_env(false/*under_lmake_ok*/) ;
	Py::init(*g_lmake_root_s) ;
	AutodepEnv ade ;
	ade.repo_root_s         = *g_repo_root_s ;
	Record::s_static_report = true           ;
	Record::s_autodep_env(ade) ;
	set_env("LMAKE_AUTODEP_ENV",ade) ;
	//
	bool     refresh_      = true ;
	bool     is_daemon     = true ;
	::string startup_dir_s ;
	for( int i : iota(1,argc) ) {
		if (argv[i][0]=='-')
			switch (argv[i][1]) {
				case 'c' : startup_dir_s << (argv[i]+2)<<add_slash ;                    continue ;
				case 'd' : is_daemon     =  false                  ; if (argv[i][2]==0) continue ; break ;
				case 'r' : refresh_      =  false                  ; if (argv[i][2]==0) continue ; break ;
				case 'R' : g_writable    =  false                  ; if (argv[i][2]==0) continue ; break ;
				case '-' :                                           if (argv[i][2]==0) continue ; break ;
			}
		exit(Rc::Usage,"unrecognized argument : ",argv[i],"\nsyntax :",*g_exe_name," [-cstartup_dir_s] [-d/*no_daemon*/] [-r/*no makefile refresh*/]") ;
	}
	block_sigs({SIGCHLD,SIGHUP,SIGINT,SIGPIPE}) ;                 //     SIGCHLD,SIGHUP,SIGINT : to capture it using signalfd ...
	Trace trace("main",getpid(),*g_lmake_root_s,*g_repo_root_s) ; // ... SIGPIPE               : to generate error on write rather than a signal when reading end is dead ...
	for( int i : iota(argc) ) trace("arg",i,argv[i]) ;            // ... must be done before any thread is launched so that all threads block the signal
	try {
		_g_server.handle_int = true       ;
		_g_server.is_daemon  = is_daemon  ;
		_g_server.writable   = g_writable ;
		_g_server.start() ;
	} catch (::pair_s<Rc> const& e) {
		if (+e.first) exit( e.second , "cannot start ",*g_exe_name," : ",e.first ) ;
		else          exit( e.second                                             ) ;
	}
	//                             vvvvvvvvvvvvvvvvv
	static ::jthread reqs_thread { _reqs_thread_func } ;
	//                             ^^^^^^^^^^^^^^^^^
	::string     msg ;
	::pair_s<Rc> rc  { {} , Rc::Ok } ;
	//                             vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	try                          { Makefiles::refresh( /*out*/msg , user_env , _g_server.rescue , refresh_ , startup_dir_s ) ; }
	//                             ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	catch(::string     const& e) { rc = { e , Rc::BadState } ;                                                                 }
	catch(::pair_s<Rc> const& e) { rc = e                    ;                                                                 }
	//
	if (+msg      ) Fd::Stderr.write(with_nl(msg)) ;
	if (+rc.second) exit( rc.second , rc.first )   ;
	if (!is_daemon) ::setpgid(0/*pid*/,0/*pgid*/)  ;              // once we have reported we have started, lmake will send us a message to kill us
	//
	Trace::s_channels = g_config->trace.channels ;
	Trace::s_sz       = g_config->trace.sz       ;
	if (_g_server.writable) Trace::s_new_trace_file( g_config->local_admin_dir_s+"trace/"+*g_exe_name ) ;
	Codec::CodecLock::s_init() ;
	Job             ::s_init() ;
	//                             vvvvvvvvvvvvvv
	/**/   bool      interrupted = _engine_loop() ;
	//                             ^^^^^^^^^^^^^^
	if (_g_server.writable) {
		try { unlnk_inside_s(cat(AdminDirS,"auto_tmp/"),{.force=true}) ; } catch (::string const&) {}                    // cleanup
		if (_g_seen_make) AcFd( cat(PrivateAdminDirS,"kpi") , {O_WRONLY|O_TRUNC|O_CREAT} ).write( g_kpi.pretty_str() ) ;
	}
	//
	Backend::s_finalize() ;
	trace("done",STR(interrupted),New) ;
	return interrupted ;
}
