// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>

#include "serialize.hh"
#include "time.hh"

#ifndef SOCK_CLOEXEC
	#define SOCK_CLOEXEC 0 // CLOEXEC is just defensive programming, not really needed
#endif

::string const& fqdn() ; // fully qualified domain name (includes hostname)

struct Service {
	friend ::string& operator+=( ::string& , Service const& ) ;
	using Key = uint64_t ;
	// cxtors & casts
	Service( in_addr_t a , in_port_t p=0            ) : addr{a} , port{p} {}
	Service(               in_port_t p=0            ) : addr{0} , port{p} {}
	Service( ::string const& s , bool name_ok=false ) ;
	//
	operator ::string() const { return str() ; }
	// access
	bool     operator+(                    ) const { return addr || port ;                                 }
	::string str      (::string const& host) const { ::string res = host ; res <<':'<< port ; return res ; }
	::string str      (                    ) const ;
	// services
	template<IsStream S> void serdes(S& s) {
		::serdes( s , addr,port ) ;
	}
	// data
	in_addr_t addr = 0 ;
	in_port_t port = 0 ;
} ;

struct KeyedService : Service {
	friend ::string& operator+=( ::string& , KeyedService const& ) ;
	// cxtors & casts
	KeyedService() = default ;
	KeyedService( Service s , Key k={}                   ) : Service{s} , key{k} {}
	KeyedService( ::string const& s , bool name_ok=false ) ;
	//
	operator ::string() const { return str() ; }
	// access
	::string str     (::string const& host) const { ::string res = Service::str(host) ; if (key) res <<'/'<< key ; return res ; }
	::string user_str(::string const& host) const { ::string res = Service::str(host) ;                            return res ; }
	::string str     (                    ) const ;
	::string user_str(                    ) const ;
	// services
	template<IsStream S> void serdes(S& s) {
		Service::serdes<S>(s) ;
		::serdes( s , key ) ;
	}
	// data
	Key key = {} ;
} ;

// manage endianness in ::sockaddr_in (which must not be used directly)
struct SockAddr : private ::sockaddr_in { // ensure fields cannot be accessed directly so as to not forget endianness converstion
	// cxtors & casts
	SockAddr( Service s={} ) : ::sockaddr_in{ .sin_family=AF_INET , .sin_port=htons(s.port) , .sin_addr{.s_addr=htonl(s.addr)} , .sin_zero{} } {} // dont prefix with :: as hton* may be macros
	//
	::sockaddr const& as_sockaddr() const { return *::launder(reinterpret_cast<::sockaddr const*>(this)) ; }
	::sockaddr      & as_sockaddr()       { return *::launder(reinterpret_cast<::sockaddr      *>(this)) ; }
	// accesses
	in_port_t port    (           ) const { return ntohs(sin_port       ) ; }                                                                     // dont prefix with :: as ntoh* may be macros
	in_addr_t addr    (           ) const { return ntohl(sin_addr.s_addr) ; }                                                                     // .
	void      set_port(in_port_t p)       { sin_port        = htons(p) ;    }                                                                     // dont prefix with :: as hton* may be macros
	void      set_addr(in_addr_t a)       { sin_addr.s_addr = htonl(a) ;    }                                                                     // .
} ;

struct SockFd : AcFd {
	friend ::string& operator+=( ::string& , SockFd const& ) ;
	using Key = Service::Key ;
	static constexpr in_addr_t   LoopBackAddr      = 0x7f000001                 ;                              // 127.0.0.1
	static constexpr in_addr_t   LoopBackMask      = 0xff000000                 ;
	static constexpr in_addr_t   LoopBackBroadcast = LoopBackAddr|~LoopBackMask ;                              // must be avoided as it is illegal
	static constexpr Time::Delay AddrInUseTick     {    0.010 }                 ;
	static constexpr uint32_t    NAddrInUseTrials  = 1000                       ;
	static constexpr uint32_t    NConnectTrials    =  100                       ;
	static constexpr Time::Delay ConnectTimeout    { 1000     }                 ;
	// statics
	static bool            s_is_loopback    ( in_addr_t a                               ) {                                             return (a&LoopBackMask)==(LoopBackAddr&LoopBackMask) ; }
	static in_addr_t       s_random_loopback(                                           ) ;
	static ::string        s_addr_str       ( in_addr_t                                 ) ;
	static in_addr_t       s_addr           ( ::string const& host , bool name_ok=false ) ;                    // if !name_ok, host must be empty or in dot notation
	static ::string const& s_host           ( in_addr_t                                 ) ;
	static ::string        s_host           ( ::string const& service                   ) { size_t    pos=service.rfind(':')          ; return service.substr(0,pos)                         ; }
	static SockAddr        s_sock_addr      ( Fd    , bool peer                         ) ;
	static in_port_t       s_port           ( Fd fd , bool peer                         ) {                                             return s_sock_addr(fd,peer).port()                   ; }
	static in_addr_t       s_addr           ( Fd fd , bool peer                         ) { in_addr_t a  =s_sock_addr(fd,peer).addr() ; return s_is_loopback(a) ? 0 : a                      ; }
	// cxtors & casts
protected :
	SockFd() = default ;
	SockFd(          Key k , bool reuse_addr , in_addr_t local_addr , bool for_server ) ;                      // for Server and Client
	SockFd( int fd , Key k                                                            ) : AcFd{fd} , key{k} {} // for Slave
	// services
public :
	// if timeout is 0, it means infinity (no timeout)
	void set_receive_timeout(Time::Delay to={}) { Time::TimeVal to_tv(to) ; ::setsockopt( fd , SOL_SOCKET , SO_RCVTIMEO , &to_tv , sizeof(to_tv) ) ; }
	void set_send_timeout   (Time::Delay to={}) { Time::TimeVal to_tv(to) ; ::setsockopt( fd , SOL_SOCKET , SO_SNDTIMEO , &to_tv , sizeof(to_tv) ) ; }
	void set_timeout        (Time::Delay to={}) {
		set_receive_timeout(to) ;
		set_send_timeout   (to) ;
	}
	SockAddr     sock_addr  ( bool peer                        ) const { return s_sock_addr( fd , peer )           ; }
	in_addr_t    addr       ( bool peer                        ) const { return s_addr     ( fd , peer )           ; }
	in_port_t    port       ( bool peer                        ) const { return s_port     ( fd , peer )           ; }
	KeyedService service    ( bool peer , in_addr_t       a    ) const { return { {a,port(peer)} , key }           ; }
	KeyedService service    ( bool peer                        ) const { return service(peer,addr(peer))           ; }
	::string     service_str( bool peer , ::string const& host ) const { return service(peer,0/*addr*/ ).str(host) ; }
	::string     service_str( bool peer                        ) const { return service(peer           ).str(    ) ; }
	// data
	Key key = {} ;
} ;

struct SlaveSockFd : SockFd {
	friend ::string& operator+=( ::string& , SlaveSockFd const& ) ;
	// cxtors & casts
	SlaveSockFd() = default ;
	SlaveSockFd( int fd , Key k={} ) : SockFd{fd,k} {}
} ;

struct ServerSockFd : SockFd {
	friend ::string& operator+=( ::string& , ServerSockFd const& ) ;
	// cxtors & casts
	ServerSockFd() = default ;
	ServerSockFd( int backlog , bool reuse_addr=true , in_addr_t local_addr=0 ) ;
	// services                                                                        peer
	KeyedService service    (in_addr_t       a   ) const { return SockFd::service    ( false , a    ) ; }
	KeyedService service    (                    ) const { return SockFd::service    ( false        ) ; }
	::string     service_str(::string const& host) const { return SockFd::service_str( false , host ) ; }
	::string     service_str(                    ) const { return SockFd::service_str( false        ) ; }
	SlaveSockFd accept() ;
} ;

struct ClientSockFd : SockFd {
	friend ::string& operator+=( ::string& , ClientSockFd const& ) ;
	// cxtors & casts
	ClientSockFd() = default ;
	ClientSockFd( KeyedService , bool reuse_addr=true , Time::Delay timeout={} ) ;
	// services                                                                        peer
	KeyedService service    (in_addr_t       a   ) const { return SockFd::service    ( true , a    ) ; }
	KeyedService service    (                    ) const { return SockFd::service    ( true        ) ; }
	::string     service_str(::string const& host) const { return SockFd::service_str( true , host ) ; }
	::string     service_str(                    ) const { return SockFd::service_str( true        ) ; }
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
	BlockedSig (            ) = default ;
	BlockedSig (BlockedSig&&) = default ;                                                      // copy is meaningless
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
	_Pipe() = default ;
	_Pipe( NewType                            ) { open(             ) ; }
	_Pipe( NewType , int flags , bool no_std_ ) { open(flags,no_std_) ; }
	// services
	void open(                          ) { open(0/*flags*/,false/*no_std*/) ; }
	void open( int flags , bool no_std_ ) {
		int fds[2] ;
		if (::pipe2(fds,flags)!=0) fail_prod( cat("cannot create pipes (flags=0x",to_hex(uint(flags)),") : ",StrErr()) ) ;
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
template<Enum E=NewType/*when_unused*/> struct Epoll ;
template<Enum E> ::string& operator+=( ::string& os , Epoll<E> const& ep ) {
	return os<<"Epoll("<<ep._fd.fd<<','<<ep._n_waits<<')' ;
}
template<Enum E> struct Epoll {
	friend ::string& operator+=<>( ::string& , Epoll const& ) ;
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
	Epoll() = default ;
	Epoll(NewType) { init() ; }
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
		if (::epoll_ctl( _fd , EPOLL_CTL_ADD , fd , &event )!=0) fail_prod("cannot add",fd,"to epoll",_fd,'(',StrErr(),')') ;
		if (wait) _n_waits ++ ;
		/**/      _n_events++ ;
	}
	void del( bool /*write*/ , Fd fd , bool wait=true ) {                                                                                 // wait must be coherent with corresponding add
		if (::epoll_ctl( _fd , EPOLL_CTL_DEL , fd , nullptr/*event*/ )!=0) fail_prod("cannot del",fd,"from epoll",_fd,'(',StrErr(),')') ;
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
	::vector<Event> wait(Time::Pdate timeout) const {
		if      (                        timeout==Time::Pdate::Future ) return wait(Time::Delay::Forever) ;
		else if ( Time::Pdate now{New} ; timeout< now                 ) return wait(Time::Delay()       ) ;
		else                                                            return wait(timeout-now         ) ;
	}
	::vector<Event> wait(Time::Delay timeout=Time::Delay::Forever) const {
		if (!_n_events) {
			SWEAR(timeout<Time::Delay::Forever) ;                     // if we wait for nothing with no timeout, we would block forever
			timeout.sleep_for() ;
			return {} ;
		}
		Time::TimeSpec now         ;
		Time::TimeSpec end         ;
		bool           has_timeout = timeout>Time::Delay() && timeout!=Time::Delay::Forever ;
		if (has_timeout) {
			::clock_gettime(CLOCK_MONOTONIC,&now) ;
			end.tv_sec  = time_t ( now.tv_sec  + timeout.sec()       ) ;
			end.tv_nsec = int32_t( now.tv_nsec + timeout.nsec_in_s() ) ;
			if (end.tv_nsec>=int32_t(1'000'000'000)) {
				end.tv_nsec -= int32_t(1'000'000'000) ;
				end.tv_sec  += 1                      ;
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
				wait_ms  = wait_s * 1'000                                  ;
				wait_ms += time_t( (end.tv_nsec-now.tv_nsec) / 1'000'000 ) ;
			} else {
				wait_ms = timeout==Time::Delay::Forever ? -1 : 0 ;
			}
			SWEAR(_n_events<=Max<int>) ;
			cnt_ = ::epoll_wait( _fd , events.data() , int(_n_events) , wait_ms ) ;
			switch (cnt_) {
				case 0 :                                                                                                    // timeout
					if (wait_overflow) ::clock_gettime(CLOCK_MONOTONIC,&now) ;
					else               return {} ;
				break ;
				case -1 :
					SWEAR( errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR , errno ) ;
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
							SWEAR( n<0 && (errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR) , n,fd,errno ) ;                // fd is non-blocking
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

//
// implementation
//

inline ::string Service     ::str      () const { return str     (SockFd::s_addr_str(addr)) ; }
inline ::string KeyedService::str      () const { return str     (SockFd::s_addr_str(addr)) ; }
inline ::string KeyedService::user_str () const { return user_str(SockFd::s_addr_str(addr)) ; }
