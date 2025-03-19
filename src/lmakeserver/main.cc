// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <sys/inotify.h>

#include "core.hh" // must be first to include Python.h first

#include "rpc_client.hh"
#include "autodep/record.hh"
#include "cmd.hh"
#include "makefiles.hh"

using namespace Disk   ;
using namespace Engine ;
using namespace Time   ;

ENUM( EventKind
,	Master
,	Slave
,	Stop
,	Std
,	Int
,	Watch
)

static constexpr Delay StatsRefresh { 1 } ;

static ServerSockFd _g_server_fd      ;
static bool         _g_is_daemon      = true   ;
static Atomic<bool> _g_done           = false  ;
static bool         _g_server_running = false  ;
static ::string     _g_host           = host() ;
static bool         _g_seen_make      = false  ;
static Fd           _g_watch_fd       ;          // watch LMAKE/server

static ::pair_s<int> _get_mrkr_host_pid() {
	try {
		::vector_s lines = AcFd(ServerMrkr).read_lines() ; throw_unless(lines.size()==2) ;
		::string const& service    = lines[0] ;
		::string const& server_pid = lines[1] ;
		return { SockFd::s_host(service) , from_string<pid_t>(server_pid) } ; }
	catch (::string const&) {
		return {{},0} ;
	}
}

static void _server_cleanup() {
	Trace trace("_server_cleanup",STR(_g_server_running)) ;
	if (!_g_server_running) return ;                        // not running, nothing to clean
	pid_t           pid  = getpid()             ;
	::pair_s<pid_t> mrkr = _get_mrkr_host_pid() ;
	trace("pid",mrkr,pid) ;
	if (mrkr!=::pair(_g_host,pid)) return ;                 // not our file, dont touch it
	unlnk(ServerMrkr) ;
	trace("cleaned") ;
}

static void _report_server( Fd fd , bool running ) {
	Trace trace("_report_server",STR(running)) ;
	int cnt = ::write( fd , &running , sizeof(bool) ) ;
	if (cnt!=sizeof(bool)) trace("no_report") ;         // client is dead
}

static bool/*crashed*/ _start_server() {
	bool  crashed = false    ;
	pid_t pid     = getpid() ;
	Trace trace("_start_server",_g_host,pid) ;
	::pair_s<pid_t> mrkr = _get_mrkr_host_pid() ;
	if ( +mrkr.first && mrkr.first!=_g_host ) {
		trace("already_existing_elsewhere",mrkr) ;
		return false/*unused*/ ;                   // if server is running on another host, we cannot qualify with a kill(pid,0), be pessimistic
	}
	if (mrkr.second) {
		if (kill_process(mrkr.second,0)) {         // another server exists
			trace("already_existing",mrkr) ;
			return false/*unused*/ ;
		}
		unlnk(ServerMrkr) ;                        // before doing anything, we create the marker, which is unlinked at the end, so it is a marker of a crash
		crashed = true ;
		trace("vanished",mrkr) ;
	}
	if (!g_writable) {
		_g_server_running = true ;
	} else {
		_g_server_fd.listen() ;
		::string tmp = ""s+ServerMrkr+'.'+_g_host+'.'+pid ;
		AcFd(tmp,Fd::Write).write(
			_g_server_fd.service() + '\n'
		+	getpid()               + '\n'
		) ;
		//vvvvvvvvvvvvvvvvvvvvvv
		::atexit(_server_cleanup) ;
		//^^^^^^^^^^^^^^^^^^^^^^
		_g_server_running = true ;                 // while we link, pretend we run so cleanup can be done if necessary
		fence() ;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		_g_server_running = ::link( tmp.c_str() , ServerMrkr )==0 ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		_g_watch_fd = ::inotify_init1(O_CLOEXEC) ; // start watching file as soon as possible (ideally would be before)
		unlnk(tmp) ;
		trace("started",STR(crashed),STR(_g_is_daemon),STR(_g_server_running)) ;
	}
	return crashed ;
}

static void _chk_os() {
	static constexpr const char* ReleaseFile = "/etc/os-release" ;
	::vector_s lines      = AcFd(ReleaseFile).read_lines(true/*no_file_ok*/) ;
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
	id << '-'<<version_id ;
	if (id!=OS_ID) exit(Rc::System,"bad OS in",ReleaseFile,':',id,"!=",OS_ID) ;
}

static void _record_targets(Job job) {
	::string   targets_file  = AdminDirS+"targets"s                              ;
	::vector_s known_targets = AcFd(targets_file).read_lines(true/*no_file_ok*/) ;
	for( Node t : job->deps ) {
		::string tn = t->name() ;
		for( ::string& ktn : known_targets ) if (ktn==tn) ktn.clear() ;
		known_targets.push_back(tn) ;
	}
	::string content ; for( ::string tn : known_targets ) if (+tn) content << tn <<'\n' ;
	AcFd(targets_file,Fd::Write).write(content) ;
}

static void _reqs_thread_func( ::stop_token stop , Fd in_fd , Fd out_fd ) {
	using Event = Epoll<EventKind>::Event ;
	t_thread_key = 'Q' ;
	Trace trace("_reqs_thread_func",STR(_g_is_daemon)) ;
	//
	::stop_callback              stop_cb { stop , [&](){ trace("stop") ; kill_self(SIGINT) ; } } ;               // transform request_stop into an event we wait for
	::umap<Fd,pair<IMsgBuf,Req>> in_tab  ;
	Epoll<EventKind>             epoll   { New }                                                 ;
	//
	if (g_writable) { epoll.add_read( _g_server_fd , EventKind::Master ) ; trace("read_master",_g_server_fd) ; } // if read-only, we do not expect additional connections
	/**/            { epoll.add_sig ( SIGHUP       , EventKind::Int    ) ; trace("read_hup"                ) ; }
	/**/            { epoll.add_sig ( SIGINT       , EventKind::Int    ) ; trace("read_int"                ) ; }
	if ( +_g_watch_fd && ::inotify_add_watch( _g_watch_fd , ServerMrkr , IN_DELETE_SELF | IN_MOVE_SELF | IN_MODIFY )>=0 ) {
		trace("read_watch",_g_watch_fd) ;
		epoll.add_read( _g_watch_fd , EventKind::Watch ) ; // if server marker is touched by user, we do as we received a ^C
	}
	//
	if (!_g_is_daemon) {
		in_tab[in_fd] ;
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
						SWEAR(cnt==sizeof(event),cnt) ;
					}
					for( Req r : Req::s_reqs_by_start ) {
						trace("all_zombie",r) ;
						r.zombie(true) ;
					}
					//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					g_engine_queue.emplace_urgent(GlobalProc::Int) ;
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				} break ;
				case EventKind::Slave :
				case EventKind::Std   : {
					ReqRpcReq rrr ;
					try         { if (!in_tab.at(fd).first.receive_step(fd,rrr)) continue ; }
					catch (...) { rrr.proc = ReqProc::None ;                                }
					Fd ofd = kind==EventKind::Std ? out_fd : fd ;
					trace("req",rrr) ;
					switch (rrr.proc) {
						case ReqProc::Make : {
							SWEAR(g_writable) ;
							Req r ;
							try {
								r = {New} ;
							} catch (::string const& e) {
								audit( ofd , rrr.options , Color::None , e , true/*as_is*/ ) ;
								OMsgBuf().send( ofd , ReqRpcReply(ReqRpcReplyProc::Status,false/*ok*/) ) ;
								if (ofd!=fd) ::close   (ofd        ) ;
								else         ::shutdown(ofd,SHUT_WR) ;
								break ;
							}
							r.zombie(false) ;
							in_tab.at(fd).second = r ;
							//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
							g_engine_queue.emplace( rrr.proc , r , fd , ofd , rrr.files , rrr.options ) ;
							//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
							trace("make",r) ;
						} break ;
						case ReqProc::Debug  :             // PER_CMD : handle request coming from receiving thread, just add your Proc here if the request is answered immediately
						case ReqProc::Forget :
						case ReqProc::Mark   :
							SWEAR(g_writable) ;
						[[fallthrough]] ;
						case ReqProc::Show :
							epoll.del(false/*write*/,fd) ; trace("del_fd",rrr.proc,fd) ;  // must precede close(fd) which may occur as soon as we push to g_engine_queue
							in_tab.erase(fd) ;
							//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
							g_engine_queue.emplace( rrr.proc , fd , ofd , rrr.files , rrr.options ) ;
							//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
						break ;
						case ReqProc::Kill :
						case ReqProc::None : {
							epoll.del(false/*write*/,fd) ; trace("stop_fd",rrr.proc,fd) ; // must precede close(fd) which may occur as soon as we push to g_engine_queue
							auto it=in_tab.find(fd) ;
							Req r = it->second.second ;
							trace("eof",fd) ;
							if (+r) { trace("zombie",r) ; r.zombie(true) ; }              // make req zombie immediately to optimize reaction time
							//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
							g_engine_queue.emplace_urgent( rrr.proc , r , fd , ofd ) ;    // this will close ofd when done writing to it
							//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
							in_tab.erase(it) ;
						} break ;
					DF}
				} break ;
			DF}
		}
		//
		if ( !_g_is_daemon && !in_tab ) break ;                                           // check end of loop after processing slave events and before master events
		//
		if (new_fd) {
			Fd slave_fd = _g_server_fd.accept().detach() ;
			in_tab[slave_fd] ;                                                            // allocate entry
			epoll.add_read(slave_fd,EventKind::Slave) ; trace("new_req",slave_fd) ;
			_report_server(slave_fd,true/*running*/) ;
		}
	}
Done :
	_g_done = true ;
	g_engine_queue.emplace( GlobalProc::Wakeup ) ;                                        // ensure engine loop sees we are done
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
		::pair<bool/*popped*/,EngineClosure> popped_closure =
			refresh_stats ? ::pair( true/*popped*/ , g_engine_queue.pop    (            ) )
			:                                        g_engine_queue.pop_for(StatsRefresh)
		;
		if (!popped_closure.first) goto Retry ;
		EngineClosure& closure = popped_closure.second ;
		switch (closure.kind()) {
			case EngineClosureKind::Global : {
				switch (closure.ecg().proc) {
					case GlobalProc::Int :
						trace("int") ;
						//       vvvvvvvvvvvv
						Backend::s_kill_all() ;
						//       ^^^^^^^^^^^^
						return true ;
					case GlobalProc::Wakeup :
						trace("wakeup") ;
					break ;
				DF}
			} break ;
			case EngineClosureKind::Req : {
				EngineClosureReq& ecr           = closure.ecr()             ;
				Req               req           = ecr.req                   ;
				::string const&   startup_dir_s = ecr.options.startup_dir_s ;
				switch (ecr.proc) {
					case ReqProc::Debug  : // PER_CMD : handle request coming from receiving thread, just add your Proc here if the request is answered immediately
					case ReqProc::Forget :
					case ReqProc::Mark   :
					case ReqProc::Show   : {
						trace(ecr) ;
						bool ok = true/*garbage*/ ;
						if ( !ecr.options.flags[ReqFlag::Quiet] && +startup_dir_s )
							audit( ecr.out_fd , ecr.options , Color::Note , "startup dir : "+no_slash(startup_dir_s) , true/*as_is*/ ) ;
						try                        { ok = g_cmd_tab[+ecr.proc](ecr) ;                                  }
						catch (::string  const& e) { ok = false ; if (+e) audit(ecr.out_fd,ecr.options,Color::Err,e) ; }
						OMsgBuf().send( ecr.out_fd , ReqRpcReply(ReqRpcReplyProc::Status,ok) ) ;
						if (ecr.out_fd!=ecr.in_fd) ecr.out_fd.close() ;                                       // close out_fd before in_fd as closing clears out_fd, defeating the equality test
						/**/                       ecr.in_fd .close() ;
					} break ;
					// Make, Kill and Close management :
					// there is exactly one Kill and one Close and one Make for each with only one guarantee : Close comes after Make
					// there is one exception : if already killed when Make is seen, the Req is not made and Make executes as if immediately followed by Close
					// read  side is closed upon Kill  (cannot be upon Close as epoll.del must be called before close)
					// write side is closed upon Close (cannot be upon Kill  as this may trigger lmake command termination, which, in turn, will trigger eof on the read side
					case ReqProc::Make : {
						bool allocated = false ;
						if (req.zombie()) {                                                                   // if already zombie, dont make req
							trace("already_killed",req) ;
							goto NoMake ;
						}
						::string msg ;
						try {
							Makefiles::dynamic_refresh( msg , startup_dir_s ) ;
							if (+msg) audit_err( ecr.out_fd , ecr.options , msg ) ;
							trace("new_req",req) ;
							req.alloc() ; allocated = true ;
							//vvvvvvvvvvv
							req.make(ecr) ;
							//^^^^^^^^^^^
							_g_seen_make = true ;
						} catch(::string const& e) {
							if (allocated) req.dealloc() ;
							if (+msg) audit_err   ( ecr.out_fd , ecr.options , msg            ) ;
							/**/      audit_err   ( ecr.out_fd , ecr.options , Color::Err , e ) ;
							/**/      audit_status( ecr.out_fd , ecr.options , false/*ok*/    ) ;
							trace("cannot_refresh",req) ;
							goto NoMake ;
						}
						if (!ecr.as_job()) _record_targets(req->job) ;
						SWEAR( +ecr.in_fd && +ecr.out_fd , ecr.in_fd , ecr.out_fd ) ;                         // in_fd and out_fd are used as marker for killed and closed respectively
						fd_tab[req] = { .in=ecr.in_fd , .out=ecr.out_fd } ;
					} break ;
					NoMake :
						if (ecr.in_fd!=ecr.out_fd) ::close   (ecr.out_fd        ) ;                           // do as if immediate Close
						else                       ::shutdown(ecr.out_fd,SHUT_WR) ;                           // .
					break ;
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
						auto     it  = fd_tab.find(req) ; if (it==fd_tab.end()) { trace("kill_before_make",ecr) ; break ; }
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
				DF}
			} break ;
			case EngineClosureKind::Job : {
				EngineClosureJob& ecj = closure.ecj() ;
				JobExec         & je  = ecj.job_exec  ;
				trace("job",ecj.proc(),je) ;
				Req::s_new_etas() ;                                                                           // regularly adjust queued job priorities if necessary
				switch (ecj.proc()) {
					//                             vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					case JobRpcProc::Start       : je.started     ( ecj.start().report , ecj.start().report_unlnks , ecj.start().txts ) ; break ;
					case JobRpcProc::ReportStart : je.report_start(                                                                   ) ; break ;
					case JobRpcProc::GiveUp      : je.give_up     ( ecj.give_up().req , ecj.give_up().report                          ) ; break ;
					case JobRpcProc::End         : je.end         ( ::move(ecj.end())                                                 ) ; break ;
					//                             ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				DF}
			} break ;
			case EngineClosureKind::JobMngt : {
				EngineClosureJobMngt& ecjm = closure.ecjm() ;
				JobExec             & je   = ecjm.job_exec  ;
				trace("job_mngt",ecjm.proc,je) ;
				switch (ecjm.proc) {
					//                          vvvvvvvvvvvvvvvvvvvvv
					case JobMngtProc::LiveOut : je.live_out(ecjm.txt) ; break ;
					//                          ^^^^^^^^^^^^^^^^^^^^^
					case JobMngtProc::ChkDeps    :
					case JobMngtProc::DepVerbose : {
						JobMngtRpcReply jmrr = je.job_analysis(ecjm.proc,ecjm.deps) ;
						jmrr.fd = ecjm.fd ;                                                                   // seq_id will be filled in by send_reply
						//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
						Backends::send_reply( +je , ::move(jmrr) ) ;
						//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					} break ;
				DF}
			} break ;
		DF}
	}
	trace("done") ;
	return false ;
}

int main( int argc , char** argv ) {
	Trace::s_backup_trace = true ;
	g_writable = !app_init(true/*read_only_ok*/,Maybe/*chk_version*/) ;                        // server is always launched at root
	if (Record::s_is_simple(g_repo_root_s->c_str()))
		exit(Rc::Usage,"cannot use lmake inside system directory "+no_slash(*g_repo_root_s)) ; // all local files would be seen as simple, defeating autodep
	_chk_os() ;
	Py::init(*g_lmake_root_s) ;
	AutodepEnv ade ;
	ade.repo_root_s         = *g_repo_root_s ;
	Record::s_static_report = true           ;
	Record::s_autodep_env(ade) ;
	if (+*g_startup_dir_s) {
		g_startup_dir_s->pop_back() ;
		FAIL("lmakeserver must be started from repo root, not from ",*g_startup_dir_s) ;
	}
	//
	bool refresh_ = true       ;
	Fd   in_fd    = Fd::Stdin  ;
	Fd   out_fd   = Fd::Stdout ;
	for( int i : iota(1,argc) ) {
		if (argv[i][0]!='-') goto Bad ;
		switch (argv[i][1]) {
			case 'c' : g_startup_dir_s = new ::string(argv[i]+2)     ;                               break ;
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
	if (g_startup_dir_s) SWEAR( is_dirname(*g_startup_dir_s) , *g_startup_dir_s ) ;
	else                 g_startup_dir_s = new ::string ;
	//
	block_sigs({SIGCHLD,SIGHUP,SIGINT,SIGPIPE}) ;                                            //     SIGCHLD,SIGHUP,SIGINT : to capture it using signalfd ...
	Trace trace("main",getpid(),*g_lmake_root_s,*g_repo_root_s) ;                            // ... SIGPIPE               : to generate error on write rather than a signal when reading end is dead ...
	for( int i : iota(argc) ) trace("arg",i,argv[i]) ;                                       // ... must be done before any thread is launched so that all threads block the signal
	mk_dir_s(PrivateAdminDirS) ;
	//             vvvvvvvvvvvvvvv
	bool crashed = _start_server() ;
	//             ^^^^^^^^^^^^^^^
	if (!_g_is_daemon     ) _report_server(out_fd,_g_server_running/*server_running*/) ;     // inform lmake we did not start
	if (!_g_server_running) return 0 ;
	::string msg ; //!          vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	try                       { Makefiles::refresh( msg , crashed , refresh_ ) ; if (+msg) Fd::Stderr.write(ensure_nl(msg)) ;                      }
	catch (::string const& e) { /*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/   if (+msg) Fd::Stderr.write(ensure_nl(msg)) ; exit(Rc::Format,e) ; }
	if (!_g_is_daemon) ::setpgid(0,0) ;                                                      // once we have reported we have started, lmake will send us a message to kill us
	//
	for( AncillaryTag tag : iota(All<AncillaryTag>) ) dir_guard(Job().ancillary_file(tag)) ;
	mk_dir_s(PrivateAdminDirS+"tmp/"s         ,true/*unlnk_ok*/) ;                           // prepare job execution so no dir_guard is necessary for each job
	mk_dir_s(PrivateAdminDirS+"fast_reports/"s,true/*unlnk_ok*/) ;                           // .
	//
	Trace::s_channels = g_config->trace.channels ;
	Trace::s_sz       = g_config->trace.sz       ;
	if (g_writable) Trace::s_new_trace_file( g_config->local_admin_dir_s+"trace/"+*g_exe_name ) ;
	Codec::Closure::s_init() ;
	Job           ::s_init() ;
	//
	static ::jthread reqs_thread { _reqs_thread_func , in_fd , out_fd } ;
	//
	//                 vvvvvvvvvvvvvv
	bool interrupted = _engine_loop() ;
	//                 ^^^^^^^^^^^^^^
	if (g_writable) {
		try                       { unlnk_inside_s(PrivateAdminDirS+"tmp/"s,false/*abs_ok*/,true/*force*/,true/*ignore_errs*/) ; } // cleanup
		catch (::string const& e) { exit(Rc::System,e) ;                                                                         }
		//
		if (_g_seen_make) AcFd(PrivateAdminDirS+"kpi"s,Fd::Write).write(g_kpi.pretty_str()) ;
	}
	//
	trace("done",STR(interrupted),New) ;
	return interrupted ;
}
