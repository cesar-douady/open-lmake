// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>

#include "serialize.hh"
#include "time.hh"

::string host() ;

struct Fd {
	friend ::ostream& operator<<( ::ostream& , Fd const& ) ;
	static const Fd Cwd    ;
	static const Fd Stdin  ;
	static const Fd Stdout ;
	static const Fd Stderr ;
	static const Fd Std    ; // the highest standard fd
	// cxtors & casts
	constexpr Fd(                        ) = default ;
	constexpr Fd( int fd_                ) : fd{fd_} {                         }
	/**/      Fd( int fd_ , bool no_std_ ) : fd{fd_} { if (no_std_) no_std() ; }
	//
	constexpr operator int  () const { return fd      ; }
	constexpr bool operator+() const { return fd>=0   ; }
	constexpr bool operator!() const { return !+*this ; }
	//
	void swap(Fd& fd_) { ::swap(fd,fd_.fd) ; }
	// services
	bool              operator== (Fd const&) const = default ;
	::strong_ordering operator<=>(Fd const&) const = default ;
	void write(::string_view const& data) {
		for( size_t cnt=0 ; cnt<data.size() ;) {
			ssize_t c = ::write(fd,data.data(),data.size()) ;
			if (c<=0) throw "cannot write to "+fmt_string(fd) ;
			cnt += c ;
		}
	}
	Fd             dup   () const { return ::dup(fd) ;                      }
	constexpr Fd   detach()       { Fd res = *this ; fd = -1 ; return res ; }
	constexpr void close () {
		if (!*this        ) return ;
		if (::close(fd)!=0) throw "cannot close file descriptor "+fmt_string(fd)+" : "+strerror(errno) ;
		*this = {} ;
	}
	void no_std() {
		if ( !*this || fd>Std.fd ) return ;
		int new_fd = ::fcntl( fd , F_DUPFD_CLOEXEC , Std.fd+1 ) ;
		swear_prod(new_fd>Std.fd,"cannot duplicate ",fd) ;
		close() ;
		fd = new_fd ;
	}
	void cloexec(bool set=true) {
		::fcntl(fd,F_SETFD,set?FD_CLOEXEC:0) ;
	}
	template<IsStream T> void serdes(T& s) {
		::serdes(s,fd) ;
	}
	// data
	int fd = -1 ;
} ;
constexpr Fd Fd::Cwd   {int(AT_FDCWD)} ;
constexpr Fd Fd::Stdin {0            } ;
constexpr Fd Fd::Stdout{1            } ;
constexpr Fd Fd::Stderr{2            } ;
constexpr Fd Fd::Std   {2            } ;

struct AutoCloseFd : Fd {
	friend ::ostream& operator<<( ::ostream& , AutoCloseFd const& ) ;
	// cxtors & casts
	AutoCloseFd(                              ) = default ;
	AutoCloseFd( Fd fd_                       ) : Fd{fd_        } {              }
	AutoCloseFd( AutoCloseFd&& acfd           )                   { swap(acfd) ; }
	AutoCloseFd( int fd_ , bool no_std_=false ) : Fd{fd_,no_std_} {              }
	//
	~AutoCloseFd() { close() ; }
	//
	AutoCloseFd& operator=(int           fd_ ) { if (fd!=fd_) { close() ; fd = fd_ ; } return *this ; }
	AutoCloseFd& operator=(Fd const&     fd_ ) { *this = fd_ .fd ;                     return *this ; }
	AutoCloseFd& operator=(AutoCloseFd&& acfd) { swap(acfd) ;                          return *this ; }
} ;

struct LockedFd : Fd {
	friend ::ostream& operator<<( ::ostream& , LockedFd const& ) ;
	// cxtors & casts
	LockedFd(                         ) = default ;
	LockedFd( Fd fd_ , bool exclusive ) : Fd{fd_}         { lock(exclusive) ; }
	LockedFd(LockedFd&& lfd           ) : Fd{::move(lfd)} { lfd.detach() ;    }
	//
	~LockedFd() { unlock() ; }
	//
	LockedFd& operator=(LockedFd&& lfd) { fd = lfd.fd ; lfd.detach() ; return *this ; }
	//
	void lock  (bool e) { if (fd>=0) flock(fd,e?LOCK_EX:LOCK_SH) ; }
	void unlock(      ) { if (fd>=0) flock(fd,  LOCK_UN        ) ; }
} ;

static constexpr in_addr_t NoSockAddr = 0x7f000001 ;
struct SockFd : AutoCloseFd {
	friend ::ostream& operator<<( ::ostream& , SockFd const& ) ;
	static constexpr in_addr_t LoopBackAddr = NoSockAddr ;
	// statics
	static ::string s_addr_str(in_addr_t addr) {
		::string res ; res.reserve(15) ;         // 3 digits per level + 5 digits for the port
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
		if (col==Npos) throw "bad service : "+service ;
		return col ;
	}
	// cxtors & casts
public :
	using AutoCloseFd::AutoCloseFd ;
	SockFd(NewType) { init() ; }
	//
	void init() {
		*this = ::socket( AF_INET , SOCK_STREAM|SOCK_CLOEXEC , 0 ) ;
		no_std() ;
	}
	// services
	in_addr_t peer_addr() const {
		static_assert(sizeof(in_addr_t)==4) ;    // else use adequate ntohs/ntohl according to the size
		struct sockaddr_in peer_addr ;
		socklen_t          len       = sizeof(peer_addr)                                                           ;
		int                rc        = ::getpeername( fd , reinterpret_cast<struct sockaddr*>(&peer_addr) , &len ) ;
		SWEAR( rc ==0                 , rc  ) ;
		SWEAR( len==sizeof(peer_addr) , len ) ;
		return ntohl(peer_addr.sin_addr.s_addr) ;
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
	friend ::ostream& operator<<( ::ostream& , SlaveSockFd const& ) ;
	// cxtors & casts
	using SockFd::SockFd ;
} ;

struct ServerSockFd : SockFd {
	// cxtors & casts
	using SockFd::SockFd ;
	ServerSockFd( NewType , int backlog=0 ) { listen(backlog) ; }
	// services
	void listen(int backlog=0) {
		if (!*this  ) init() ;
		if (!backlog) backlog = 100 ;
		int rc = ::listen(fd,backlog) ;
		swear_prod(rc==0,"cannot listen on ",*this," with backlog ",backlog," (",rc,')') ;
	}
	::string service(in_addr_t addr) const { return s_service(addr,port()) ; }
	::string service(              ) const { return s_service(     port()) ; }
	SlaveSockFd accept() ;
} ;

struct ClientSockFd : SockFd {
	// cxtors & casts
	using SockFd::SockFd ;
	template<class... A> ClientSockFd(A&&... args) { connect(::forward<A>(args)...) ; }
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
	template<> struct hash<AutoCloseFd > { size_t operator()(AutoCloseFd  const& fd) const { return fd ; } } ;
	template<> struct hash<SockFd      > { size_t operator()(SockFd       const& fd) const { return fd ; } } ;
	template<> struct hash<SlaveSockFd > { size_t operator()(SlaveSockFd  const& fd) const { return fd ; } } ;
	template<> struct hash<ServerSockFd> { size_t operator()(ServerSockFd const& fd) const { return fd ; } } ;
	template<> struct hash<ClientSockFd> { size_t operator()(ClientSockFd const& fd) const { return fd ; } } ;
}

//
// Epoll
//

struct Epoll {
	struct Event : epoll_event {
		friend ::ostream& operator<<( ::ostream& , Event const& ) ;
		// cxtors & casts
		using epoll_event::epoll_event ;
		Event() { epoll_event::data.u64 = -1 ; }
		// access
		template<class T=uint32_t> T  data() const requires(sizeof(T)<=4) { return T       (epoll_event::data.u64>>32) ; }
		/**/                       Fd fd  () const                        { return uint32_t(epoll_event::data.u64)     ; }
	} ;
	// cxtors & casts
	Epoll (       ) = default ;
	Epoll (NewType) { init () ; }
	// services
	void init() {
		fd = AutoCloseFd( ::epoll_create1(EPOLL_CLOEXEC) , true/*no_std*/ ) ;
	}
	template<class T> void add( bool write , Fd fd_ , T data , bool wait=true ) {
		static_assert(sizeof(T)<=4) ;
		epoll_event event { .events=write?EPOLLOUT:EPOLLIN , .data={.u64=(uint64_t(uint32_t(data))<<32)|uint32_t(fd_) } } ;
		int rc = epoll_ctl( int(fd) , EPOLL_CTL_ADD , int(fd_) , &event ) ;
		swear_prod(rc==0,"cannot add ",fd_," to epoll ",fd," (",strerror(errno),')') ;
		cnt += wait ;
	}
	void del( Fd fd_ , bool wait=true ) {                                                                                                        // wait must be coherent with corresponding add
		int rc = ::epoll_ctl( fd , EPOLL_CTL_DEL , fd_ , nullptr ) ;
		swear_prod(rc==0,"cannot del",fd_,"from epoll",fd,'(',strerror(errno),')') ;
		cnt -= wait ;
	}
	::vector<Event> wait(Time::Delay timeout=Time::Delay::Forever) const ;
	/**/              void add      ( bool write , Fd fd_ ,          bool wait=true ) {               add(write,fd_,0   ,wait) ;               }
	template<class T> void add_read (              Fd fd_ , T data , bool wait=true ) {               add(false,fd_,data,wait) ;               }
	template<class T> void add_write(              Fd fd_ , T data , bool wait=true ) {               add(true ,fd_,data,wait) ;               }
	/**/              void add_read (              Fd fd_ ,          bool wait=true ) {               add(false,fd_,     wait) ;               }
	/**/              void add_write(              Fd fd_ ,          bool wait=true ) {               add(true ,fd_,     wait) ;               }
	/**/              void close    (              Fd fd_ ,          bool wait=true ) { SWEAR(+fd_) ; del(      fd_,     wait) ; fd_.close() ; } // wait must be coherent with corresponding add
	// data
	AutoCloseFd fd  ;
	int         cnt = 0 ;
} ;
::ostream& operator<<( ::ostream& , Epoll::Event const& ) ;
