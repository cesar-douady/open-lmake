// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <sys/inotify.h>

#include "disk.hh"
#include "rpc_client.hh"

#include "makefiles.hh"
#include "core.hh"

using namespace Disk   ;
using namespace Engine ;
using namespace Time   ;

static ServerSockFd   _g_server_fd      ;
static bool           _g_is_daemon      = true   ;
static ::atomic<bool> _g_done           = false  ;
static bool           _g_server_running = false  ;
static ::string       _g_server_mrkr    ;
static ::string       _g_host           = host() ;
static RealPath       _g_real_path      ;

static ::pair_s<int> _get_mrkr_host_pid() {
	::ifstream server_mrkr_stream { _g_server_mrkr } ;
	::string   service            ;
	::string   server_pid         ;
	if (!server_mrkr_stream                      ) { return { {}                      , 0                             } ; }
	if (!::getline(server_mrkr_stream,service   )) { return { {}                      , 0                             } ; }
	if (!::getline(server_mrkr_stream,server_pid)) { return { {}                      , 0                             } ; }
	try                                            { return { SockFd::s_host(service) , from_chars<pid_t>(server_pid) } ; }
	catch (::string const&)                        { return { {}                      , 0                             } ; }
}

void server_cleanup() {
	Trace trace("server_cleanup",STR(_g_server_running),_g_server_mrkr) ;
	if (!_g_server_running) return ;                                           // not running, nothing to clean
	pid_t           pid  = getpid()             ;
	::pair_s<pid_t> mrkr = _get_mrkr_host_pid() ;
	trace("pid",mrkr,pid) ;
	if (mrkr!=::pair_s(_g_host,pid)) return ;                                  // not our file, dont touch it
	unlink(_g_server_mrkr) ;
	trace("cleaned") ;
}

void report_server( Fd fd , bool running ) {
	Trace trace("report_server",STR(running)) ;
	int cnt = ::write( fd , &running , sizeof(bool) ) ;
	if (cnt!=sizeof(bool)) trace("no_report") ;                                // client is dead
}

bool/*crashed*/ start_server() {
	bool  crashed = false    ;
	pid_t pid     = getpid() ;
	Trace trace("start_server",_g_server_mrkr,_g_host,pid) ;
	dir_guard(_g_server_mrkr) ;
	::pair_s<pid_t> mrkr = _get_mrkr_host_pid() ;
	if ( +mrkr.first && mrkr.first!=_g_host ) {
		trace("already_existing_elsewhere",mrkr) ;
		return false/*unused*/ ;                                               // if server is running on another host, we cannot qualify with a kill(pid,0), be pessimistic
	}
	if (mrkr.second) {
		if (kill_process(mrkr.second,0)) {                                     // another server exists
			trace("already_existing",mrkr) ;
			return false/*unused*/ ;
		}
		unlink(_g_server_mrkr) ;                                               // before doing anything, we create the marker, which is unlinked at the end, so it is a marker of a crash
		crashed = true ;
		trace("vanished",mrkr) ;
	}
	_g_server_fd.listen() ;
	::string tmp = ::to_string(_g_server_mrkr,'.',_g_host,'.',pid) ;
	OFStream(tmp)
		<< _g_server_fd.service() << '\n'
		<< getpid()               << '\n'
	;
	//vvvvvvvvvvvvvvvvvvvv
	atexit(server_cleanup) ;
	//^^^^^^^^^^^^^^^^^^^^
	_g_server_running = true ;                                                 // while we link, pretend we run so cleanup can be done if necessary
	fence() ;
	//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	_g_server_running = ::link(tmp.c_str(),_g_server_mrkr.c_str())==0 ;
	//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	unlink(tmp) ;
	trace("started",STR(crashed),STR(_g_is_daemon),STR(_g_server_running)) ;
	return crashed ;
}

void record_targets(Job job) {
	::string targets_file    = AdminDir+"/targets"s ;
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
	{	::ofstream targets_stream { dir_guard(targets_file) } ;
		for( ::string tn : known_targets ) if (+tn) targets_stream << tn << '\n' ;
	}
}

void reqs_thread_func( ::stop_token stop , Fd int_fd ) {
	t_thread_key = 'Q' ;
	Trace trace("reqs_thread_func",STR(_g_is_daemon)) ;
	//
	::stop_callback    stop_cb        { stop , [](){ kill_self(SIGINT) ; } } ; // transform request_stop into an event we wait for
	::umap<Fd,IMsgBuf> in_tab         ;
	Epoll              epoll          { New }                                ;
	Fd                 server_stop_fd = ::inotify_init1(O_CLOEXEC)           ;
	//
	inotify_add_watch( server_stop_fd , _g_server_mrkr.c_str() , IN_DELETE_SELF | IN_MOVE_SELF | IN_MODIFY ) ;
	//
	epoll.add_read( _g_server_fd   , EventKind::Master ) ;
	epoll.add_read( int_fd         , EventKind::Int    ) ;
	epoll.add_read( server_stop_fd , EventKind::Int    ) ;                     // if server marker is touched by user, we do as we received a ^C
	if (!_g_is_daemon) {
		in_tab[Fd::Stdin] ;
		epoll.add_read(Fd::Stdin,EventKind::Std) ;
	}
	//
	for(;;) {
		::vector<Epoll::Event> events = epoll.wait() ;
		bool                   new_fd = false        ;
		for( Epoll::Event event : events ) {
			EventKind kind = event.data<EventKind>() ;
			Fd        fd   = event.fd()              ;
			switch (kind) {
				// it may be that in a single poll, we get the end of a previous run and a request for a new one
				// problem lies in this sequence :
				// - lmake foo
				// - touch Lmakefile.py
				// - lmake bar          => maybe we get this request in the same poll as the end of lmake foo and we would eroneously say that it cannot be processed
				// solution is to delay master events after other events and ignore them if we are done inbetween
				case EventKind::Master :
					SWEAR( !new_fd , new_fd ) ;
					new_fd = true ;
				break ;
				case EventKind::Int : {
					if (stop.stop_requested()) {
						trace("stop_requested") ;
						goto Done ;
					}
					trace("int") ;
					struct signalfd_siginfo _ ;
					ssize_t cnt = ::read(int_fd,&_,sizeof(_)) ;
					SWEAR( cnt==sizeof(_) , cnt ) ;
					//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					g_engine_queue.emplace_urgent(GlobalProc::Int) ;
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				} break ;
				case EventKind::Slave :
				case EventKind::Std   : {
					ReqRpcReq rrr ;
					try         { if (!in_tab.at(fd).receive_step(fd,rrr)) continue ; }
					catch (...) { rrr.proc = ReqProc::None ;                          }
					Fd out_fd = kind==EventKind::Std ? Fd::Stdout : fd ;
					trace("req",fd,rrr) ;
					if (rrr.proc>=ReqProc::HasArgs) {
						//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
						g_engine_queue.emplace( rrr.proc , fd , out_fd , rrr.files , rrr.options ) ;
						//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					} else {
						trace("close",fd) ;
						epoll.del(fd) ;                                              // must precede close(fd)
						//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
						g_engine_queue.emplace_urgent( ReqProc::Kill , fd , out_fd ) ; // this will close out_fd when done writing to it
						//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
						in_tab.erase(fd) ;
					}
				} break ;
				default : fail("kind=",kind,"fd=",fd) ;
			}
		}
		//
		if ( !_g_is_daemon && !in_tab ) break ;                                // check end of loop after processing slave events and before master events
		//
		if (new_fd) {
			Fd slave_fd = Fd(_g_server_fd.accept()) ;
			trace("new_req",slave_fd) ;
			in_tab[slave_fd] ;                                                 // allocate entry
			epoll.add_read(slave_fd,EventKind::Slave) ;
			report_server(slave_fd,true/*running*/) ;
		}
	}
Done :
	_g_done = true ;
	g_engine_queue.emplace( GlobalProc::Wakeup ) ;                             // ensure engine loop sees we are done
	trace("done") ;
}

bool/*interrupted*/ engine_loop() {
	Trace trace("engine_loop") ;
	::umap<Fd/*out*/,Req               > req_tab         ;                     // if !entry is true <=> it is zombie, next event (Make, Kill or Close) will erase entry
	::umap<Req,pair<Fd/*in*/,Fd/*out*/>> fd_tab          ;
	Pdate                                next_stats_date = Pdate::s_now() ;
	for (;;) {
		bool empty = !g_engine_queue ;
		if (empty) {                                                           // we are about to block, do some book-keeping
			trace("wait") ;
			//vvvvvvvvvvvvvvvvv
			Backend::s_launch() ;                                              // we are going to wait, tell backend as it may have retained jobs to process them with as mauuch info as possible
			//^^^^^^^^^^^^^^^^^
		}
		if ( Pdate now ; empty || (now=Pdate::s_now())>next_stats_date ) {
			for( auto const& [fd,r] : req_tab ) r->audit_stats() ;             // refresh title
			next_stats_date = now+Delay(1.) ;
		}
		if ( empty && _g_done && !Req::s_n_reqs() && !g_engine_queue ) break ;
		EngineClosure closure = g_engine_queue.pop() ;
		switch (closure.kind) {
			case EngineClosureKind::Global : {
				switch (closure.global_proc) {
					case GlobalProc::Int :
						trace("int") ;
						//       vvvvvvvvvvvv
						Backend::s_kill_all() ;
						//       ^^^^^^^^^^^^
						return true ;
					case GlobalProc::Wakeup :
						trace("wakeup") ;
					break ;
					default : FAIL(closure.global_proc) ;
				}
			} break ;
			case EngineClosureKind::Req : {
				EngineClosureReq& req           = closure.req               ;
				::string const&   startup_dir_s = req.options.startup_dir_s ;
				switch (req.proc) {
					case ReqProc::Debug  :                                     // PER_CMD : handle request coming from receiving thread, just add your Proc here if the request is answered immediately
					case ReqProc::Forget :
					case ReqProc::Mark   :
					case ReqProc::Show   : {
						trace(req) ;
						bool ok = true/*garbage*/ ;
						if ( !req.options.flags[ReqFlag::Quiet] && +startup_dir_s )
							audit( req.out_fd , req.options , Color::Note , "startup dir : "+startup_dir_s.substr(0,startup_dir_s.size()-1) ) ;
						try                        { ok = g_cmd_tab[+req.proc](req) ;                                  }
						catch (::string  const& e) { ok = false ; if (+e) audit(req.out_fd,req.options,Color::Err,e) ; }
						OMsgBuf().send( req.out_fd , ReqRpcReply(ok) ) ;
					} break ;
					// Make, Kill and Close management :
					// there is exactly one Kill and one Close and one Make for each with only one guarantee : Close comes after Make
					// there is one exception : if Kill comes before Make, no Req is created and we pretend it is directly closed
					// to do that, we use zombie entries when Req is closed
					case ReqProc::Make : {
						if ( auto it=req_tab.find(req.out_fd) ; it!=req_tab.end() ) {
							SWEAR(!it->second) ;                                      // if entry already exists, it was created by Kill and is deemed already closed
							req_tab.erase(it) ;
							trace("zombie_req",req) ;
						} else {
							Req r ;
							try {
								::string msg = Makefiles::dynamic_refresh(startup_dir_s) ;
								if (+msg) audit( req.out_fd , req.options , Color::Note , msg ) ;
								r = Req(req) ;
							} catch(::string const& e) {
								audit( req.out_fd , req.options , Color::Err , e ) ;
								OMsgBuf().send( req.out_fd , ReqRpcReply(false/*ok*/) ) ;
								break ;
							}
							if (!req.as_job()) record_targets(r->job) ;
							req_tab[req.out_fd] = r                      ;
							fd_tab [r         ] = {req.in_fd,req.out_fd} ;
							trace("new_req",req,r) ;
							//vvvvvv
							r.make() ;
							//^^^^^^
						}
					} break ;
					case ReqProc::Kill : {
						auto it = req_tab.find(req.out_fd) ;
						if (it==req_tab.end()) {
							trace("kill_new",req) ;
							req_tab.emplace(req.out_fd,Req()) ;                // Kill comes before Make, make an empty entry and it will die when Make finally comes
							/**/                       ::close(req.in_fd ) ;   // nothing comes after a Kill (the Make event is already received, just processed in reversed order)
							if (req.out_fd!=req.in_fd) ::close(req.out_fd) ;   // no Req will be created, there will be no output
						} else if (!it->second) {
							trace("kill_closed",req) ;
							req_tab.erase(it) ;                                // entry was closed
							::close(req.in_fd) ;                               // nothing comes after a Kill and the write side is already shutdown
						} else {
							trace("kill_req",req,it->second) ;
							//vvvvvvvvvvvvvvv
							it->second.kill() ;
							//^^^^^^^^^^^^^^^
							if (req.in_fd!=req.out_fd) ::close (req.in_fd        ) ; // nothing comes after a Kill
							else                       shutdown(req.in_fd,SHUT_RD) ; // but the write side is stil alive as Req may still have stuff to send
						}
					} break ;
					case ReqProc::Close : {
						auto                              fit = fd_tab.find(req.req)     ;
						::pair<Fd/*in*/,Fd/*out*/> const& fds = fit->second              ;
						auto                              rit = req_tab.find(fds.second) ;
						SWEAR(rit!=req_tab.end()) ;                                        // Close comes after a Make and entry exists (possibly a zombie if Kill already came)
						SWEAR(+rit->second      ) ;                                        // we need to keep Req to do book-keeping
						Req  r      = rit->second ;
						bool zombie = r->zombie   ;                            // fetch before r is closed
						if (zombie) req_tab.erase(rit) ;                       // entry was killed
						else        rit->second = {} ;                         // entry is yet to be killed
						trace("close_req",req,r,STR(zombie)) ;
						//vvvvvvv
						r.close() ;
						//^^^^^^^
						if      (zombie               ) ::close (fds.second        ) ; // kill already seen, close connection if it is a socket
						else if (fds.first!=fds.second) ::close (fds.second        ) ; // input and output are independentm nothing is sent after a close
						else                            shutdown(fds.second,SHUT_WR) ; // receive side is still alive as Kill was not received yet
						fd_tab.erase(fit) ;
					} break ;
					default : FAIL(req.proc) ;
				}
			} break ;
			case EngineClosureKind::Job : {
				EngineClosureJob& job = closure.job ;
				JobExec         & je  = job.exec    ;
				trace("job",job.proc,je) ;
				switch (job.proc) {
					//                          vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					case JobProc::Start       : je.started     (job.report,job.report_unlink,job.txt,job.backend_msg) ; break ;
					case JobProc::ReportStart : je.report_start(                                                    ) ; break ;
					case JobProc::LiveOut     : je.live_out    (job.txt                                             ) ; break ;
					case JobProc::Continue    : je.continue_   (job.req                                             ) ; break ;
					case JobProc::NotStarted  : je.not_started (                                                    ) ; break ;
					case JobProc::End         : je.end         (job.rsrcs,job.digest,job.backend_msg                ) ; break ;
					//                          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					case JobProc::ChkDeps     :
					case JobProc::DepInfos    : {
						::vector<Node> deps ; deps.reserve(job.digest.deps.size()) ;
						for( auto [dn,_] : job.digest.deps ) deps.emplace_back(dn) ;
						//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
						OMsgBuf().send( job.reply_fd , je.job_info(job.proc,deps) ) ;
						//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
						::close(job.reply_fd) ;
					} break ;
					default : FAIL(job.proc) ;
				}
			} break ;
			default : FAIL(closure.kind) ;
		}
	}
	trace("done") ;
	return false ;
}

int main( int argc , char** argv ) {
	bool refresh_ = true ;
	for( int i=1 ; i<argc ; i++ ) {
		if (argv[i][0]!='-') goto Bad ;
		switch (argv[i][1]) {
			case 'c' : g_startup_dir_s = new ::string(argv[i]+2) ;                               break ;
			case 'd' : _g_is_daemon    = false                   ; if (argv[i][2]!=0) goto Bad ; break ;
			case 'r' : refresh_        = false                   ; if (argv[i][2]!=0) goto Bad ; break ;
			case '-' :                                             if (argv[i][2]!=0) goto Bad ; break ;
			default : exit(2,"unrecognized option : ",argv[i]) ;
		}
		continue ;
	Bad :
		exit(2,"unrecognized argument : ",argv[i],"\nsyntax : lmakeserver [-cstartup_dir_s] [-d/*no_daemon*/] [-r/*no makefile refresh*/]") ;
	}
	if (g_startup_dir_s) SWEAR( !*g_startup_dir_s || g_startup_dir_s->back()=='/' ) ;
	else                 g_startup_dir_s = new ::string ;
	//
	Fd int_fd = open_sig_fd(SIGINT) ;                                          // must be done before app_init so that all threads block the signal
	//vvvvvvvvvvvvvvvvvvvvv
	g_store.writable = true ;
	//^^^^^^^^^^^^^^^^^^^^^
	Trace::s_backup_trace = true ;
	app_init(false/*search_root*/,false/*cd_root*/) ;                          // server is always launched at root
	Py::init(true/*multi-thread*/) ;
	_g_real_path.init({ .lnk_support=g_config.lnk_support , .root_dir=*g_root_dir }) ;
	_g_server_mrkr = to_string(AdminDir,'/',ServerMrkr) ;
	Trace trace("main",getpid(),*g_lmake_dir,*g_root_dir) ;
	//             vvvvvvvvvvvvvv
	bool crashed = start_server() ;
	//             ^^^^^^^^^^^^^^
	if (!_g_is_daemon     ) report_server(Fd::Stdout,_g_server_running/*server_running*/) ; // inform lmake we did not start
	if (!_g_server_running) return 0 ;
	::string msg ;
	//                     vvvvvvvvvvvvvvvvvvvvvvvvv
	try { msg = Makefiles::refresh(crashed,refresh_) ; }
	//                     ^^^^^^^^^^^^^^^^^^^^^^^^^
	catch (::string const& e) { exit(2,e) ; }
	if (+msg         ) ::cerr << ensure_nl(msg) ;
	if (!_g_is_daemon) ::setpgid(0,0)           ;                              // once we have reported we have started, lmake will send us a message to kill us
	//
	{	::string v ;
		Trace::s_channels = g_config.trace.channels ;
		Trace::s_sz       = g_config.trace.sz       ;
		Trace::s_new_trace_file(to_string( g_config.local_admin_dir , "/trace/" , base_name(read_lnk("/proc/self/exe")) )) ;
	}
	//
	static ::jthread reqs_thread{ reqs_thread_func , int_fd } ;
	//
	//                 vvvvvvvvvvvvv
	bool interrupted = engine_loop() ;
	//                 ^^^^^^^^^^^^^
	unlink(g_config.remote_tmp_dir,true/*dir_ok*/) ;                           // cleanup
	//
	trace("exit",STR(interrupted),Pdate::s_now()) ;
	return interrupted ;
}
