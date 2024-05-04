// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh" // must be first to include Python.h first

#include <sys/inotify.h>

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

static ServerSockFd   _g_server_fd      ;
static Fd             _g_int_fd         ;          // watch interrupts (^C and hang up)
static Fd             _g_watch_fd       ;          // watch LMAKE/server
static bool           _g_is_daemon      = true   ;
static ::atomic<bool> _g_done           = false  ;
static bool           _g_server_running = false  ;
static ::string       _g_host           = host() ;

static ::pair_s<int> _get_mrkr_host_pid() {
	::ifstream server_mrkr_stream { ServerMrkr } ;
	::string   service            ;
	::string   server_pid         ;
	if (!server_mrkr_stream                      ) { return { {}                      , 0                              } ; }
	if (!::getline(server_mrkr_stream,service   )) { return { {}                      , 0                              } ; }
	if (!::getline(server_mrkr_stream,server_pid)) { return { {}                      , 0                              } ; }
	try                                            { return { SockFd::s_host(service) , from_string<pid_t>(server_pid) } ; }
	catch (::string const&)                        { return { {}                      , 0                              } ; }
}

void server_cleanup() {
	Trace trace("server_cleanup",STR(_g_server_running)) ;
	if (!_g_server_running) return ;                       // not running, nothing to clean
	pid_t           pid  = getpid()             ;
	::pair_s<pid_t> mrkr = _get_mrkr_host_pid() ;
	trace("pid",mrkr,pid) ;
	if (mrkr!=::pair(_g_host,pid)) return ;                // not our file, dont touch it
	unlnk(ServerMrkr) ;
	trace("cleaned") ;
}

void report_server( Fd fd , bool running ) {
	Trace trace("report_server",STR(running)) ;
	int cnt = ::write( fd , &running , sizeof(bool) ) ;
	if (cnt!=sizeof(bool)) trace("no_report") ;         // client is dead
}

bool/*crashed*/ start_server(bool start) {
	bool  crashed = false    ;
	pid_t pid     = getpid() ;
	Trace trace("start_server",_g_host,pid,STR(start)) ;
	dir_guard(ServerMrkr) ;
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
	if (start) {
		_g_server_fd.listen() ;
		::string tmp = ::to_string(ServerMrkr,'.',_g_host,'.',pid) ;
		OFStream(tmp)
			<< _g_server_fd.service() << '\n'
			<< getpid()               << '\n'
		;
		//vvvvvvvvvvvvvvvvvvvvvv
		::atexit(server_cleanup) ;
		//^^^^^^^^^^^^^^^^^^^^^^
		_g_server_running = true ;                 // while we link, pretend we run so cleanup can be done if necessary
		fence() ;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		_g_server_running = ::link( tmp.c_str() , ServerMrkr )==0 ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		_g_watch_fd = ::inotify_init1(O_CLOEXEC) ; // start watching file as soon as possible (ideally would be before)
		unlnk(tmp) ;
		trace("started",STR(crashed),STR(_g_is_daemon),STR(_g_server_running)) ;
	} else {
		_g_server_running = true ;
	}
	return crashed ;
}

void record_targets(Job job) {
	::string   targets_file  = AdminDir+"/targets"s ;
	::vector_s known_targets ;
	{	::ifstream targets_stream { targets_file } ;
		::string   target         ;
		while (::getline(targets_stream,target)) known_targets.push_back(target) ;
	}
	for( Node t : job->deps ) {
		::string tn = t->name() ;
		for( ::string& ktn : known_targets ) if (ktn==tn) ktn.clear() ;
		known_targets.push_back(tn) ;
	}
	{	OFStream targets_stream { dir_guard(targets_file) } ;
		for( ::string tn : known_targets ) if (+tn) targets_stream << tn << '\n' ;
	}
}

void reqs_thread_func( ::stop_token stop , Fd in_fd , Fd out_fd ) {
	t_thread_key = 'Q' ;
	Trace trace("reqs_thread_func",STR(_g_is_daemon)) ;
	//
	::stop_callback              stop_cb  { stop , [&](){ trace("stop") ; kill_self(SIGINT) ; } } ;                         // transform request_stop into an event we wait for
	::umap<Fd,pair<IMsgBuf,Req>> in_tab   ;
	Epoll                        epoll    { New }                                                 ;
	//
	epoll.add_read( _g_server_fd , EventKind::Master ) ; trace("read_master",_g_server_fd) ;
	epoll.add_read( _g_int_fd    , EventKind::Int    ) ; trace("read_int"   ,_g_int_fd   ) ;
	//
	if ( +_g_watch_fd && ::inotify_add_watch( _g_watch_fd , ServerMrkr , IN_DELETE_SELF | IN_MOVE_SELF | IN_MODIFY )>=0 ) {
		epoll.add_read( _g_watch_fd , EventKind::Watch ) ; trace("read_watch",_g_watch_fd) ;                                // if server marker is touched by user, we do as we received a ^C
	}
	//
	if (!_g_is_daemon) {
		in_tab[in_fd] ;
		epoll.add_read(in_fd,EventKind::Std) ; trace("read_std",in_fd) ;
	}
	//
	for(;;) {
		::vector<Epoll::Event> events  = epoll.wait() ;
		bool                   new_fd  = false        ;
		for( Epoll::Event event : events ) {
			EventKind kind = event.data<EventKind>() ;
			Fd        fd   = event.fd()              ;
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
					switch (kind) {
						case EventKind::Int   : { struct signalfd_siginfo event ; ssize_t cnt = ::read(_g_int_fd  ,&event,sizeof(event)) ; SWEAR( cnt==sizeof(event) , cnt ) ; } break ;
						case EventKind::Watch : { struct inotify_event    event ; ssize_t cnt = ::read(_g_watch_fd,&event,sizeof(event)) ; SWEAR( cnt==sizeof(event) , cnt ) ; } break ;
					DF}
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
						case ReqProc::Make   : {
							Req r{New} ;
							r.zombie(false) ;
							in_tab.at(fd).second = r ;
							//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
							g_engine_queue.emplace( rrr.proc , r , fd , ofd , rrr.files , rrr.options ) ;
							//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
							trace("make",r) ;
						} break ;
						case ReqProc::Debug  : // PER_CMD : handle request coming from receiving thread, just add your Proc here if the request is answered immediately
						case ReqProc::Forget :
						case ReqProc::Mark   :
						case ReqProc::Show   :
							epoll.del(fd) ; trace("del_fd",rrr.proc,fd) ;                   // must precede close(fd) which may occur as soon as we push to g_engine_queue
							in_tab.erase(fd) ;
							//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
							g_engine_queue.emplace( rrr.proc , fd , ofd , rrr.files , rrr.options ) ;
							//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
						break ;
						case ReqProc::Kill :
						case ReqProc::None : {
							epoll.del(fd) ; trace("stop_fd",rrr.proc,fd) ;                  // must precede close(fd) which may occur as soon as we push to g_engine_queue
							auto it=in_tab.find(fd) ;
							Req r = it->second.second ;
							trace("eof",fd) ;
							if (+r) { trace("zombie",r) ; r.zombie(true) ; }                // make req zombie immediately to optimize reaction time
							//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
							g_engine_queue.emplace_urgent( ReqProc::Kill , r , fd , ofd ) ; // this will close ofd when done writing to it
							//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
							in_tab.erase(it) ;
						} break ;
					DF}
				} break ;
			DF}
		}
		//
		if ( !_g_is_daemon && !in_tab ) break ;                                             // check end of loop after processing slave events and before master events
		//
		if (new_fd) {
			Fd slave_fd = Fd(_g_server_fd.accept()) ;
			in_tab[slave_fd] ;                                                              // allocate entry
			epoll.add_read(slave_fd,EventKind::Slave) ; trace("new_req",slave_fd) ;
			report_server(slave_fd,true/*running*/) ;
		}
	}
Done :
	_g_done = true ;
	g_engine_queue.emplace( GlobalProc::Wakeup ) ;                                          // ensure engine loop sees we are done
	trace("done") ;
}

bool/*interrupted*/ engine_loop() {
	struct FdEntry {
		Fd   in     ;
		Fd   out    ;
		bool killed = false ;
	} ;
	Trace trace("engine_loop") ;
	::umap<Req,FdEntry> fd_tab          ;
	Pdate               next_stats_date = New ;
	for (;;) {
		bool empty = !g_engine_queue ;
		if (empty) {                                                               // we are about to block, do some book-keeping
			trace("wait") ;
			//vvvvvvvvvvvvvvvvv
			Backend::s_launch() ;                                                  // we are going to wait, tell backend as it may have retained jobs to process them with as mauuch info as possible
			//^^^^^^^^^^^^^^^^^
		}
		if ( Pdate now=New ; empty || now>next_stats_date ) {
			for( auto const& [r,_] : fd_tab ) if (+r->audit_fd) r->audit_stats() ; // refresh title
			next_stats_date = now+Delay(1.) ;
		}
		if ( empty && _g_done && !Req::s_n_reqs() && !g_engine_queue ) break ;
		EngineClosure closure = g_engine_queue.pop() ;
		switch (closure.kind) {
			case EngineClosureKind::Global : {
				switch (closure.ecg.proc) {
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
				EngineClosureReq& ecr           = closure.ecr               ;
				Req               req           = ecr.req                   ;
				::string const&   startup_dir_s = ecr.options.startup_dir_s ;
				switch (ecr.proc) {
					case ReqProc::Debug  :                // PER_CMD : handle request coming from receiving thread, just add your Proc here if the request is answered immediately
					case ReqProc::Forget :
					case ReqProc::Mark   :
					case ReqProc::Show   : {
						trace(ecr) ;
						bool ok = true/*garbage*/ ;
						if ( !ecr.options.flags[ReqFlag::Quiet] && +startup_dir_s )
							audit( ecr.out_fd , ecr.options , Color::Note , "startup dir : "+startup_dir_s.substr(0,startup_dir_s.size()-1) , true/*as_is*/ ) ;
						try                        { ok = g_cmd_tab[+ecr.proc](ecr) ;                                  }
						catch (::string  const& e) { ok = false ; if (+e) audit(ecr.out_fd,ecr.options,Color::Err,e) ; }
						OMsgBuf().send( ecr.out_fd , ReqRpcReply(ReqRpcReplyProc::Status,ok) ) ;
						/**/                       ecr.in_fd .close() ;
						if (ecr.out_fd!=ecr.in_fd) ecr.out_fd.close() ;
					} break ;
					// Make, Kill and Close management :
					// there is exactly one Kill and one Close and one Make for each with only one guarantee : Close comes after Make
					// there is one exception : if already killed when Make is seen, the Req is not made and Make executes as if immediately followed by Close
					// read  side is closed upon Kill  (cannot be upon Close as epoll.del must be called before close)
					// write side is closed upon Close (cannot be upon Kill  as this may trigger lmake command termination, which, in turn, will trigger eof on the read side
					case ReqProc::Make :
						if (req.zombie())                 // if already zombie, dont make req
							trace("already_killed",req) ;
						else
							try {
								::string msg = Makefiles::dynamic_refresh(startup_dir_s) ;
								if (+msg) audit( ecr.out_fd , ecr.options , Color::Note , msg ) ;
								trace("new_req",req) ;
								//vvvvvvvvvvv
								req.make(ecr) ;
								//^^^^^^^^^^^
								if (!ecr.as_job()) record_targets(req->job) ;
								fd_tab[req] = { .in=ecr.in_fd , .out=ecr.out_fd } ;
								break ;
							} catch(::string const& e) {
								audit       ( ecr.out_fd , ecr.options , Color::Err , e ) ;
								audit_status( ecr.out_fd , ecr.options , false/*ok*/    ) ;
								trace("no_make",req) ;
							}
						// cannot make, process as if followed by Close
						req.close() ;
						/**/                       ecr.in_fd .close() ;
						if (ecr.out_fd!=ecr.in_fd) ecr.out_fd.close() ;
					break ;
					case ReqProc::Close : {
						auto it = fd_tab.find(req) ;
						trace("close_req",ecr,it->second.in,it->second.out,STR(it->second.killed)) ;
						//vvvvvvvvv
						req.close() ;
						//^^^^^^^^^
						if      (it->second.killed            ) req.dealloc() ;                      // dealloc when req can be reused
						if      (it->second.in!=it->second.out) ::close   (it->second.out        ) ;
						else if (it->second.killed            ) ::close   (it->second.out        ) ; // we are after  Kill, finalize close of file descriptor
						else                                    ::shutdown(it->second.out,SHUT_WR) ; // we are before Kill, shutdown until final close upon Close
						fd_tab.erase(it) ;
					} break ;
					case ReqProc::Kill : {
						auto it         = fd_tab.find(req)                               ;
						bool req_active = it!=fd_tab.end() && it->second.out==ecr.out_fd ; // out_fd is held until now, and if it does not coincide with it->second, req id was reused for a new Req
						//
						if (it==fd_tab.end()) trace("kill_req",ecr                                                    ) ;
						else                  trace("kill_req",ecr,it->second.in,it->second.out,STR(it->second.killed)) ;
						//                                 vvvvvvvvvv
						if ( +req && +*req && req_active ) req.kill() ;
						//                                 ^^^^^^^^^^
						if      (req_active           ) it->second.killed = true ;
						else                            req.dealloc() ;                    // dealloc when req can be reused
						if      (ecr.in_fd!=ecr.out_fd) ::close   (ecr.in_fd        ) ;
						else if (!req_active          ) ::close   (ecr.in_fd        ) ;    // we are after  Close, finalize close of file descriptor
						else                            ::shutdown(ecr.in_fd,SHUT_RD) ;    // we are before Close, shutdown until final close upon Close
					} break ;
				DF}
			} break ;
			case EngineClosureKind::Job : {
				EngineClosureJob& ecj = closure.ecj  ;
				JobExec         & je  = ecj.job_exec ;
				trace("job",ecj.proc,je) ;
				switch (ecj.proc) {
					//                          vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					case JobProc::Start       : je.started     (ecj.report,ecj.report_unlnks,ecj.txt,ecj.msg) ; break ;
					case JobProc::ReportStart : je.report_start(                                            ) ; break ;
					case JobProc::GiveUp      : je.give_up     (ecj.req , ecj.report                        ) ; break ;
					case JobProc::End         : je.end         (ecj.rsrcs,ecj.digest,::move(ecj.msg)        ) ; break ;
					//                          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				DF}
			} break ;
			case EngineClosureKind::JobMngt : {
				EngineClosureJobMngt& ecjm = closure.ecjm ;
				JobExec             & je   = ecjm.job_exec    ;
				trace("job_mngt",ecjm.proc,je) ;
				switch (ecjm.proc) {
					//                          vvvvvvvvvvvvvvvvvvvvv
					case JobMngtProc::LiveOut : je.live_out(ecjm.txt) ; break ;
					//                          ^^^^^^^^^^^^^^^^^^^^^
					case JobMngtProc::ChkDeps    :
					case JobMngtProc::DepVerbose : {
						::vector<Dep> deps ; deps.reserve(ecjm.deps.size()) ;
						for( auto const& [dn,dd] : ecjm.deps ) deps.emplace_back(Node(dn),dd) ;
						JobMngtRpcReply jmrr = je.job_info(ecjm.proc,deps) ;
						jmrr.fd = ecjm.fd ;                                                // seq_id will be filled in by send_reply
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
	app_init(Maybe/*chk_version*/) ;                                                    // server is always launched at root
	Py::init( *g_lmake_dir , true/*multi-thread*/ ) ;
	AutodepEnv ade ;
	ade.root_dir = *g_root_dir ;
	Record::s_static_report = true ;
	Record::s_autodep_env(ade) ;
	if (+*g_startup_dir_s) {
		g_startup_dir_s->pop_back() ;
		FAIL("lmakeserver must be started from repo root, not from ",*g_startup_dir_s) ;
	}
	//
	bool refresh_ = true       ;
	Fd   in_fd    = Fd::Stdin  ;
	Fd   out_fd   = Fd::Stdout ;
	for( int i=1 ; i<argc ; i++ ) {
		if (argv[i][0]!='-') goto Bad ;
		switch (argv[i][1]) {
			case 'c' : g_startup_dir_s = new ::string(argv[i]+2)     ;                               break ;
			case 'd' : _g_is_daemon    = false                       ; if (argv[i][2]!=0) goto Bad ; break ;
			case 'i' : in_fd           = from_string<int>(argv[i]+2) ;                               break ;
			case 'o' : out_fd          = from_string<int>(argv[i]+2) ;                               break ;
			case 'r' : refresh_        = false                       ; if (argv[i][2]!=0) goto Bad ; break ;
			case '-' :                                                 if (argv[i][2]!=0) goto Bad ; break ;
			default : exit(Rc::Usage,"unrecognized option : ",argv[i]) ;
		}
		continue ;
	Bad :
		exit(Rc::Usage,"unrecognized argument : ",argv[i],"\nsyntax : lmakeserver [-cstartup_dir_s] [-d/*no_daemon*/] [-r/*no makefile refresh*/]") ;
	}
	if (g_startup_dir_s) SWEAR( !*g_startup_dir_s || g_startup_dir_s->back()=='/' ) ;
	else                 g_startup_dir_s = new ::string ;
	//
	_g_int_fd = open_sig_fd({SIGINT,SIGHUP}) ;                                          // must be done before app_init so that all threads block the signal
	set_sig({SIGPIPE},true/*block*/) ;
	//          vvvvvvvvvvvvvvv
	Persistent::writable = true ;
	Codec     ::writable = true ;
	//          ^^^^^^^^^^^^^^^
	Trace trace("main",getpid(),*g_lmake_dir,*g_root_dir) ;
	for( int i=0 ; i<argc ; i++ ) trace("arg",i,argv[i]) ;
	//             vvvvvvvvvvvvvvvvvvvvvvvvvvv
	bool crashed = start_server(true/*start*/) ;
	//             ^^^^^^^^^^^^^^^^^^^^^^^^^^^
	if (!_g_is_daemon     ) report_server(out_fd,_g_server_running/*server_running*/) ; // inform lmake we did not start
	if (!_g_server_running) return 0 ;
	try {
		//                        vvvvvvvvvvvvvvvvvvvvvvvvv
		::string msg = Makefiles::refresh(crashed,refresh_) ;
		//                        ^^^^^^^^^^^^^^^^^^^^^^^^^
		if (+msg  ) ::cerr << ensure_nl(msg) ;
	} catch (::string const& e) { exit(Rc::Format,e) ; }
	if (!_g_is_daemon) ::setpgid(0,0) ;                                                 // once we have reported we have started, lmake will send us a message to kill us
	//
	{	::string v ;
		Trace::s_channels = g_config.trace.channels ;
		Trace::s_sz       = g_config.trace.sz       ;
		Trace::s_new_trace_file(to_string( g_config.local_admin_dir , "/trace/" , base_name(read_lnk("/proc/self/exe")) )) ;
	}
	Codec::Closure::s_init() ;
	//
	static ::jthread reqs_thread { reqs_thread_func , in_fd , out_fd } ;
	//
	//                 vvvvvvvvvvvvv
	bool interrupted = engine_loop() ;
	//                 ^^^^^^^^^^^^^
	unlnk(g_config.remote_tmp_dir,true/*dir_ok*/) ;                                     // cleanup
	//
	trace("done",STR(interrupted),Pdate(New)) ;
	return interrupted ;
}
