// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <sys/inotify.h>

#include <csignal>

#include "msg.hh"
#include "trace.hh"

inline bool is_sig_sync(int sig) {
	switch (sig) {
		case SIGILL  :
		case SIGTRAP :
		case SIGABRT :
		case SIGBUS  :
		case SIGFPE  :
		case SIGSEGV :
		case SIGSYS  : return true  ;
		default      : return false ;
	}
}

inline bool wstatus_ok(int wstatus) {
	return WIFEXITED(wstatus) && WEXITSTATUS(wstatus)==0 ;
}

::string wstatus_str(int wstatus) ;

/**/   bool/*done*/   kill_process ( pid_t pid , int sig , bool as_group=false ) ;
inline bool/*exists*/ sense_process( pid_t pid                                 ) { return kill_process( pid , 0 ) ; }

pid_t  get_ppid (pid_t pid) ;
mode_t get_umask(         ) ;

struct Child {
	static constexpr size_t StackSz = 16<<10 ;        // stack size for sub-process : we just need s small stack before exec, experiment shows 8k is enough, take 16k
	static constexpr Fd     NoneFd  { -1 }   ;
	static constexpr Fd     PipeFd  { -2 }   ;
	static constexpr Fd     JoinFd  { -3 }   ;        // used on sderr to join to stdout
	// cxtors & casts
	~Child() {
		swear_prod(pid==0,"bad pid",pid) ;
	}
	// accesses
	bool operator+() const { return pid ; }
	// services
	void spawn() ;
	void mk_daemon() {
		pid = 0 ;
		_p2c .write.detach() ;
		_c2po.read .detach() ;
		_c2pe.read .detach() ;
	}
	void waited() {
		pid = 0 ;
	}
	int/*wstatus*/ wait() {
		SWEAR(pid!=0) ;
		int wstatus ;
		int rc      = ::waitpid(pid,&wstatus,0/*options*/) ;
		swear_prod(rc==pid,"cannot wait for pid",pid) ;
		waited() ;
		return wstatus ;
	}
	bool         wait_ok (       )       { return wstatus_ok(wait())                           ; }
	bool/*done*/ kill    (int sig)       { return kill_process(pid,sig,as_session/*as_group*/) ; }
	bool         is_alive(       ) const { return ::kill(pid,0/*sig*/)==0                      ; }
private :
	[[noreturn]] void _exit( Rc , const char* msg , const char* msg_dir_s=nullptr ) ;
	//data
public :
	// spawn parameters
	::map_ss const* add_env            = nullptr    ;
	bool            as_session         = false      ;
	::vector_s      cmd_line           = {}         ;
	::string        cwd_s              = {}         ;
	::map_ss const* env                = nullptr    ;
	uint8_t         nice               = 0          ;
	int/*rc*/       (*pre_exec)(void*) = nullptr    ; // if no cmd_line, this is the entire function exec'ed as child returning the exit status
	void*           pre_exec_arg       = nullptr    ;
	Fd              stderr             = Fd::Stderr ;
	Fd              stdin              = Fd::Stdin  ;
	Fd              stdout             = Fd::Stdout ;
	// child info
	pid_t pid = 0 ;
	// private (cannot really declare private or it would not be an aggregate any more)
	Pipe         _p2c        = {}      ;
	Pipe         _c2po       = {}      ;
	Pipe         _c2pe       = {}      ;
	const char** _child_args = nullptr ;              // all memory must be allocated before clone/fork/vfork is called
	const char** _child_env  = nullptr ;              // .
} ;

struct AutoServerBase {
	struct SlaveEntry {
		Bool3       out_active = Maybe ;  // Maybe means both input and output are active, Yes means output is active, No means input is active
		SockFd::Key key        = {}    ;
		IMsgBuf     buf        = {}    ;
	} ;
	// cxtors & casts
	AutoServerBase() = default ;
	AutoServerBase(::string const& sm) : server_mrkr{sm} {}
	// accesses
	size_t n_connections() const { return _slaves.size() ; }
	// services
	void start() ;
	// data
	bool         handle_int  = false ;    // config
	bool         is_daemon   = false ;    // .
	bool         writable    = false ;    // .
	bool         rescue      = false ;    // report
	::string     server_mrkr ;
	ServerSockFd server_fd   ;
	AcFd         watch_fd    ;
protected :
	Mutex<> mutable       _slaves_mutex ;
	::umap<Fd,SlaveEntry> _slaves       ; // indexed by in_fd
} ;

template<class T> struct AutoServer : AutoServerBase {
	// cxtors & casts
	using AutoServerBase::AutoServerBase ;
	// services
	bool/*interrupted*/ event_loop     (         ) ;
	void                close_slave_out(Fd out_fd) ;
	// injection
	bool/*done*/  interrupt       (                ) { return false/*done*/ ; }
	void          start_connection( Fd             ) {                        }
	void          end_connection  ( Fd             ) {                        }
//	Bool3/*done*/ process_item    ( Fd , T::Item&& ) ;                          // Maybe means there may be further outputs to Fd, close_slave_out will be/has been called
} ;

::pair<ClientSockFd,pid_t> connect_to_server( bool try_old , uint64_t magic , ::vector_s&& cmd_line , ::string const& server_mrkr , ::string const& dir={} ) ;

//
// implementation
//

enum class _AutoServerEventKind : uint8_t {
	Master
,	Stdin
,	Slave
,	Int
,	Watch
} ;

template<class T> bool/*interrupted*/ AutoServer<T>::event_loop() {
	using Item      = typename T::Item        ;
	using EventKind = _AutoServerEventKind    ;
	using Event     = Epoll<EventKind>::Event ;
	Trace trace("server_loop",STR(is_daemon)) ;
	//
	Epoll<EventKind> epoll       { New } ;
	bool             interrupted = false ;
	//
	auto new_slave = [&]( Fd fd , SockFd::Key key={} ) {
		epoll.add_read(fd,EventKind::Slave) ;
		trace("new_slave",fd,key) ;
		::string magic_str ( sizeof(T::Magic) , 0 ) ; encode_int( magic_str.data() , T::Magic ) ;
		try                     { fd.write(magic_str) ; }
		catch (::string const&) { trace("no_report") ;  }                                                                              // client is dead
		Lock lock     { _slaves_mutex }                                         ;
		auto inserted = _slaves.try_emplace( fd , SlaveEntry{.key=key} ).second ; SWEAR( inserted , fd ) ;
		static_cast<T&>(self).start_connection(fd) ;
	} ;
	//                                                                wait
	if (+server_fd) { epoll.add_read( server_fd , EventKind::Master , is_daemon ) ; trace("read_master",server_fd) ; }                 // if read-only, we do not expect additional connections
	if (handle_int) { epoll.add_sig ( SIGHUP    , EventKind::Int    , false     ) ; trace("read_hup"             ) ; }
	if (handle_int) { epoll.add_sig ( SIGINT    , EventKind::Int    , false     ) ; trace("read_int"             ) ; }
	if (+watch_fd ) { epoll.add_read( watch_fd  , EventKind::Watch  , false     ) ; trace("read_watch" ,watch_fd ) ; }
	if (!is_daemon) { epoll.add_read( Fd::Stdin , EventKind::Stdin  , true      ) ; trace("read_stdin" ,Fd::Stdin) ; }
	//
	while (+epoll) {
		bool new_fd = false ;
		for( Event event : epoll.wait() ) {
			EventKind kind = event.data() ;
			Fd        fd   = event.fd  () ;
			trace("event",kind,fd) ;
			switch (kind) {
				case EventKind::Watch : {
					struct inotify_event event ;
					ssize_t              cnt   = ::read( fd , &event , sizeof(event) ) ;
					SWEAR( cnt==sizeof(event) , cnt ) ;
					trace("watch",event.mask) ;
				} [[fallthrough]] ;
				case EventKind::Int :
					interrupted = true ;
					if (static_cast<T&>(self).interrupt()) goto Done ;
				break ;
				case EventKind::Stdin :
					Fd::Stdin.read() ;
					epoll.close(false/*write*/,Fd::Stdin) ;
				break ;
				case EventKind::Master :
					// it may be that in a single poll, we get the end of a previous run and a request for a new one
					// problem lies in this sequence :
					// - lmake foo
					// - touch Lmakefile.py
					// - lmake bar          => maybe we get this request in the same poll as the end of lmake foo and we would eroneously say that it cannot be processed
					// solution is to delay Master event after other events and ignore them if we are done inbetween
					// note that there may be at most a single Master event
					SWEAR( !new_fd , new_fd ) ;
					new_fd = true ;
				break ;
				case EventKind::Slave : {
					auto        it = _slaves.find(fd) ; SWEAR( it!=_slaves.end()  , fd ) ;
					SlaveEntry& se = it->second       ; SWEAR( se.out_active!=Yes , fd ) ;
					Trace trace("process",se.key,fd) ;
					//
					for( Bool3 fetch=Yes ;; fetch=No ) {
						::optional<Item> received = se.buf.receive_step<Item>( fd , fetch , /*inout*/se.key ) ; if (!received) break ; // partial message
						Item&            item     = *received                                                 ;
						trace("item",item) ;
						Bool3 done = static_cast<T&>(self).process_item(fd,::move(item)) ;
						if (done==No) {
							SWEAR(+item) ;                                                          // ensure we have no eof condition to avoid infinite loop
						} else {
							epoll.del(false/*write*/,fd) ; trace("del_slave_fd",fd,se.out_active) ; // /!\ must precede close(fd) which may not occur as long as input is not closed
							Lock lock { _slaves_mutex } ;
							if ( done==Maybe && se.out_active==Maybe ) { se.out_active = Yes                      ; ::shutdown(fd,SHUT_RD) ;                     }
							else                                       { static_cast<T&>(self).end_connection(fd) ; ::close   (fd        ) ; _slaves.erase(it) ; }
							break ;
						}
					}
				} break ;
			DF}                                                                                     // NO_COV
		}
		if (new_fd) new_slave( server_fd.accept().detach() , server_fd.key ) ;
	}
Done :
	trace("done",STR(interrupted)) ;
	return interrupted ;
}

template<class T> void AutoServer<T>::close_slave_out(Fd fd)  {
	Lock        lock { _slaves_mutex }  ;
	auto        it   = _slaves.find(fd) ; SWEAR( it!=_slaves.end() , fd ) ;
	SlaveEntry& se   = it->second       ; SWEAR( se.out_active!=No , fd ) ;
	Trace trace("close_slave_out",fd,se.out_active) ;
	if (se.out_active==Maybe) { se.out_active = No                       ; ::shutdown(fd,SHUT_WR) ;                     }
	else                      { static_cast<T&>(self).end_connection(fd) ; ::close   (fd        ) ; _slaves.erase(it) ; }
}
