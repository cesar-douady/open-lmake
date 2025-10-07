// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <ifaddrs.h>    // struct ifaddrs, getifaddrs, freeifaddrs
#include <netdb.h>      // NI_NOFQDN, struct addrinfo, getnameinfo, getaddrinfo, freeaddrinfo
#include <sys/socket.h>

#include "fd.hh"

#ifndef HOST_NAME_MAX
	#define HOST_NAME_MAX 255  // SYSv2 limits hostnames to 255 with no macro definition
#endif
#ifndef DOMAIN_NAME_MAX
	#define DOMAIN_NAME_MAX 64 // from man getdomainname
#endif

using namespace Time ;

StaticUniqPtr<::uset<int>> _s_epoll_sigs = new ::uset<int> ;

::string& operator+=( ::string& os , LockedFd     const& fd ) { return fd.append_to_str(os,"LockedFd"    ) ; } // NO_COV
::string& operator+=( ::string& os , SockFd       const& fd ) { return fd.append_to_str(os,"SockFd"      ) ; } // NO_COV
::string& operator+=( ::string& os , SlaveSockFd  const& fd ) { return fd.append_to_str(os,"SlaveSockFd" ) ; } // NO_COV
::string& operator+=( ::string& os , ServerSockFd const& fd ) { return fd.append_to_str(os,"ServerSockFd") ; } // NO_COV
::string& operator+=( ::string& os , ClientSockFd const& fd ) { return fd.append_to_str(os,"ClientSockFd") ; } // NO_COV
::string& operator+=( ::string& os , EventFd      const& fd ) { return fd.append_to_str(os,"EventFd"     ) ; } // NO_COV
::string& operator+=( ::string& os , SignalFd     const& fd ) { return fd.append_to_str(os,"SignalFd"    ) ; } // NO_COV

::string const& host() {
	static ::string s_host = []()->::string {
		char buf[HOST_NAME_MAX+1] ;
		int  rc                   = ::gethostname( buf , sizeof(buf) ) ; SWEAR( rc==0 , errno ) ;
		return buf ;
	}() ;
	return s_host ;
}

::string const& fqdn() {
	static ::string s_fqdn = []() {
		struct addrinfo  hints = {}     ; hints.ai_family = AF_UNSPEC ; hints.ai_flags = AI_CANONNAME ;
		struct addrinfo* ai    ;
		::string         res   = host() ; // default to hostname
		//
		if ( ::getaddrinfo( res.c_str() , nullptr/*service*/ , &hints , &ai )!=0 ) goto Return  ;
		if ( !ai->ai_canonname                                                   ) goto Release ;
		res = ai->ai_canonname ;
	Release :
		::freeaddrinfo(ai) ;
	Return :
		return res ;
	}() ;
	return s_fqdn ;
}

//
// SockFd
//

// manage endianness in ::sockaddr_in (which must not be used directly)
struct SockAddr : private ::sockaddr_in { // ensure fields cannot be accessed directly so as to not forget endianness converstion
	// cxtors & casts
	SockAddr( in_addr_t a=0 , in_port_t p=0 ) : ::sockaddr_in{ .sin_family=AF_INET , .sin_port=htons(p) , .sin_addr{.s_addr=htonl(a)} , .sin_zero{} } {} // dont prefix with :: as hton* may be macros
	SockAddr(                 in_port_t p   ) : ::sockaddr_in{ .sin_family=AF_INET , .sin_port=htons(p) , .sin_addr{.s_addr=htonl(0)} , .sin_zero{} } {} // .
	//
	::sockaddr const& as_sockaddr() const { return *::launder(reinterpret_cast<::sockaddr const*>(this)) ; }
	::sockaddr      & as_sockaddr()       { return *::launder(reinterpret_cast<::sockaddr      *>(this)) ; }
	// accesses
	in_port_t port    (           ) const { return ntohs(sin_port       ) ; }                                                                            // dont prefix with :: as ntoh* may be macros
	in_addr_t addr    (           ) const { return ntohl(sin_addr.s_addr) ; }                                                                            // .
	void      set_port(in_port_t p)       { sin_port        = htons(p) ;    }                                                                            // dont prefix with :: as hton* may be macros
	void      set_addr(in_addr_t a)       { sin_addr.s_addr = htonl(a) ;    }                                                                            // .
} ;

::string SockFd::s_addr_str(in_addr_t addr) {
	if (!addr) return {} ;                    // no address available
	::string res ; res.reserve(15) ;          // 3 digits per level + 5 digits for the port
	res <<      ((addr>>24)&0xff) ;           // dot notation is big endian
	res <<'.'<< ((addr>>16)&0xff) ;
	res <<'.'<< ((addr>> 8)&0xff) ;
	res <<'.'<< ((addr>> 0)&0xff) ;
	return res ;
}

in_addr_t SockFd::s_addr(::string const& server) {
	if (!server) return LoopBackAddr ;
	// by standard dot notation
	{	in_addr_t addr   = 0     ;         // address being decoded
		int       byte   = 0     ;         // ensure component is less than 256
		int       n      = 0     ;         // ensure there are 4 components
		bool      first  = true  ;         // prevent empty components
		bool      first0 = false ;         // prevent leading 0's (unless component is 0)
		for( char c : server ) {
			if (c=='.') {
				if (first) goto ByName ;
				addr  = (addr<<8) | byte ; // dot notation is big endian
				byte  = 0                ;
				first = true             ;
				n++ ;
				continue ;
			}
			if ( c>='0' && c<='9' ) {
				byte = byte*10 + (c-'0') ;
				if      (first    ) { first0 = first && c=='0' ; first  = false ; }
				else if (first0   )   goto ByName ;
				if      (byte>=256)   goto ByName ;
				continue ;
			}
			goto ByName ;
		}
		if (first) goto ByName ;
		if (n!=4 ) goto ByName ;
		return addr ;
	}
ByName :
	{	struct addrinfo  hint = {}                                                      ; hint.ai_family = AF_INET ; hint.ai_socktype = SOCK_STREAM ;
		struct addrinfo* ai   ;
		int              rc   = ::getaddrinfo( server.c_str() , nullptr , &hint , &ai ) ; throw_unless( rc==0 , "cannot get addr of ",server," (",rc,')' ) ;
		in_addr_t        addr = reinterpret_cast<SockAddr*>(ai->ai_addr)->addr()        ;
		freeaddrinfo(ai) ;
		return addr ;
	}
}

::vmap_s<in_addr_t> SockFd::s_addrs_self(::string const& ifce) {
	::vmap_s<in_addr_t> res ;
	struct ifaddrs*     ifa ;
	if (::getifaddrs(&ifa)!=0) return {{{},LoopBackAddr}} ;
	for( struct ifaddrs* p=ifa ; p ; p=p->ifa_next ) {
		if (!( p->ifa_addr && p->ifa_addr->sa_family==AF_INET )) continue ;
		in_addr_t addr = reinterpret_cast<SockAddr*>(p->ifa_addr)->addr() ;
		if (+ifce) { if (p->ifa_name      !=ifce) continue ; }              // searching provided interface
		else       { if (((addr>>24)&0xff)==127 ) continue ; }              // searching for any non-loopback interface
		res.emplace_back(p->ifa_name,addr) ;
	}
	freeifaddrs(ifa) ;
	if (+res ) return res                 ;
	if (+ifce) return {{{},s_addr(ifce)}} ;                                 // ifce may actually be a name
	/**/       return {{{},LoopBackAddr}} ;
}

struct Ports {
	in_port_t first = 0 ;
	in_port_t sz    = 0 ;
} ;
static Ports const& _ports() {
	static Ports s_ports = []() {
		Ports      res   ;
		::vector_s ports = split(AcFd("/proc/sys/net/ipv4/ip_local_port_range").read()) ; SWEAR( ports.size()==2 , ports ) ;
		res.first = from_string<in_port_t>(ports[0])               ;
		res.sz    = from_string<in_port_t>(ports[1])+1 - res.first ; // ephemeral range is specified as "first last" inclusive
		return res ;
	}() ;
	return s_ports ;
}

SockFd::SockFd( NewType , bool reuse_addr ) {
	static constexpr int       True    = 1   ;
	static constexpr in_port_t PortInc = 199 ;                              // a prime number to ensure all ports are tried
	//
	self = ::socket( AF_INET , SOCK_STREAM|SOCK_CLOEXEC , 0/*protocol*/ ) ; no_std() ; if (!self) fail_prod("cannot create socket :",::strerror(errno)) ;
	if (!reuse_addr) return ;
	// if we want to set SO_REUSEADDR, we cannot auto-bind to an ephemeral port as SO_REUSEADDR is not taken into account in that case
	// so we try random port numbers in the ephemeral range until we find a free one
	//
	Ports const& ports = _ports() ;
	SockAddr     sa    ;
	throw_unless( ::setsockopt( fd , SOL_SOCKET , SO_REUSEADDR , &True , sizeof(True) )==0 , "cannot set socket option SO_REUSEADR on ",self ) ;
	in_port_t random   = Pdate(New).hash()              ;
	uint64_t  n_trials = ::bit_ceil<uint64_t>(ports.sz) ;
	in_port_t mask     = n_trials - 1                   ;
	for( uint64_t i : iota(n_trials) ) {                                    // try all ports in random order (need bit_ceil(ports.sz) to ensure all ports are covered with the method below)
		sa.set_port( ports.first + (((i*PortInc)&mask)^random)%ports.sz ) ;
		if ( ::bind( fd , &sa.as_sockaddr() , sizeof(sa))==0 )  return ;
		switch (errno) {
			case EADDRINUSE :
			case EACCES     : break ;
			default         : FAIL(self,::strerror(errno)) ;
		}
	}
	throw cat("cannot bind ",self," : ",::strerror(errno)) ;                // we have trie all ports
}

::string const& SockFd::s_host(in_addr_t a) {                                // implement a cache as getnameinfo implies network access and can be rather long
	static ::umap<in_addr_t,::string> s_tab { {0,""} , {LoopBackAddr,""} } ; // pre-populate to return empty for local accesses
	//
	auto it = s_tab.find(a) ;
	if (it==s_tab.end()) {
		char     buf[HOST_NAME_MAX+1] ;
		SockAddr sa                   { a }                                                                                                              ;
		int      rc                   = ::getnameinfo( &sa.as_sockaddr() , sizeof(sa) , buf , sizeof(buf) , nullptr/*serv*/ , 0/*servlen*/ , NI_NOFQDN ) ;
		if (rc) {
			it = s_tab.emplace(a,"???").first ;
		} else {
			::string host = &buf[0] ; if ( size_t p=host.find('.') ; p!=Npos ) host.resize(p) ;
			it = s_tab.emplace(a,::move(host)).first ;
		}
	}
	return it->second ;
}

in_port_t SockFd::port() const {
	SockAddr  my_addr ;
	socklen_t sz      = sizeof(my_addr)                                    ;
	int       rc      = ::getsockname( fd , &my_addr.as_sockaddr() , &sz ) ;
	SWEAR( rc ==0              , rc,self ) ;
	SWEAR( sz==sizeof(my_addr) , sz,self ) ;
	return my_addr.port() ;
}

in_addr_t SockFd::peer_addr() const {
	SockAddr  peer_addr ;
	socklen_t sz        = sizeof(peer_addr)                                    ;
	int       rc        = ::getpeername( fd , &peer_addr.as_sockaddr() , &sz ) ;
	SWEAR( rc ==0                , rc,self ) ;
	SWEAR( sz==sizeof(peer_addr) , sz,self ) ;
	return peer_addr.addr() ;
}

//
// ServerSockFd
//

ServerSockFd::ServerSockFd( NewType , int backlog , bool reuse_addr ) : SockFd{New,reuse_addr} {
	if (!backlog) backlog = 100 ;
	for( int i=1 ;; i++ ) {
		if ( ::listen(fd,backlog)==0 ) break ;
		SWEAR( errno==EADDRINUSE , self,backlog,reuse_addr ) ;
		if (i>=_ports().sz) throw cat("cannot listen to ",self," : ",::strerror(errno)) ;
		AddrInUseTick.sleep_for() ;
	}
}

SlaveSockFd ServerSockFd::accept() {
	SlaveSockFd slave_fd = ::accept( fd , nullptr/*addr*/ , nullptr/*addrlen*/ ) ;
	swear_prod( +slave_fd , "cannot accept from",self ) ;
	return slave_fd ;
}

//
// ClientSockFd
//

ClientSockFd::ClientSockFd( in_addr_t server , in_port_t port , bool reuse_addr , Time::Delay timeout ) : SockFd{New,reuse_addr} {
	SockAddr sa               { server , port } ;
	Pdate    end              ;                   if (+timeout) end = Pdate(New) + timeout ;
	int      i_reuse_addr = 1 ;
	int      i_connect    = 1 ;
	for( int i=1 ;; i++ ) {
		if (+timeout) {
			Delay::TimeVal to ( ::max( Delay(0.001) , end-Pdate(New) ) ) ;                                                           // ensure to is positive
			::setsockopt( fd , SOL_SOCKET , SO_SNDTIMEO , &to , sizeof(to) ) ;
		}
		if ( ::connect( fd , &sa.as_sockaddr() , sizeof(sa) )==0 ) {
			if (+timeout) ::setsockopt( fd , SOL_SOCKET , SO_SNDTIMEO , &::ref(Delay::TimeVal(Delay())) , sizeof(Delay::TimeVal) ) ; // restore no timeout
			break ;
		}
		switch (errno) {
			case EADDRNOTAVAIL :
				if (i_reuse_addr>=_ports().sz) throw cat("cannot connect to ",self," after ",_ports().sz," trials : ",::strerror(errno)) ;
				i_reuse_addr++ ;
				AddrInUseTick.sleep_for() ;
			break ;
			case ECONNREFUSED :
			case ECONNRESET   :                                                                                                      // although not documented, may happen when server is overloaded
				if (i_connect>=NConnectTrials) throw cat("cannot connect to ",self," after ",NConnectTrials  ," trials : ",::strerror(errno)) ;
				i_connect++ ;
			break ;
			case EINTR :
			break ;
			case ETIMEDOUT :
				if (Pdate(New)>end) throw cat("cannot connect to ",self," : ",::strerror(errno)) ;
				break ;
			default :
				FAIL(self,server,port,timeout,reuse_addr,::strerror(errno)) ;
		}
	}
}

