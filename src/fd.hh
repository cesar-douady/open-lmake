// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
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

struct AcFd : Fd {
	friend ::string& operator+=( ::string& , AcFd const& ) ;
	// cxtors & casts
	AcFd(                                                                          ) = default ;
	AcFd( Fd fd_                                                                   ) : Fd{fd_                            } {              }
	AcFd( AcFd&& acfd                                                              )                                       { swap(acfd) ; }
	AcFd( int fd_                                             , bool no_std_=false ) : Fd{fd_,no_std_                    } {              }
	AcFd(         ::string const& file , FdAction action=Read , bool no_std_=false ) : Fd{       file , action , no_std_ } {              }
	AcFd( Fd at , ::string const& file , FdAction action=Read , bool no_std_=false ) : Fd{ at  , file , action , no_std_ } {              }
	//
	~AcFd() { close() ; }
	//
	AcFd& operator=(int       fd_ ) { if (fd!=fd_) { close() ; fd = fd_ ; } return self ; }
	AcFd& operator=(Fd const& fd_ ) { self = fd_ .fd ;                      return self ; }
	AcFd& operator=(AcFd   && acfd) { swap(acfd) ;                          return self ; }
} ;

struct LockedFd : Fd {
	friend ::string& operator+=( ::string& , LockedFd const& ) ;
	// cxtors & casts
	LockedFd(                         ) = default ;
	LockedFd( Fd fd_ , bool exclusive ) : Fd{fd_}         { lock(exclusive) ; }
	LockedFd(LockedFd&& lfd           ) : Fd{::move(lfd)} { lfd.detach() ;    }
	//
	~LockedFd() { unlock() ; }
	//
	LockedFd& operator=(LockedFd&& lfd) { fd = lfd.fd ; lfd.detach() ; return self ; }
	//
	void lock  (bool e) { if (fd>=0) flock(fd,e?LOCK_EX:LOCK_SH) ; }
	void unlock(      ) { if (fd>=0) flock(fd,  LOCK_UN        ) ; }
} ;

static constexpr in_addr_t NoSockAddr = 0x7f000001 ;
struct SockFd : AcFd {
	friend ::string& operator+=( ::string& , SockFd const& ) ;
	static constexpr in_addr_t LoopBackAddr = NoSockAddr ;
	// statics
	static ::string s_addr_str(in_addr_t addr) {
		::string res ; res.reserve(15) ;          // 3 digits per level + 5 digits for the port
		res <<      ((addr>>24)&0xff) ;
		res <<'.'<< ((addr>>16)&0xff) ;
		res <<'.'<< ((addr>> 8)&0xff) ;
		res <<'.'<< ((addr>> 0)&0xff) ;
		return res ;
	}
	static struct sockaddr_in  s_sockaddr( in_addr_t a , in_port_t p ) {
		struct sockaddr_in res {
			.sin_family = AF_INET
		,	.sin_port   = htons(p)
		,	.sin_addr   = { .s_addr=htonl(a) }
		,	.sin_zero   = {}
		} ;
		return res ;
	}
	static ::string const&     s_host     (in_addr_t              ) ;
	static ::string            s_host     (::string const& service) { size_t col = _s_col(service) ; return   service.substr(0,col)                                                   ; }
	static in_port_t           s_port     (::string const& service) { size_t col = _s_col(service) ; return                           from_string<in_port_t>(service.c_str()+col+1)   ; }
	static ::pair_s<in_port_t> s_host_port(::string const& service) { size_t col = _s_col(service) ; return { service.substr(0,col) , from_string<in_port_t>(service.c_str()+col+1) } ; }
	static in_addr_t           s_addr     (::string const& server ) ;
	//
	static ::string s_service( ::string const& host , in_port_t port ) { return host+':'+port                    ; }
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
		self = ::socket( AF_INET , SOCK_STREAM|SOCK_CLOEXEC , 0 ) ;
		no_std() ;
	}
	// services
	void set_receive_timeout(Time::Delay to) { Time::Pdate::TimeVal to_tv(to) ; ::setsockopt( fd , SOL_SOCKET , SO_RCVTIMEO , &to_tv , sizeof(to_tv) ) ; }
	void set_send_timeout   (Time::Delay to) { Time::Pdate::TimeVal to_tv(to) ; ::setsockopt( fd , SOL_SOCKET , SO_SNDTIMEO , &to_tv , sizeof(to_tv) ) ; }
	void set_timeout        (Time::Delay to) {
		set_receive_timeout(to) ;
		set_send_timeout   (to) ;
	}
	in_addr_t peer_addr() const {
		static_assert(sizeof(in_addr_t)==4) ;     // else use adequate ntohs/ntohl according to the size
		struct sockaddr_in peer_addr ;
		socklen_t          len       = sizeof(peer_addr)                                                           ;
		int                rc        = ::getpeername( fd , reinterpret_cast<struct sockaddr*>(&peer_addr) , &len ) ;
		SWEAR( rc ==0                 , rc  ) ;
		SWEAR( len==sizeof(peer_addr) , len ) ;
		return ntohl(peer_addr.sin_addr.s_addr) ; // dont prefix with :: as ntohl may be a macro
	}
	in_port_t port() const {
		struct sockaddr_in my_addr ;
		socklen_t          len     = sizeof(my_addr)                                                           ;
		int                rc      = ::getsockname( fd , reinterpret_cast<struct sockaddr*>(&my_addr) , &len ) ;
		SWEAR( rc ==0               , rc  ) ;
		SWEAR( len==sizeof(my_addr) , len ) ;
		return ntohs(my_addr.sin_port) ;
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
	// cxtors & casts
	using SockFd::SockFd ;
	ClientSockFd( in_addr_t       server , in_port_t port , int n_trials=1 , Time::Delay timeout={} ) { connect(server,port,n_trials,timeout) ; }
	ClientSockFd( ::string const& server , in_port_t port , int n_trials=1 , Time::Delay timeout={} ) { connect(server,port,n_trials,timeout) ; }
	ClientSockFd( ::string const& service                 , int n_trials=1 , Time::Delay timeout={} ) { connect(service    ,n_trials,timeout) ; }
	// services
	void connect( in_addr_t       server , in_port_t port , int n_trials=1 , Time::Delay timeout={} ) ;
	void connect( ::string const& server , in_port_t port , int n_trials=1 , Time::Delay timeout={} ) {
		connect( s_addr(server) , port , n_trials , timeout ) ;
	}
	void connect( ::string const& service , int n_trials=1 , Time::Delay timeout={} ) {
		::pair_s<in_port_t> host_port = s_host_port(service) ;
		connect( host_port.first , host_port.second , n_trials , timeout ) ;
	}
} ;

namespace std {
	template<> struct hash<Fd          > { size_t operator()(Fd           const& fd) const { return fd ; } } ;
	template<> struct hash<AcFd        > { size_t operator()(AcFd         const& fd) const { return fd ; } } ;
	template<> struct hash<SockFd      > { size_t operator()(SockFd       const& fd) const { return fd ; } } ;
	template<> struct hash<SlaveSockFd > { size_t operator()(SlaveSockFd  const& fd) const { return fd ; } } ;
	template<> struct hash<ServerSockFd> { size_t operator()(ServerSockFd const& fd) const { return fd ; } } ;
	template<> struct hash<ClientSockFd> { size_t operator()(ClientSockFd const& fd) const { return fd ; } } ;
}

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
	swear( ::pthread_sigmask(0,nullptr,&old_mask)==0 , "cannot get sig ",sig ) ;
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

struct Pipe {
	// cxtors & casts
	Pipe(                          ) = default ;
	Pipe(NewType,bool no_std_=false) { open(no_std_) ; }
	// services
	void open(bool no_std_=false) {
		int fds[2] ;
		swear_prod( ::pipe(fds)==0 , "cannot create pipes" ) ;
		read  = {fds[0],no_std_} ;
		write = {fds[1],no_std_} ;
	}
	void close() {
		read .close() ;
		write.close() ;
	}
	void no_std() {
		read .no_std() ;
		write.no_std() ;
	}
	// data
	Fd read  ; // read  side of the pipe
	Fd write ; // write side of the pipe
} ;

//
// EventFd
//

struct EventFd : AcFd {
	EventFd(NewType) : AcFd{::eventfd(0,O_CLOEXEC),true/*no_std*/} {}
	EventFd(Fd fd_ ) : AcFd{fd_                                  } {}
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
// Epoll
//

extern ::uset<int>* _s_epoll_sigs ; // use pointer to avoid troubles when freeing at end of execution, cannot wait for the same signal on several instances
template<StdEnum E> struct _Epoll {
	struct Event : ::epoll_event {
		// cxtors & casts
		using ::epoll_event::epoll_event ;
		Event(                             ) : ::epoll_event{ .events=0                      , .data{.u64=                     uint32_t(Fd())} } {}
		Event( bool write , Fd fd , E data ) : ::epoll_event{ .events=write?EPOLLOUT:EPOLLIN , .data{.u64=(uint64_t(data)<<32)|uint32_t(fd  )} } {}
		// access
		bool operator+(                ) const { return +fd()                                 ; }
		int sig       (_Epoll const& ep) const { return ep._fd_infos.at(fd()).first           ; }
		Fd  fd        (                ) const { return uint32_t(::epoll_event::data.u64    ) ; }
		E   data      (                ) const { return E       (::epoll_event::data.u64>>32) ; }
	} ;
	// cxtors & casts
	~_Epoll() {
		for( auto [fd,sig_pid] : _fd_infos ) _s_epoll_sigs->erase(sig_pid.first) ;
	}
	// services
	void init() {
		_fd = AcFd( ::epoll_create1(EPOLL_CLOEXEC) , true/*no_std*/ ) ;
	}
	void add( bool write , Fd fd , E data ) {
		Event event { write , fd , data } ;
		if (::epoll_ctl( _fd , EPOLL_CTL_ADD , fd , &event )<0) fail_prod("cannot add",fd,"to epoll",_fd,'(',::strerror(errno),')') ;
	}
	void del( bool /*write*/ , Fd fd ) {
		if (::epoll_ctl( _fd , EPOLL_CTL_DEL , fd , nullptr )<0) fail_prod("cannot del",fd,"from epoll",_fd,'(',::strerror(errno),')') ;
	}
	void add_sig( int sig , E data , pid_t pid=0 ) {
		SWEAR(is_blocked_sig(sig)) ;
		::sigset_t sig_set  ;                                                          sigemptyset(&sig_set) ; sigaddset(&sig_set,sig) ; // sigemptyset and sigaddset can be macros
		Fd         fd       = ::signalfd( -1 , &sig_set , SFD_CLOEXEC|SFD_NONBLOCK ) ;
		bool       inserted = _sig_infos   . try_emplace(sig,fd     ).second          ; SWEAR(inserted,fd,sig    ) ;
		/**/       inserted = _fd_infos    . try_emplace(fd ,sig,pid).second          ; SWEAR(inserted,fd,sig,pid) ;
		/**/       inserted = _s_epoll_sigs->insert     (sig        ).second          ; SWEAR(inserted,fd,sig    ) ;
		add( false/*write*/ , fd , data ) ;
		_n_sigs++ ;
	}
	void del_sig(int sig) {
		auto it = _sig_infos.find(sig) ; SWEAR(it!=_sig_infos.end(),sig) ;
		SWEAR(_n_sigs) ; _n_sigs-- ;
		del( false/*write*/ , it->second ) ;
		_fd_infos    . erase(it->second) ;                          // must be done before next line as it is not valid after erasing it
		_sig_infos   . erase(it        ) ;
		_s_epoll_sigs->erase(sig       ) ;
	}
	void add_pid( pid_t pid , E data ) { add_sig(SIGCHLD,data,pid) ; }
	void del_pid (pid_t              ) { del_sig(SIGCHLD         ) ; }
	::vector<Event> wait( Time::Delay timeout , uint cnt ) const {
		struct ::timespec now ;
		struct ::timespec end ;
		bool has_timeout = timeout>Time::Delay() && timeout!=Time::Delay::Forever ;
		if (has_timeout) {
			::clock_gettime(CLOCK_MONOTONIC,&now) ;
			end.tv_sec  = now.tv_sec  + timeout.sec()       ;
			end.tv_nsec = now.tv_nsec + timeout.nsec_in_s() ;
			if (end.tv_nsec>=1'000'000'000l) {
				end.tv_nsec -= 1'000'000'000l ;
				end.tv_sec  += 1              ;
			}
		}
		for(;;) {                                                   // manage case where timeout is longer than the maximum allowed timeout by looping over partial timeouts
			::vector<Event> events        ( cnt ) ;
			int             cnt_          ;
			int             wait_ms       = -1    ;
			bool            wait_overflow = false ;
			if (has_timeout) {
				time_t wait_s   = end.tv_sec - now.tv_sec               ;
				time_t wait_max = ::numeric_limits<int>::max()/1000 - 1 ;
				if ((wait_overflow=(wait_s>wait_max))) wait_s = wait_max ;
				wait_ms  = wait_s                    * 1'000      ;
				wait_ms += (end.tv_nsec-now.tv_nsec) / 1'000'000l ; // protect against possible conversion to time_t which may be unsigned
			} else {
				wait_ms = +timeout ? -1 : 0 ;
			}
			cnt_ = ::epoll_wait( _fd , events.data() , int(cnt) , wait_ms ) ;
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
								SWEAR(int(si.ssi_signo)==sig,si.ssi_signo,sig) ;
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
} ;

template<StdEnum E=NewType/*when_unused*/> struct Epoll : _Epoll<E> {
	using Base  = _Epoll<E>            ;
	using Event = typename Base::Event ;
	using Base::init ;
	// cxtors & casts
	Epoll(       ) = default ;
	Epoll(NewType) { init () ; }
	// accesses
	uint operator+() const { return _cnt ;            }
	void dec      ()       { SWEAR(_cnt>0) ; _cnt-- ; }
	// services
	void add    ( bool write , Fd    fd  , E data={} , bool wait=true ) { Base::add    (write,fd ,data) ;                 _cnt += wait ; }
	void del    ( bool write , Fd    fd  ,             bool wait=true ) { Base::del    (write,fd      ) ; SWEAR(_cnt>0) ; _cnt -= wait ; } // wait must be coherent with corresponding add
	void add_sig(              int   sig , E data={} , bool wait=true ) { Base::add_sig(      sig,data) ;                 _cnt += wait ; }
	void del_sig(              int   sig ,             bool wait=true ) { Base::del_sig(      sig     ) ; SWEAR(_cnt>0) ; _cnt -= wait ; } // wait must be coherent with corresponding add_sig
	void add_pid(              pid_t pid , E data={} , bool wait=true ) { Base::add_pid(      pid,data) ;                 _cnt += wait ; }
	void del_pid(              pid_t pid ,             bool wait=true ) { Base::del_pid(      pid     ) ; SWEAR(_cnt>0) ; _cnt -= wait ; } // wait must be coherent with corresponding add_sig
	//
	void add_read (              Fd fd , E data={} , bool wait=true ) {              add(false,fd,data,wait) ;              }
	void add_write(              Fd fd , E data={} , bool wait=true ) {              add(true ,fd,data,wait) ;              }
	void close    ( bool write , Fd fd ,             bool wait=true ) { SWEAR(+fd) ; del(write,fd,     wait) ; fd.close() ; }              // wait must be coherent with corresponding add
	//
	::vector<Event> wait(Time::Delay timeout=Time::Delay::Forever) const {
		if (!_cnt) {
			SWEAR(timeout<Time::Delay::Forever) ; // if we wait for nothing with no timeout, we would block forever
			timeout.sleep_for() ;
			return {} ;
		}
		return Base::wait(timeout,_cnt) ;
	}
	// data
private :
	uint _cnt = 0 ;
} ;
