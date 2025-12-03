// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh" // /!\ must be first to include Python.h first

#include <sys/inotify.h>

#include "rpc_client.hh"
#include "rpc_job_exec.hh"
#include "autodep/record.hh"
#include "cmd.hh"
#include "makefiles.hh"

using namespace Disk   ;
using namespace Engine ;
using namespace Time   ;

enum class EventKind : uint8_t {
	Master
,	Slave
,	Stop
,	Std
,	Int
,	Watch
} ;

static constexpr Delay StatsRefresh { 1 } ;

static ServerSockFd    _g_server_fd ;
static bool            _g_is_daemon = true                  ;
static Atomic<bool>    _g_done      = false                 ;
static bool            _g_seen_make = false                 ;
static Fd              _g_watch_fd  ;                         // watch LMAKE/server

static ::pair_s/*fqdn*/<pid_t> _get_mrkr() {
	try {
		::vector_s lines = AcFd(ServerMrkr).read_lines() ; throw_unless(lines.size()==2) ;
		return { SockFd::s_host(lines[0]/*service*/) , from_string<pid_t>(lines[1]) } ; }
	catch (::string const&) {
		return { {}/*fqdn*/ , 0/*pid*/ } ;
	}
}

static void _server_cleanup() {
	Trace trace("_server_cleanup") ;
	unlnk(File(ServerMrkr)) ;
}

static void _report_server( Fd fd , bool running ) {
	Trace trace("_report_server",STR(running)) ;
	int cnt = ::write( fd , &running , sizeof(bool) ) ;
	if (cnt!=sizeof(bool)) trace("no_report") ;         // client is dead
}

static ::pair_s/*msg*/<Rc> _start_server(bool&/*out*/ rescue) { // Maybe means last server crashed
	::pair_s<pid_t>     mrkr      { fqdn() , ::getpid() } ;
	::pair_s<pid_t>     file_mrkr = _get_mrkr()           ;
	::pair_s/*msg*/<Rc> res       = { {} , Rc::Ok }       ;
	Trace trace("_start_server",mrkr) ;
	if ( +file_mrkr.first && file_mrkr.first!=mrkr.first ) {
		trace("already_existing_elsewhere",file_mrkr) ;
		return { {}/*msg*/ , Rc::BadServer } ;
	}
	if (file_mrkr.second) {
		if (sense_process(file_mrkr.second)) {                  // another server exists on same host
			trace("already_existing",file_mrkr) ;
			return { {}/*msg*/ , Rc::BadServer } ;
		}
		unlnk(File(ServerMrkr)) ;                               // before doing anything, we create the marker, which is unlinked at the end, so it is a marker of a crash
		rescue = true ;
		trace("vanished",file_mrkr) ;
	}
	if (g_writable) {
		::string tmp = cat(ServerMrkr,'.',mrkr.first,'.',mrkr.second) ;
		_g_server_fd = ServerSockFd( 0/*backlog*/ , false/*reuse_addr*/ )   ;
		AcFd( tmp , {O_WRONLY|O_TRUNC|O_CREAT,0666/*mod*/} ).write(cat(
			_g_server_fd.service_str(mrkr.first) , '\n'
		,	mrkr.second                          , '\n'
		)) ;
		//  vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		if (::link(tmp.c_str(),ServerMrkr)==0) {
			::atexit(_server_cleanup) ;
			//^^^^^^^^^^^^^^^^^^^^^^^
			// if server marker is touched by user, we do as we received a ^C
			// ideally, we should watch ServerMrkr before it is created to be sure to miss nothing, but inotify requires an existing file
			if ( +(_g_watch_fd=::inotify_init1(O_CLOEXEC)) )
				if ( ::inotify_add_watch( _g_watch_fd , ServerMrkr , IN_DELETE_SELF|IN_MOVE_SELF|IN_MODIFY )<0 )
					_g_watch_fd.close() ;                                                                        // useless if we cannot watch
		} else {
			res = { StrErr() , Rc::System } ;
		}
		unlnk(tmp) ;
	}
	trace("started",STR(_g_is_daemon),res) ;
	return res ;
}

::string _os_compat(::string const& os_id) {
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
	AcFd( targets_file , {O_WRONLY|O_TRUNC|O_CREAT,0666/*mod*/} ).write( content ) ;
}

struct ReqEntry {
	IMsgBuf     buf = {} ;
	Req         req = {} ;
	SockFd::Key key = {} ;
} ;

static void _reqs_thread_func( ::stop_token stop , Fd in_fd , Fd out_fd ) {
	using Event = Epoll<EventKind>::Event ;
	t_thread_key = 'Q' ;
	Trace trace("_reqs_thread_func",STR(_g_is_daemon)) ;
	//
	::stop_callback     stop_cb    { stop , [&](){ trace("stop") ; kill_self(SIGINT) ; } } ;                       // transform request_stop into an event we wait for
	::umap<Fd,ReqEntry> req_slaves ;
	Epoll<EventKind>    epoll      { New }                                                 ;
	//
	if (g_writable  ) { epoll.add_read( _g_server_fd , EventKind::Master ) ; trace("read_master",_g_server_fd) ; } // if read-only, we do not expect additional connections
	/**/              { epoll.add_sig ( SIGHUP       , EventKind::Int    ) ; trace("read_hup"                ) ; }
	/**/              { epoll.add_sig ( SIGINT       , EventKind::Int    ) ; trace("read_int"                ) ; }
	if (+_g_watch_fd) { epoll.add_read( _g_watch_fd  , EventKind::Watch  ) ; trace("read_watch" ,_g_watch_fd ) ; }
	//
	if (!_g_is_daemon) {
		req_slaves[in_fd] ;                                                                                        // allocate entry
		epoll.add_read(in_fd,EventKind::Std) ; trace("read_std",in_fd) ;
	}
	//
	for(;;) {
		::vector<Event> events = epoll.wait() ;
		bool            new_fd = false        ;
		for( Event event : events ) {
			EventKind kind = event.data() ;
			Fd        fd   = event.fd  () ;
			trace("event",kind,fd) ;
			switch (kind) {
				// it may be that in a single poll, we get the end of a previous run and a request for a new one
				// problem lies in this sequence :
				// - lmake foo
				// - touch Lmakefile.py
				// - lmake bar          => maybe we get this request in the same poll as the end of lmake foo and we would eroneously say that it cannot be processed
				// solution is to delay Master event after other events and ignore them if we are done inbetween
				// note that there may at most a single Master event
				case EventKind::Master :
					SWEAR( !new_fd , new_fd ) ;
					new_fd = true ;
				break ;
				case EventKind::Int   :
				case EventKind::Watch : {
					if (stop.stop_requested()) {
						trace("stop_requested") ;
						goto Done ;
					}
					if (kind==EventKind::Watch) {
						struct inotify_event event ;
						ssize_t              cnt   = ::read( _g_watch_fd , &event , sizeof(event) ) ;
						SWEAR( cnt==sizeof(event) , cnt ) ;
						trace("watch",event.mask) ;
					}
					{	Lock lock { Req::s_reqs_mutex } ;
						for( Req r : Req::s_reqs_by_start() ) {
							trace("all_zombie",r) ;
							r.zombie(true) ;
						}
					}
					//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					g_engine_queue.emplace_urgent(GlobalProc::Int) ;                                               // this will close ofd when done writing to it, urgent to ensure good reactivity
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				} break ;
				case EventKind::Slave :
				case EventKind::Std   : {
					auto      rit = req_slaves.find(fd) ; SWEAR( rit!=req_slaves.end() , fd ) ;
					ReqEntry& re  = rit->second                                               ;
					for( Bool3 fetch=Yes ;; fetch=No ) {
						::optional<ReqRpcReq> received = re.buf.receive_step<ReqRpcReq>( fd , fetch , re.key ) ; if (!received) break ; // partial message
						ReqRpcReq&            rrr      = *received                                             ;
						Fd                    ofd      = kind==EventKind::Std ? out_fd : fd                    ;
						trace("req",rrr) ;
						switch (rrr.proc) {
							case ReqProc::Kill :
							case ReqProc::None : {
								Req r = re.req ;
								trace("eof",fd,r) ;
								if (+r) { trace("zombie",r) ; r.zombie(true) ; }             // make req zombie immediately to optimize reaction time
								epoll.del(false/*write*/,fd) ; trace("del_fd",rrr.proc,fd) ; // /!\ must precede close(fd) which may occur as soon as we push to g_engine_queue
								req_slaves.erase(rit) ;
								//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
								g_engine_queue.emplace_urgent( rrr.proc , r , fd , ofd ) ;   // this will close ofd when done writing to it, urgent to ensure good reactivity
								//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
							} goto NextEvent ;
							case ReqProc::Collect : // PER_CMD : handle request coming from receiving thread, just add your Proc here if the request is answered immediately
							case ReqProc::Debug   :
							case ReqProc::Forget  :
							case ReqProc::Mark    :
								SWEAR(g_writable) ;
							[[fallthrough]] ;
							case ReqProc::Show :
								epoll.del(false/*write*/,fd) ; trace("del_fd",rrr.proc,fd) ;                     // /!\ must precede close(fd) which may occur as soon as we push to g_engine_queue
								req_slaves.erase(rit) ;
								//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
								g_engine_queue.emplace_urgent( rrr.proc , fd , ofd , rrr.files , rrr.options ) ; // urgent to ensure in order Kill/None
								//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
							goto NextEvent ;
							case ReqProc::Make : {
								SWEAR(g_writable) ;
								Req r ;
								try {
									r = New ;
								} catch (::string const& e) {
									audit( ofd , rrr.options , Color::None , e , true/*as_is*/ ) ;
									try                       { OMsgBuf( ReqRpcReply(ReqRpcReplyProc::Status,Rc::Fail) ).send( ofd , {}/*key*/ ) ; }
									catch (::string const& e) { trace("lost_client",e) ;                                                           } // we cant do much if we cant communicate
									if (ofd!=fd) ::close   (ofd        ) ;
									else         ::shutdown(ofd,SHUT_WR) ;
									break ;
								}
								r.zombie(false) ;
								re.req = r ;
								//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
								g_engine_queue.emplace_urgent( rrr.proc , r , fd , ofd , rrr.files , rrr.options ) ;                                 // urgent to ensure in order Kill/None
								//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
								trace("make",r) ;
							} break ;
						DF}                                                                                                                          // NO_COV
					}
				} break ;
			DF}                                                                                                                                      // NO_COV
		NextEvent : ;
		}
		//
		if ( !_g_is_daemon && !req_slaves ) break ;                                 // check end of loop after processing slave events and before master events
		//
		if (new_fd) {
			Fd slave_fd = _g_server_fd.accept().detach() ;
			req_slaves[slave_fd] = {.key=_g_server_fd.key} ;                        // allocate entry
			epoll.add_read(slave_fd,EventKind::Slave) ; trace("new_req",slave_fd) ;
			_report_server(slave_fd,true/*running*/) ;
		}
	}
Done :
	_g_done = true ;
	g_engine_queue.emplace( GlobalProc::Wakeup ) ;                                  // ensure engine loop sees we are done
	trace("done") ;
}

static bool/*interrupted*/ _engine_loop() {
	struct FdEntry {
		Fd in  ;
		Fd out ;
	} ;
	Trace trace("_engine_loop") ;
	::umap<Req,FdEntry> fd_tab          ;
	Pdate               next_stats_date = New ;
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
							audit( ecr.out_fd , ecr.options , Color::Note , cat("startup dir : ",startup_dir_s,rm_slash) , true/*as_is*/ ) ;
						try                        { ok = g_cmd_tab[+ecr.proc](ecr) ;                                  }
						catch (::string  const& e) { ok = false ; if (+e) audit(ecr.out_fd,ecr.options,Color::Err,e) ; }
						try                       { OMsgBuf( ReqRpcReply(ReqRpcReplyProc::Status,ok?Rc::Ok:Rc::Fail) ).send( ecr.out_fd , {}/*key*/ ) ; }
						catch (::string const& e) { trace("lost_client",e) ;                                                                            } // we cant do much if we cant communicate
						if (ecr.out_fd!=ecr.in_fd) ecr.out_fd.close() ;                                       // close out_fd before in_fd as closing clears out_fd, defeating the equality test
						/**/                       ecr.in_fd .close() ;
					} break ;
					// 2 possible orders : Make-Kill-Close or Make-Close-Kill
					// None counts as Kill
					// read  side is closed upon Kill  (cannot be upon Close as epoll.del must be called before close)
					// write side is closed upon Close (cannot be upon Kill  as this may trigger lmake command termination, which, in turn, will trigger eof on the read side
					case ReqProc::Make : {
						bool     allocated = false ;
						::string msg       ;
						if (req.zombie()) {                                                                   // if already zombie, dont make req
							trace("zombie_when_make",req) ;
							goto NoMake ;
						}
						try {
							try {
								Makefiles::refresh( /*out*/msg , ecr.options.user_env , false/*rescue*/  , true/*refresh*/ , startup_dir_s ) ;
								if (+msg) audit_err( ecr.out_fd , ecr.options , msg ) ;
								trace("new_req",req) ;
								req.alloc() ; allocated = true ;
								//vvvvvvvvvvv
								req.make(ecr) ;
								//^^^^^^^^^^^
								_g_seen_make = true ;
							} catch(::string const& e) { throw ::pair(e,Rc::BadState) ; }
						} catch(::pair_s<Rc> const& e) {
							if (allocated) req.dealloc() ;
							if (+msg) audit_err   ( ecr.out_fd , ecr.options , msg                  ) ;
							/**/      audit_err   ( ecr.out_fd , ecr.options , Color::Err , e.first ) ;
							/**/      audit_status( ecr.out_fd , ecr.options , e.second             ) ;
							trace("cannot_refresh",req) ;
							goto NoMake ;
						}
						if (!ecr.is_job()) _record_targets(req->job) ;
						SWEAR( +ecr.in_fd && +ecr.out_fd , ecr.in_fd , ecr.out_fd ) ;                         // in_fd and out_fd are used as marker for killed and closed respectively
						fd_tab[req] = { .in=ecr.in_fd , .out=ecr.out_fd } ;
						break ;
					NoMake :
						if (ecr.in_fd!=ecr.out_fd) ::close   (ecr.out_fd        ) ;                           // do as if immediate Close
						else                       ::shutdown(ecr.out_fd,SHUT_WR) ;                           // .
					} break ;
					case ReqProc::Close : {
						auto     it  = fd_tab.find(req) ; SWEAR(it!=fd_tab.end()) ;
						FdEntry& fde = it->second       ;
						trace("close_req",ecr,fde.in,fde.out) ;
						//vvvvvvvvv
						req.close() ;
						//^^^^^^^^^
						if (fde.in!=fde.out)   ::close   (fde.out        ) ;                                  // either finalize close after Kill or in and out are different from the beginning
						else                   ::shutdown(fde.out,SHUT_WR) ;                                  // close only output side
						if (+fde.in        )   fde.out = Fd() ;                                               // mark req is closed
						else                 { fd_tab.erase(it) ; req.dealloc() ; }                           // dealloc when req can be reused, i.e. after Kill and Close
					} break ;
					case ReqProc::Kill :
					case ReqProc::None : {
						auto     it  = fd_tab.find(req) ; if (it==fd_tab.end()) { trace("was_zombie_when_make",ecr) ; break ; }
						FdEntry& fde = it->second       ;
						trace("kill_req",ecr,fde.in,fde.out) ;
						//                                              vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
						if (+fde.out       ) { SWEAR( +req && +*req ) ; req.kill(ecr.proc==ReqProc::Kill) ; } // kill req if not already closed
						//                                              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
						if (fde.in!=fde.out)   ::close   (fde.in        ) ;                                   // either finalize close after Close or in and out are different from the beginning
						else                   ::shutdown(fde.in,SHUT_RD) ;                                   // close only input side
						if (+fde.out       )   fde.in = Fd() ;                                                // mark req is killed
						else                 { fd_tab.erase(it) ; req.dealloc() ; }                           // dealloc when req can be reused, i.e. after Kill and Close
					} break ;
				DF}                                                                                           // NO_COV
			} break ;
			case EngineClosureKind::Job : {
				EngineClosureJob& ecj = closure.ecj() ;
				JobExec         & je  = ecj.job_exec  ;
				trace("job",ecj.proc(),je) ;
				Req::s_new_etas() ;                                                                           // regularly adjust queued job priorities if necessary
				switch (ecj.proc()) {
					//                             vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					case JobRpcProc::Start       : je.started     ( ecj.start().report , ecj.start().report_unlnks , ecj.start().msg_stderr ) ; break ;
					case JobRpcProc::ReportStart : je.report_start(                                                                         ) ; break ;
					case JobRpcProc::GiveUp      : je.give_up     ( ecj.give_up().req , ecj.give_up().report                                ) ; break ;
					case JobRpcProc::End         : je.end         ( ::move(ecj.end())                                                       ) ; break ;
					//                             ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				DF}                                                                                           // NO_COV
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
				DF}                                                                                           // NO_COV
			} break ;
		DF}                                                                                                   // NO_COV
	}
	trace("done") ;
	return false/*interrupted*/ ;
}

int main( int argc , char** argv ) {

	Trace::s_backup_trace = true ;
	g_writable = !app_init({ .read_only_ok=true , .chk_version=Maybe }) ;                                                          // server is always launched at root
	if (Record::s_is_simple(*g_repo_root_s)) exit(Rc::Usage,"cannot use lmake inside system directory ",*g_repo_root_s,rm_slash) ; // all local files would be seen as simple, defeating autodep
	_chk_os() ;
	Py::init(*g_lmake_root_s) ;
	AutodepEnv ade ;
	ade.repo_root_s         = *g_repo_root_s ;
	Record::s_static_report = true           ;
	Record::s_autodep_env(ade) ;
	if (+*g_startup_dir_s) {
		g_startup_dir_s->pop_back() ;
		FAIL("lmakeserver must be started from repo root, not from ",*g_startup_dir_s) ; // NO_COV
	}
	//
	bool refresh_ = true       ;
	Fd   in_fd    = Fd::Stdin  ;
	Fd   out_fd   = Fd::Stdout ;
	for( int i : iota(1,argc) ) {
		if (argv[i][0]!='-') goto Bad ;
		switch (argv[i][1]) {
			case 'c' : g_startup_dir_s = new ::string{argv[i]+2}     ;                               break ;
			case 'd' : _g_is_daemon    = false                       ; if (argv[i][2]!=0) goto Bad ; break ;
			case 'i' : in_fd           = from_string<int>(argv[i]+2) ;                               break ;
			case 'o' : out_fd          = from_string<int>(argv[i]+2) ;                               break ;
			case 'r' : refresh_        = false                       ; if (argv[i][2]!=0) goto Bad ; break ;
			case 'R' : g_writable      = false                       ; if (argv[i][2]!=0) goto Bad ; break ;
			case '-' :                                                 if (argv[i][2]!=0) goto Bad ; break ;
			default : exit(Rc::Usage,"unrecognized option : ",argv[i]) ;
		}
		continue ;
	Bad :
		exit(Rc::Usage,"unrecognized argument : ",argv[i],"\nsyntax :",*g_exe_name," [-cstartup_dir_s] [-d/*no_daemon*/] [-r/*no makefile refresh*/]") ;
	}
	if (+g_startup_dir_s) SWEAR( is_dir_name(*g_startup_dir_s) , *g_startup_dir_s ) ;
	else                  g_startup_dir_s = new ::string ;
	//
	block_sigs({SIGCHLD,SIGHUP,SIGINT,SIGPIPE}) ;                                        //     SIGCHLD,SIGHUP,SIGINT : to capture it using signalfd ...
	Trace trace("main",getpid(),*g_lmake_root_s,*g_repo_root_s) ;                        // ... SIGPIPE               : to generate error on write rather than a signal when reading end is dead ...
	for( int i : iota(argc) ) trace("arg",i,argv[i]) ;                                   // ... must be done before any thread is launched so that all threads block the signal
	bool rescue = false ;
	//                          vvvvvvvvvvvvvvvvvvvvvvvvvvvv
	::pair_s<Rc> start_digest = _start_server(/*out*/rescue) ;
	//                          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	if (!_g_is_daemon       ) _report_server( out_fd , start_digest.second==Rc::Ok ) ;   // inform lmake we did not start
	if (+start_digest.second) {
		if (+start_digest.first) exit( start_digest.second , "cannot start server : ",start_digest.first ) ;
		else                     exit( Rc::Ok                                                            ) ;
	}
	::string     msg ;
	::pair_s<Rc> rc  { {} , Rc::Ok } ;
	//                             vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	try                          { Makefiles::refresh( /*out*/msg , mk_environ() , rescue , refresh_ , *g_startup_dir_s ) ; }
	//                             ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	catch(::string     const& e) { rc = { e , Rc::BadState } ;                                                              }
	catch(::pair_s<Rc> const& e) { rc = e                    ;                                                              }
	//
	if (+msg         ) Fd::Stderr.write(with_nl(msg)) ;
	if (+rc.second   ) exit( rc.second , rc.first )   ;
	if (!_g_is_daemon) ::setpgid(0/*pid*/,0/*pgid*/)  ;                                  // once we have reported we have started, lmake will send us a message to kill us
	//
	Trace::s_channels = g_config->trace.channels ;
	Trace::s_sz       = g_config->trace.sz       ;
	if (g_writable) Trace::s_new_trace_file( g_config->local_admin_dir_s+"trace/"+*g_exe_name ) ;
	Job::s_init() ;
	Codec::s_init() ;
	//
	static ::jthread reqs_thread { _reqs_thread_func , in_fd , out_fd } ;
	//
	//                 vvvvvvvvvvvvvv
	bool interrupted = _engine_loop() ;
	//                 ^^^^^^^^^^^^^^
	if (g_writable) {
		try { unlnk_inside_s(cat(AdminDirS,"auto_tmp/"),{.force=true}) ; } catch (::string const&) {}                                // cleanup
		if (_g_seen_make) AcFd( cat(PrivateAdminDirS,"kpi") , {O_WRONLY|O_TRUNC|O_CREAT,0666/*mod*/} ).write( g_kpi.pretty_str() ) ;
	}
	//
	Backend::s_finalize() ;
	trace("done",STR(interrupted),New) ;
	return interrupted ;
}
