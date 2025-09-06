// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/socket.h>

#include "time.hh"

#ifndef SOCK_CLOEXEC
	#define SOCK_CLOEXEC 0 // CLOEXEC is just defensive programming, not really needed
#endif

::string host() ;
::string fqdn() ; // fully qualified domain name (includes hostname)

struct LockedFd : AcFd {
	friend ::string& operator+=( ::string& , LockedFd const& ) ;
	// cxtors & casts
	LockedFd() = default ;
	//
	LockedFd ( ::string const& file , bool exclusive=true ) : AcFd{file,{.flags=O_RDWR}} { lock  (exclusive) ; }
	~LockedFd(                                            )                              { unlock(         ) ; }
	//
	LockedFd           (LockedFd&&) = default ;
	LockedFd& operator=(LockedFd&&) = default ;
	//
	void lock(bool exclusive) {
		if (!self) return ;
		struct flock lock {
			.l_type   = short( exclusive ? F_WRLCK : F_RDLCK )
		,	.l_whence = SEEK_SET
		,	.l_start  = 0
		,	.l_len    = 1 // ensure a lock exists even if file is empty
		,	.l_pid    = 0
		} ;
		while (::fcntl(fd,F_SETLKW,&lock)!=0) swear_prod( errno==EINTR , +self ) ;
	}
	void unlock() {
		if (!self) return ;
		struct flock lock {
			.l_type   = F_UNLCK
		,	.l_whence = SEEK_SET
		,	.l_start  = 0
		,	.l_len    = 1 // ensure a lock exists even if file is empty
		,	.l_pid    = 0
		} ;
		swear_prod( ::fcntl(fd,F_SETLK,&lock)==0 , +self ) ;
	}
} ;

struct SockFd : AcFd {
	friend ::string& operator+=( ::string& , SockFd const& ) ;
	static constexpr in_addr_t LoopBackAddr = 0x7f000001 ;
	// statics
	static ::string s_addr_str(in_addr_t addr) {
		::string res ; res.reserve(15) ;             // 3 digits per level + 5 digits for the port
		res <<      ((addr>>24)&0xff) ;              // dot notation is big endian
		res <<'.'<< ((addr>>16)&0xff) ;
		res <<'.'<< ((addr>> 8)&0xff) ;
		res <<'.'<< ((addr>> 0)&0xff) ;
		return res ;
	}
	static struct sockaddr_in  s_sockaddr( in_addr_t a , in_port_t p ) {
		struct sockaddr_in res {
			.sin_family = AF_INET
		,	.sin_port   =           htons(p)         // dont prefix with :: as htons may be a macro
		,	.sin_addr   = { .s_addr=htonl(a) }       // dont prefix with :: as htonl may be a macro
		,	.sin_zero   = {}
		} ;
		return res ;
	}
	static ::string const&     s_host      (in_addr_t              ) ;
	static ::string            s_host      (::string const& service) { size_t col = _s_col(service) ; return   service.substr(0,col)                                                   ; }
	static in_port_t           s_port      (::string const& service) { size_t col = _s_col(service) ; return                           from_string<in_port_t>(service.c_str()+col+1)   ; }
	static ::pair_s<in_port_t> s_host_port (::string const& service) { size_t col = _s_col(service) ; return { service.substr(0,col) , from_string<in_port_t>(service.c_str()+col+1) } ; }
	static in_addr_t           s_addr      (::string const& server ) ;
	static ::vmap_s<in_addr_t> s_addrs_self(::string const& ifce={}) ;
	//
	static ::string s_service( ::string const& host , in_port_t port ) { return cat(host,':',port)               ; }
	static ::string s_service( in_addr_t       addr , in_port_t port ) { return s_service(s_addr_str(addr),port) ; }
	static ::string s_service(                        in_port_t port ) { return s_service(host()          ,port) ; }
private :
	static size_t _s_col(::string const& service) {
		size_t col = service.rfind(':') ;
		throw_unless(col!=Npos , "bad service : ",service ) ;
		return col ;
	}
	// cxtors & casts
public :
	using AcFd::AcFd ;
	SockFd(NewType) { init() ; }
	//
	void init() {
		self = ::socket( AF_INET , SOCK_STREAM|SOCK_CLOEXEC , 0/*protocol*/ ) ;
		no_std() ;
	}
	// services
	// if timeout is 0, it means infinity (no timeout)
	void set_receive_timeout(Time::Delay to={}) { Time::Pdate::TimeVal to_tv(to) ; ::setsockopt( fd , SOL_SOCKET , SO_RCVTIMEO , &to_tv , sizeof(to_tv) ) ; }
	void set_send_timeout   (Time::Delay to={}) { Time::Pdate::TimeVal to_tv(to) ; ::setsockopt( fd , SOL_SOCKET , SO_SNDTIMEO , &to_tv , sizeof(to_tv) ) ; }
	void set_timeout        (Time::Delay to={}) {
		set_receive_timeout(to) ;
		set_send_timeout   (to) ;
	}
	in_addr_t peer_addr() const {
		struct sockaddr_in peer_addr ;
		socklen_t          len       = sizeof(peer_addr)                                                           ;
		int                rc        = ::getpeername( fd , reinterpret_cast<struct sockaddr*>(&peer_addr) , &len ) ;
		SWEAR( rc ==0                 , rc ,self ) ;
		SWEAR( len==sizeof(peer_addr) , len,self ) ;
		return ntohl(peer_addr.sin_addr.s_addr) ;    // dont prefix with :: as ntohl may be a macro
	}
	in_port_t port() const {
		struct sockaddr_in my_addr ;
		socklen_t          len     = sizeof(my_addr)                                                           ;
		int                rc      = ::getsockname( fd , reinterpret_cast<struct sockaddr*>(&my_addr) , &len ) ;
		SWEAR( rc ==0               , rc ,self ) ;
		SWEAR( len==sizeof(my_addr) , len,self ) ;
		return ntohs(my_addr.sin_port) ;             // dont prefix with :: as ntohs may be a macro
	}
} ;

struct SlaveSockFd : SockFd {
	friend ::string& operator+=( ::string& , SlaveSockFd const& ) ;
	// cxtors & casts
	using SockFd::SockFd ;
} ;

struct ServerSockFd : SockFd {
	friend ::string& operator+=( ::string& , ServerSockFd const& ) ;
	// cxtors & casts
	using SockFd::SockFd ;
	ServerSockFd( NewType , int backlog=0 ) { listen(backlog) ; }
	// services
	void listen(int backlog=0) {
		if (!self   ) init() ;
		if (!backlog) backlog = 100 ;
		int rc = ::listen(fd,backlog) ;
		swear_prod(rc==0,"cannot listen on",self,"with backlog",backlog,'(',rc,')') ;
	}
	::string service(in_addr_t addr) const { return s_service(addr,port()) ; }
	::string service(              ) const { return s_service(     port()) ; }
	SlaveSockFd accept() ;
} ;

struct ClientSockFd : SockFd {
	static constexpr int NTrials = 3 ;
	friend ::string& operator+=( ::string& , ClientSockFd const& ) ;
	// cxtors & casts
	using SockFd::SockFd ;
	ClientSockFd( in_addr_t       server , in_port_t port , Time::Delay timeout={} ) { connect(server,port,timeout) ; }
	ClientSockFd( ::string const& server , in_port_t port , Time::Delay timeout={} ) { connect(server,port,timeout) ; }
	ClientSockFd( ::string const& service                 , Time::Delay timeout={} ) { connect(service    ,timeout) ; }
	// services
	void connect( in_addr_t       server , in_port_t port , Time::Delay timeout={} ) ;
	void connect( ::string const& server , in_port_t port , Time::Delay timeout={} ) {
		connect( s_addr(server) , port , timeout ) ;
	}
	void connect( ::string const& service , Time::Delay timeout={} ) {
		::pair_s<in_port_t> host_port = s_host_port(service) ;
		connect( host_port.first , host_port.second , timeout ) ;
	}
} ;

//
// sigs
//

inline sigset_t _mk_sigset(::vector<int> const& sigs) {
	sigset_t res ;
	sigemptyset(&res) ;                                                                        // sigemptyset can be a macro
	for( int s : sigs) sigaddset(&res,s) ;                                                     // sigaddset can be a macro
	return res ;
}
inline bool is_blocked_sig(int sig) {
	sigset_t old_mask ;
	swear( ::pthread_sigmask(0/*how*/,nullptr/*set*/,&old_mask)==0 , "cannot get sig ",sig ) ;
	return sigismember(&old_mask,sig) ;                                                        // sigismember can be a macro
}
inline void block_sigs  (::vector<int> const& sigs) { swear( ::pthread_sigmask( SIG_BLOCK   , &::ref(_mk_sigset(sigs)) , nullptr )==0 , "cannot block sigs "  ,sigs) ; }
inline void unblock_sigs(::vector<int> const& sigs) { swear( ::pthread_sigmask( SIG_UNBLOCK , &::ref(_mk_sigset(sigs)) , nullptr )==0 , "cannot unblock sigs ",sigs) ; }
struct BlockedSig {
	BlockedSig (                         ) = default ;
	BlockedSig (BlockedSig&&             ) = default ;                                         // copy is meaningless
	BlockedSig (::vector<int> const& sigs) : blocked{       sigs } { block_sigs  (blocked) ; }
	BlockedSig (::vector<int>     && sigs) : blocked{::move(sigs)} { block_sigs  (blocked) ; }
	~BlockedSig(                         )                         { unblock_sigs(blocked) ; }
	// data
	::vector<int> blocked ;
} ;

//
// Pipe
//

template<class F> struct _Pipe {
	// cxtors & casts
	_Pipe(                                    ) = default ;
	_Pipe( NewType                            ) { open(             ) ; }
	_Pipe( NewType , int flags , bool no_std_ ) { open(flags,no_std_) ; }
	// services
	void open(                          ) { open(0/*flags*/,false/*no_std*/) ; }
	void open( int flags , bool no_std_ ) {
		int fds[2] ;
		if (::pipe2(fds,flags)!=0) fail_prod( "cannot create pipes (flags=0x",to_hex(uint(flags)),") : ",::strerror(errno) ) ;
		read  = F(fds[0],no_std_) ;
		write = F(fds[1],no_std_) ;
	}
	void close () { read.close () ; write.close () ; }
	void no_std() { read.no_std() ; write.no_std() ; }
	void detach() { read.detach() ; write.detach() ; }
	// data
	F read  ; // read  side of the pipe
	F write ; // write side of the pipe
} ;

using Pipe   = _Pipe<Fd  > ;
using AcPipe = _Pipe<AcFd> ;

//
// EventFd
//

struct EventFd : AcFd {
	friend ::string& operator+=( ::string& , EventFd const& ) ;
	// cxtors & casts
	EventFd(NewType) : AcFd{::eventfd(0/*initval*/,O_CLOEXEC),true/*no_std*/} {}
	EventFd(Fd fd_ ) : AcFd{fd_                                             } {}
	// services
	void wakeup() const {
		static constexpr uint64_t One = 1 ;
		ssize_t cnt = ::write(self,&One,sizeof(One)) ;
		SWEAR( cnt==sizeof(One) , cnt,self ) ;
	}
	void flush() const {
		uint64_t one ;
		ssize_t  cnt = ::read(self,&one,sizeof(one)) ;
		SWEAR( cnt==sizeof(one) , cnt,self ) ;
	}
} ;

//
// SignalFd
//

struct SignalFd : AcFd {
	friend ::string& operator+=( ::string& , SignalFd const& ) ;
	// cxtors & casts
	SignalFd( NewType, int sig ) : AcFd{_mk_fd(sig),true/*no_std*/} {}
	SignalFd( Fd fd_           ) : AcFd{fd_                       } {}
private :
	int _mk_fd(int sig) {
		SWEAR(is_blocked_sig(sig)) ;                                                                                            // if not blocked, it may signal the process
		::sigset_t sig_set  ;                                                 sigemptyset(&sig_set) ; sigaddset(&sig_set,sig) ; // sigemptyset and sigaddset can be macros
		return ::signalfd( -1/*fd*/ , &sig_set , SFD_CLOEXEC|SFD_NONBLOCK ) ;
	}
	// services
public :
	int/*sig*/ read() const {
		struct ::signalfd_siginfo si ;
		ssize_t  cnt = ::read(self,&si,sizeof(si)) ;
		SWEAR( cnt==sizeof(si) , cnt,self ) ;
		return int(si.ssi_signo) ;
	}
} ;

//
// Epoll
//

extern StaticUniqPtr<::uset<int>> _s_epoll_sigs ;      // use pointer to avoid troubles when freeing at end of execution, cannot wait for the same signal on several instances
template<Enum E=NewType/*when_unused*/> struct Epoll {
	struct Event : ::epoll_event {
		// cxtors & casts
		using ::epoll_event::epoll_event ;
		Event(                             ) : ::epoll_event{ .events=0                      , .data{.u64=                     uint32_t(Fd())} } {}
		Event( bool write , Fd fd , E data ) : ::epoll_event{ .events=write?EPOLLOUT:EPOLLIN , .data{.u64=(uint64_t(data)<<32)|uint32_t(fd  )} } {}
		// access
		bool operator+(               ) const { return +fd()                                 ; }
		int sig       (Epoll const& ep) const { return ep._fd_infos.at(fd()).first           ; }
		Fd  fd        (               ) const { return uint32_t(::epoll_event::data.u64    ) ; }
		E   data      (               ) const { return E       (::epoll_event::data.u64>>32) ; }
	} ;
	// cxtors & casts
	Epoll(       ) = default ;
	Epoll(NewType) { init () ; }
	~Epoll() {
		for( auto [fd,sig_pid] : _fd_infos ) _s_epoll_sigs->erase(sig_pid.first) ;
	}
	// accesses
	uint operator+() const { return _n_waits ;                }
	void dec      ()       { SWEAR(_n_waits>0) ; _n_waits-- ; }
	// services
	void init() {
		_fd = AcFd( ::epoll_create1(EPOLL_CLOEXEC) , true/*no_std*/ ) ;
	}
	void add( bool write , Fd fd , E data={} , bool wait=true ) {
		Event event { write , fd , data } ;
		if (::epoll_ctl( _fd , EPOLL_CTL_ADD , fd , &event )!=0) fail_prod("cannot add",fd,"to epoll",_fd,'(',::strerror(errno),')') ;
		if (wait) _n_waits ++ ;
		/**/      _n_events++ ;
	}
	void del( bool /*write*/ , Fd fd , bool wait=true ) {                                                                                          // wait must be coherent with corresponding add
		if (::epoll_ctl( _fd , EPOLL_CTL_DEL , fd , nullptr/*event*/ )!=0) fail_prod("cannot del",fd,"from epoll",_fd,'(',::strerror(errno),')') ;
		if (wait) { SWEAR(_n_waits >0) ; _n_waits -- ; }
		/**/        SWEAR(_n_events>0) ; _n_events-- ;
	}
private :
	void _add_sig( int sig , E data , pid_t pid , bool wait ) {
		Fd         fd       = SignalFd(New,sig).detach()                                                  ;
		bool       inserted = _sig_infos   . try_emplace(sig,fd     ).second ; SWEAR(inserted,fd,sig    ) ;
		/**/       inserted = _fd_infos    . try_emplace(fd ,sig,pid).second ; SWEAR(inserted,fd,sig,pid) ;
		/**/       inserted = _s_epoll_sigs->insert     (sig        ).second ; SWEAR(inserted,fd,sig    ) ;
		add( false/*write*/ , fd , data , wait ) ;
		_n_sigs++ ;
	}
public :
	void add_sig( int sig , E data={} , bool wait=true ) { _add_sig(sig,data,0/*pid*/,wait) ; }
	void del_sig( int sig ,             bool wait=true ) {                                                                    // wait must be coherent with corresponding add_sig
		auto it = _sig_infos.find(sig) ; SWEAR(it!=_sig_infos.end(),sig) ;
		del( false/*write*/ , it->second , wait ) ;
		_fd_infos    . erase(it->second) ;                                                                                    // must be done before next line as it is not valid after erasing it
		_sig_infos   . erase(it        ) ;
		_s_epoll_sigs->erase(sig       ) ;
		SWEAR(_n_sigs) ; _n_sigs-- ;
	}
	void add_pid( pid_t pid , E data={} , bool wait=true ) { _add_sig(SIGCHLD,data,pid,wait) ; }
	void del_pid( pid_t     ,             bool wait=true ) { del_sig (SIGCHLD         ,wait) ; }                              // wait must be coherent with corresponding add_pid
	//
	void add_read (              Fd fd , E data={} , bool wait=true ) {              add(false,fd,data,wait) ;              }
	void add_write(              Fd fd , E data={} , bool wait=true ) {              add(true ,fd,data,wait) ;              }
	void close    ( bool write , Fd fd ,             bool wait=true ) { SWEAR(+fd) ; del(write,fd,     wait) ; fd.close() ; } // wait must be coherent with corresponding add
	//
	::vector<Event> wait(Time::Delay timeout=Time::Delay::Forever) const {
		if (!_n_events) {
			SWEAR(timeout<Time::Delay::Forever) ;                     // if we wait for nothing with no timeout, we would block forever
			timeout.sleep_for() ;
			return {} ;
		}
		struct ::timespec now         ;
		struct ::timespec end         ;
		bool              has_timeout = timeout>Time::Delay() && timeout!=Time::Delay::Forever ;
		if (has_timeout) {
			::clock_gettime(CLOCK_MONOTONIC,&now) ;
			end.tv_sec  = now.tv_sec  + timeout.sec()       ;
			end.tv_nsec = now.tv_nsec + timeout.nsec_in_s() ;
			if (end.tv_nsec>=1'000'000'000l) {
				end.tv_nsec -= 1'000'000'000l ;
				end.tv_sec  += 1              ;
			}
		}
		for(;;) {                                                     // manage case where timeout is longer than the maximum allowed timeout by looping over partial timeouts
			::vector<Event> events        ( _n_events ) ;
			int             cnt_          ;
			int             wait_ms       = -1          ;
			bool            wait_overflow = false       ;
			if (has_timeout) {
				static constexpr time_t WaitMax = Max<int>/1000 - 1 ; // ensure time can be expressed in ms with an int after adding fractional part
				time_t wait_s = end.tv_sec - now.tv_sec ;
				if ((wait_overflow=(wait_s>WaitMax))) wait_s = WaitMax ;
				wait_ms  = wait_s                    * 1'000      ;
				wait_ms += (end.tv_nsec-now.tv_nsec) / 1'000'000l ;   // /!\ protect against possible conversion to time_t which may be unsigned
			} else {
				wait_ms = timeout==Time::Delay::Forever ? -1 : 0 ;
			}
			SWEAR(_n_events<=Max<int>) ;
			cnt_ = ::epoll_wait( _fd , events.data() , int(_n_events) , wait_ms ) ;
			switch (cnt_) {
				case  0 :                                                                                                   // timeout
					if (wait_overflow) ::clock_gettime(CLOCK_MONOTONIC,&now) ;
					else               return {} ;
				break ;
				case -1 :
					SWEAR( errno==EINTR , errno ) ;
				break ;
				default :
					events.resize(cnt_) ;
					if (_n_sigs) {                                                                                          // fast path : avoid looping over events if not necessary
						bool shorten = false ;
						for( Event& e : events ) {
							Fd                        fd        = e.fd()             ; SWEAR(+fd) ;                         // it is non-sense to have an event for non-existent fd
							auto                      it        = _fd_infos.find(fd) ; if (it==_fd_infos.end() ) continue ;
							auto                      [sig,pid] = it->second         ;
							bool                      found     = !pid               ;                                      // if not waiting for a particular pid, event is always ok
							struct ::signalfd_siginfo si        ;
							ssize_t                   n         ;                                                           // we are supposed to read at least once, so init with error case
							while ( (n=::read(fd,&si,sizeof(si)))==sizeof(si) ) {                                           // flush signal fd, including possibly left-over old sigs
								SWEAR( int(si.ssi_signo)==sig , si.ssi_signo,sig ) ;
								found |= pid_t(si.ssi_pid)==pid ;
							}
							SWEAR( n<0 && (errno==EAGAIN||errno==EWOULDBLOCK) , n,fd,errno ) ;                              // fd is non-blocking
							if (!found) { e = {} ; shorten = true ; }                                                       // event is supposed to represent that pid is terminated
						}
						if (shorten) {
							size_t j = 0 ;
							for( size_t i : iota(events.size()) ) {
								if (!events[i]) continue ;
								if (j<i) events[j] = events[i] ;
								j++ ;
							}
							events.resize(j) ;
						}
					}
					return events ;
			}
		}
	}
	// data
private :
	AcFd                                        _fd        ;
	::umap<int/*sig*/,Fd                      > _sig_infos ;
	::umap<Fd        ,::pair<int/*sig*/,pid_t>> _fd_infos  ;
	uint                                        _n_sigs    = 0 ;
	uint                                        _n_waits   = 0 ;
	uint                                        _n_events  = 0 ;
} ;
