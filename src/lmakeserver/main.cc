// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
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

ENUM( EventKind , Master , Int , Slave , Std )

static ServerSockFd   _g_server_fd      ;
static bool           _g_is_daemon      = false ;
static ::atomic<bool> _g_done           = false ;
static bool           _g_server_running = false ;
static ::string       _g_server_mrkr    ;

static int _get_mrkr_pid() {
	::ifstream server_mrkr_stream {_g_server_mrkr} ;
	if (!server_mrkr_stream) return 0 ;
	::string server_pid ;
	getline(server_mrkr_stream,server_pid) ;                                   // server:port
	getline(server_mrkr_stream,server_pid) ;                                   // pid
	return atol(server_pid.c_str()) ;
}

void server_cleanup() {
	Trace trace("server_cleanup",STR(_g_server_running),_g_server_mrkr) ;
	if (!_g_server_running) return ;                                           // not running, nothing to clean
	int pid      = getpid()        ;
	int mrkr_pid = _get_mrkr_pid() ;
	trace("pid",mrkr_pid,pid) ;
	if (mrkr_pid!=pid) return ;                                                // not our file, dont touch it
	::unlink(_g_server_mrkr.c_str()) ;
	trace("cleaned") ;
}

void report_server( Fd fd , bool running ) {
	int cnt = ::write( fd , &running , sizeof(bool) ) ;
	if (cnt!=sizeof(bool)) fail_prod("cannot report server ",STR(running)) ;
}

bool/*crashed*/ start_server() {
	bool crashed       = false      ;
	int  pid           = getpid()   ;
	::string hostname_ = hostname() ;
	Trace trace("start_server",_g_server_mrkr,hostname_,pid) ;
	Disk::dir_guard(_g_server_mrkr) ;
	if ( int mrkr_pid = _get_mrkr_pid() ) {
		if (::kill(mrkr_pid,0)==0) {                                           // another server exists
			trace("already_existing",mrkr_pid) ;
			return false/*unused*/ ;
		}
		::unlink(_g_server_mrkr.c_str()) ;                                     // before doing anything, we create the marker, which is unlinked at the end, so it is a marker of a crash
		crashed = true ;
		trace("vanished",mrkr_pid) ;
	}
	_g_server_fd.listen() ;
	::string tmp = ::to_string(_g_server_mrkr,'.',hostname_,'.',pid) ;
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
	::unlink(tmp.c_str()) ;
	trace("started",STR(crashed),STR(_g_is_daemon),STR(_g_server_running)) ;
	return crashed ;
}

void record_targets( ::vector<Node> const& targets ) {
	::string targets_file    = AdminDir+"/targets"s ;
	::vector_s known_targets ;
	{	::ifstream targets_stream { targets_file } ;
		::string   target         ;
		while (std::getline(targets_stream,target)) {
			known_targets.push_back(target) ;
		}
	}
	for( Node t : targets ) {
		::string tn = t.name() ;
		for( ::string& ktn : known_targets ) if (ktn==tn) ktn.clear() ;
		known_targets.push_back(tn) ;
	}
	dir_guard(targets_file) ;
	{	::ofstream targets_stream { targets_file } ;
		for( ::string tn : known_targets ) if (!tn.empty()) targets_stream << tn << '\n' ;
	}
}

void reqs_thread_func( ::stop_token stop , Fd int_fd ) {
	Trace::t_key = 'R' ;
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
		DiskDate::s_refresh_now() ;                                            // we may have waited, refresh now
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
					SWEAR(!new_fd) ;
					new_fd = true ;
				break ;
				case EventKind::Int : {
					if (stop.stop_requested()) {
						trace("stop_requested") ;
						goto Done ;
					}
					trace("int") ;
					struct signalfd_siginfo _ ;
					SWEAR( ::read(int_fd,&_,sizeof(_)) == sizeof(_) ) ;
					//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					g_engine_queue.emplace(GlobalProc::Int) ;
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				} break ;
				case EventKind::Slave :
				case EventKind::Std   : {
					ReqRpcReq rrr ;
					try         { if (!in_tab.at(fd).receive_step(fd,rrr)) continue ; }
					catch (...) { rrr.proc = ReqProc::None ;                          }
					Fd out_fd = kind==EventKind::Std ? Fd::Stdout : fd ;
					trace("req",fd,rrr) ;
					switch (rrr.proc) {
						case ReqProc::Make : {
							if (!Makefiles::s_chk_makefiles()) {
								//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
								audit( out_fd , rrr.options , Color::Err , 0 , "cannot make with modified makefiles while other lmake is running\n"s ) ;
								//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
								trace("modified_makefiles") ;
								goto Bad ;
							}
						/*fall through*/
						case ReqProc::Forget :                                 // PER_CMD : handle request coming from command, just add your Proc here if the request is answered immediately
						case ReqProc::Freeze :
						case ReqProc::Show   :
							RealPath       real_path {g_config.lnk_support} ;
							::vector<Node> targets   ; targets.reserve(rrr.targets.size()) ; // typically, there is no bads
							::vector_s     bads      ;
							for( ::string const& target : rrr.targets ) {
								::string real_target = real_path.solve(target).real ;        // ignore links that lead to real path
								if (real_target.empty()) bads   .emplace_back(target     ) ;
								else                     targets.emplace_back(real_target) ;
							}
							if (bads.empty()) {
								trace("targets",targets) ;
								if (rrr.proc!=ReqProc::Make) {                 // Make requests are not answered immediately
									epoll.del(fd) ;
									in_tab.erase(fd) ;
								}
								//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
								g_engine_queue.emplace( rrr.proc , fd , out_fd , targets , rrr.options ) ;
								//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
							} else {
								trace("bads",bads) ;
								//                                vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
								for( ::string const& bad : bads ) audit( out_fd , rrr.options , Color::Err , 0 , "cannot process target",bad) ;
								//                                ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
								goto Bad ;
							}
						} break ;
						Bad :
							//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
							OMsgBuf().send( out_fd , ReqRpcReply(false/*ok*/) ) ;
							//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
							trace("bad",fd) ;
							epoll.close (fd) ;
							in_tab.erase(fd) ;
						break ;
						default :
							trace("close",fd) ;
							epoll.del(fd) ;                                       // must precede close(fd)
							//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
							g_engine_queue.emplace( ReqProc::Kill , fd , out_fd ) ; // this will close out_fd when done writing to it
							//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
							in_tab.erase(fd) ;
					}
				} break ;
				default : fail("kind=",kind,"fd=",fd) ;
			}
		}
		//
		if ( !_g_is_daemon && in_tab.empty() ) break ;                         // check end of loop after processing slave events and before master events
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
	trace("end_loop") ;
}

bool/*interrupted*/ engine_loop() {
	Trace trace("engine_loop") ;
	::umap<Fd,Req                      > req_tab ;                             // indexed by out_fd
	::umap<Req,pair<Fd/*in*/,Fd/*out*/>> fd_tab  ;
	 bool                                empty   = false/*garbage*/ ;
	 while ( !(empty=!g_engine_queue) || _g_is_daemon || Req::s_n_reqs() || !_g_done ) {
		if (empty) {                                                                     // we are about to block, do some book-keeping
			trace("wait") ;
			for( auto const& [fd,req] : req_tab ) req->audit_stats() ;
			if ( !_g_is_daemon && !Req::s_n_reqs() && _g_done ) break ;
		}
		EngineClosure closure = g_engine_queue.pop() ;
		DiskDate::s_refresh_now() ;                                            // we may have waited, refresh now
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
				EngineClosure::Req& req = closure.req ;
				switch (req.proc) {
					case ReqProc::Forget :                                     // PER_CMD : handle request coming from receiving thread, just add your Proc here if the request is answered immediately
					case ReqProc::Freeze :
					case ReqProc::Show   :
						trace(req) ;
						OMsgBuf().send( req.out_fd , ReqRpcReply(g_cmd_tab[+req.proc]( req.out_fd , req.options , req.targets )) ) ;
					break ;
					case ReqProc::Make : {
						record_targets(req.targets) ;
						Req r ;
						try {
							r = { req.out_fd , req.targets , req.options } ;
						} catch(::string const& e) {
							audit( req.out_fd , req.options , Color::Err , 0 , e ) ;
							OMsgBuf().send( req.out_fd , ReqRpcReply(false/*ok*/) ) ;
							break ;
						}
						req_tab[req.out_fd] = r                      ;
						fd_tab [r         ] = {req.in_fd,req.out_fd} ;
						trace("new_req",r) ;
						//vvvvvv
						r.make() ;
						Backend::s_launch() ;                                  // tell backend a bunch of submit's may be finished as backend may wait until then before actually launching jobs
						//^^^^^^^^^^^^^^^^^
					} break ;
					// there is exactly one Kill and one Close for each Req created by Make but unordered :
					// - there may be spurious Kill w/o corresponding Req if lmake is interrupted before sending its Req
					// - close out fd on Close (or shutdown if it is a socket as we have a single fd for both directions) to tell client nothing more will come
					// - release req_tab entry upon the first
					// - close in_fd (or in/out fd if it is a socket) upon the second (not upon Close as epoll would be undefined behavior if it still waits on in fd)
					case ReqProc::Kill : {
						auto it = req_tab.find(req.out_fd) ;
						if (it==req_tab.end()) { trace("close_in",req.in_fd ) ; ::close(req.in_fd)                    ; }
						//                                                      vvvvvvvvvvvvvvvvv
						else                   { trace("release" ,req.out_fd) ; it->second.kill() ; req_tab.erase(it) ; }
						//                                                      ^^^^^^^^^^^^^^^^^
					} break ;
					case ReqProc::Close : {
						auto                              fd_it  = fd_tab .find(req.req   ) ;
						::pair<Fd/*in*/,Fd/*out*/> const& fds    = fd_it->second            ;
						auto                              req_it = req_tab.find(fds.second) ;
						//vvvvvvvvvvvvv
						req.req.close() ;
						//^^^^^^^^^^^^^
						if (fds.first!=fds.second) { trace("close_out",fds) ; ::close (fds.second        ) ; } // tell client no more data will be sent (pipe   : close fd                      )
						else                       { trace("shutdown" ,fds) ; shutdown(fds.second,SHUT_WR) ; } // .                                     (socket : shutdown to leave input active)
						if (req_it==req_tab.end()) { trace("close_in" ,fds) ; ::close (fds.first         ) ; }
						else                       { trace("release"  ,fds) ; req_tab.erase(req_it)        ; }
						fd_tab.erase(fd_it) ;
					} break ;
					default : FAIL(req.proc) ;
				}
			} break ;
			case EngineClosureKind::Job : {
				EngineClosure::Job& job = closure.job ;
				trace("job",job.proc,job.job) ;
				switch (job.proc) {
					//                          vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					case JobProc::Start       : job.job.started      (job.report,job.report_unlink) ;                       break ;
					case JobProc::ReportStart : job.job.report_start (                            ) ;                       break ;
					case JobProc::LiveOut     : job.job.live_out     (job.txt                     ) ;                       break ;
					case JobProc::Continue    : job.job.premature_end(job.req                     ) ;                       break ;
					case JobProc::NotStarted  : job.job.not_started  (                            ) ;          /*vvvvvv*/   break ;
					case JobProc::End         : job.job.end          (job.start,job.digest        ) ; Backend::s_launch() ; break ; // backends may defer job launch to have a complete view
					//                          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^            ^^^^^^^^^^
					case JobProc::ChkDeps     :
					//                          vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					case JobProc::DepCrcs     : OMsgBuf().send( job.reply_fd , job.job.job_info(job.proc,job.digest.deps) ) ; ::close(job.reply_fd) ; break ;
					//                          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					default : FAIL(job.proc) ;
				}
			} break ;
			default : FAIL(closure.kind) ;
		}
	}
	trace("end_loop") ;
	return false ;
}

int main( int argc , char** argv ) {
	if(!( argc==1 || (argc==2&&!argv[1][0]) ))
		exit(2,"syntax is lmakeserver '' (launched by lmake) or lmakeserver (no args, daemon)") ;
	//
	Fd int_fd = open_sig_fd(SIGINT,true/*block*/) ;                            // must be done before app_init so that all threads block the signal
	block_sig(SIGCHLD) ;
	//vvvvvvvvvvvvvvvvvvvvv
	g_store.writable = true ;
	//^^^^^^^^^^^^^^^^^^^^^
	Trace::s_backup_trace = true ;
	app_init(false/*search_root*/,false/*cd_root*/) ;                          // server is always launched at root
	_g_server_mrkr = to_string(AdminDir,'/',ServerMrkr) ;
	_g_is_daemon   = argc==1                         ;
	Trace trace("main",getpid(),*g_lmake_dir,*g_root_dir) ;
	//             vvvvvvvvvvvvvv
	bool crashed = start_server() ;
	//             ^^^^^^^^^^^^^^
	if (!_g_is_daemon     ) report_server(Fd::Stdout,_g_server_running/*server_running*/) ; // inform lmake we did not start
	if (!_g_server_running) return 0 ;
	if (crashed) {
		::cerr<<"previous crash detected, checking & rescueing"<<endl ;
		//           vvvvvvvvvv
		EngineStore::s_rescue() ;
		//           ^^^^^^^^^^
		::cerr<<"seems ok"<<endl ;
	}
	//                                     vvvvvvvvvvvvvvvvvvvvv
	try                       { Makefiles::s_refresh_makefiles() ; }
	//                                     ^^^^^^^^^^^^^^^^^^^^^
	catch (::string const& e) { exit(2,e) ;                        }
	if (!_g_is_daemon) ::setpgid(0,0) ;                                        // once we have reported we have started, lmake will send us a message to kill us
	//
	Trace::s_sz = g_config.trace_sz ;
	if (g_has_local_admin_dir) Trace::s_new_trace_file(to_string( *g_local_admin_dir , "/trace/" , base_name(read_lnk("/proc/self/exe")) )) ;
	//
	static ::jthread reqs_thread{ reqs_thread_func , int_fd } ;
	//
	//                 vvvvvvvvvvvvv
	bool interrupted = engine_loop() ;
	//                 ^^^^^^^^^^^^^
	trace("exit",STR(interrupted),ProcessDate::s_now()) ;
	return interrupted ;
}
