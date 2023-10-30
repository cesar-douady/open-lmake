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
	::unlink(_g_server_mrkr.c_str()) ;
	trace("cleaned") ;
}

void report_server( Fd fd , bool running ) {
	int cnt = ::write( fd , &running , sizeof(bool) ) ;
	if (cnt!=sizeof(bool)) fail_prod("cannot report server ",STR(running)) ;
}

bool/*crashed*/ start_server() {
	bool  crashed = false    ;
	pid_t pid     = getpid() ;
	Trace trace("start_server",_g_server_mrkr,_g_host,pid) ;
	dir_guard(_g_server_mrkr) ;
	::pair_s<pid_t> mrkr = _get_mrkr_host_pid() ;
	if ( !mrkr.first.empty() && mrkr.first!=_g_host ) {
		trace("already_existing_elsewhere",mrkr) ;
		return false/*unused*/ ;                                               // if server is running on another host, we cannot qualify with a kill(pid,0), be pessimistic
	}
	if (mrkr.second) {
		if (kill_process(mrkr.second,0)) {                                     // another server exists
			trace("already_existing",mrkr) ;
			return false/*unused*/ ;
		}
		::unlink(_g_server_mrkr.c_str()) ;                                     // before doing anything, we create the marker, which is unlinked at the end, so it is a marker of a crash
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
	::unlink(tmp.c_str()) ;
	trace("started",STR(crashed),STR(_g_is_daemon),STR(_g_server_running)) ;
	return crashed ;
}

void record_targets( ::vector<Node> const& targets ) {
	::string targets_file    = AdminDir+"/targets"s ;
	::vector_s known_targets ;
	{	::ifstream targets_stream { targets_file } ;
		::string   target         ;
		while (::getline(targets_stream,target)) known_targets.push_back(target) ;
	}
	for( Node t : targets ) {
		::string tn = t.name() ;
		for( ::string& ktn : known_targets ) if (ktn==tn) ktn.clear() ;
		known_targets.push_back(tn) ;
	}
	{	::ofstream targets_stream { dir_guard(targets_file) } ;
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
							::string reason = Makefiles::s_chk_makefiles() ;
							if (!reason.empty()) {
								//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
								audit( out_fd , rrr.options , Color::Err , 0 , to_string("cannot make with modified makefiles (",reason,") while other lmake is running\n") ) ;
								//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
								trace("modified_makefiles") ;
								goto Bad ;
							}
						} [[fallthrough]] ;
						case ReqProc::Debug  :                                 // PER_CMD : handle request coming from command, just add your Proc here if the request is answered immediately
						case ReqProc::Forget :
						case ReqProc::Mark   :
						case ReqProc::Show   : {
							::vector<Node> targets   ; targets.reserve(rrr.targets.size()) ; // typically, there is no bads
							::vector_s     bads      ;
							for( ::string const& target : rrr.targets ) {
								RealPath::SolveReport rp = _g_real_path.solve(target,true/*no_follow*/) ; // ignore links that lead to real path
								if (rp.kind==Kind::Repo) targets.emplace_back(rp.real) ;
								else                     bads   .emplace_back(target ) ;
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
								//                                vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
								for( ::string const& bad : bads ) audit( out_fd , rrr.options , Color::Err , 0 , "cannot make target outside repository : ",bad) ;
								//                                ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
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
	::umap<Fd,Req                      > req_tab         ;                     // indexed by out_fd
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
			for( auto const& [fd,req] : req_tab ) req->audit_stats() ;         // refresh title
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
				EngineClosure::Req& req = closure.req ;
				switch (req.proc) {
					case ReqProc::Debug  :                                     // PER_CMD : handle request coming from receiving thread, just add your Proc here if the request is answered immediately
					case ReqProc::Forget :
					case ReqProc::Mark   :
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
						//^^^^^^
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
				JobExec           & je  = job.exec    ;
				trace("job",job.proc,je) ;
				switch (job.proc) {
					//                          vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					case JobProc::Start       : je.started      (job.report,job.report_unlink,job.txt) ; break ;
					case JobProc::ReportStart : je.report_start (                                    ) ; break ;
					case JobProc::LiveOut     : je.live_out     (job.txt                             ) ; break ;
					case JobProc::Continue    : je.premature_end(job.req                             ) ; break ;
					case JobProc::NotStarted  : je.not_started  (                                    ) ; break ;
					case JobProc::End         : je.end          (job.rsrcs,job.digest                ) ; break ;
					//                          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
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
	bool refresh = true ;
	for( int i=1 ; i<argc ; i++ ) {
		if ( argv[i][0]!='-' || argv[i][2]!=0 ) exit(2,"unrecognized argument : ",argv[i],"\nsyntax : lmakeserver [-r/*no makefile refresh*/]") ;
		switch (argv[i][1]) {
			case 'd' : _g_is_daemon = false ; break ;
			case 'r' : refresh      = false ; break ;
			case '-' :                        break ;
			default : exit(2,"unrecognized option : ",argv[i]) ;
		}
	}
	//
	Fd int_fd = open_sig_fd(SIGINT,true/*block*/) ;                            // must be done before app_init so that all threads block the signal
	block_sig(SIGCHLD) ;
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
	//               vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	try { Makefiles::s_refresh_makefiles(crashed,refresh) ; }
	//               ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	catch (::string const& e) { exit(2,e) ; }
	if (!_g_is_daemon) ::setpgid(0,0) ;                                        // once we have reported we have started, lmake will send us a message to kill us
	//
	Trace::s_sz = g_config.trace_sz ;
	Trace::s_new_trace_file(to_string( g_config.local_admin_dir , "/trace/" , base_name(read_lnk("/proc/self/exe")) )) ;
	//
	static ::jthread reqs_thread{ reqs_thread_func , int_fd } ;
	//
	//                 vvvvvvvvvvvvv
	bool interrupted = engine_loop() ;
	//                 ^^^^^^^^^^^^^
	trace("exit",STR(interrupted),Pdate::s_now()) ;
	return interrupted ;
}
