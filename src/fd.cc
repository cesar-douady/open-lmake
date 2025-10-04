// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <ifaddrs.h> // struct ifaddrs, getifaddrs, freeifaddrs
#include <netdb.h>   // NI_NOFQDN, struct addrinfo, getnameinfo, getaddrinfo, freeaddrinfo

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

::string host() {
	char buf[HOST_NAME_MAX+1] ;
	int  rc                   = ::gethostname( buf , sizeof(buf) ) ; SWEAR( rc==0 , errno ) ;
	return buf ;
}

::string fqdn() {
	struct addrinfo  hints = {}     ; hints.ai_family = AF_UNSPEC ; hints.ai_flags = AI_CANONNAME ;
	struct addrinfo* ai    ;
	::string         fqdn  = host() ; // default to hostname
	//
	if ( ::getaddrinfo( fqdn.c_str() , nullptr/*service*/ , &hints , &ai )!=0 ) goto Return ;
	if ( !ai->ai_canonname                                                    ) goto Return ;
	fqdn = ai->ai_canonname ;
Return :
	::freeaddrinfo(ai) ;
	return fqdn ;
}

//
// SockFd
//

::string const& SockFd::s_host(in_addr_t a) {                                // implement a cache as getnameinfo implies network access and can be rather long
	static ::umap<in_addr_t,::string> s_tab { {0,""} , {LoopBackAddr,""} } ; // pre-populate to return empty for local accesses
	//
	auto it = s_tab.find(a) ;
	if (it==s_tab.end()) {
		char                 buf[HOST_NAME_MAX+1] ;
		struct ::sockaddr_in sa                   = s_sockaddr(a,0)                                                                                                                       ;
		int                  rc                   = ::getnameinfo( reinterpret_cast<sockaddr*>(&sa) , sizeof(sockaddr) , buf , sizeof(buf) , nullptr/*serv*/ , 0/*servlen*/ , NI_NOFQDN ) ;
		if (rc) {
			it = s_tab.emplace(a,"???").first ;
		} else {
			::string host = &buf[0] ; if ( size_t p=host.find('.') ; p!=Npos ) host.resize(p) ;
			it = s_tab.emplace(a,::move(host)).first ;
		}
	}
	return it->second ;
}

SockFd::SockFd( NewType , bool reuse_addr ) {
	static constexpr int       True    = 1   ;
	static constexpr in_port_t PortInc = 199 ;                                                            // a prime number to ensure all ports are tried
	//
	self = ::socket( AF_INET , SOCK_STREAM|SOCK_CLOEXEC , 0/*protocol*/ ) ; no_std() ; if (!self) fail_prod("cannot create socket :",::strerror(errno)) ;
	if (!reuse_addr) return ;
	// if we want to set SO_REUSEADDR, we cannot auto-bind to an ephemeral port as SO_REUSEADDR is not taken into account in that case
	// so we try random port numbers in the ephemeral range until we find a free one
	static ::pair<in_port_t/*first*/,in_port_t/*sz*/> s_ports = []() {
		::pair<in_port_t/*first*/,in_port_t/*sz*/> res   ;
		::vector_s                                 ports = split(AcFd("/proc/sys/net/ipv4/ip_local_port_range").read()) ; SWEAR( ports.size()==2 , ports ) ;
		res.first  = from_string<in_port_t>(ports[0])               ;
		res.second = from_string<in_port_t>(ports[1])+1 - res.first ;                                     //ephemeral range is specified as "first last" inclusive
		SWEAR( res.second>PortInc , res ) ;                                                               // else we need to reduce PortInc
		return res ;
	}() ;
	//
	struct ::sockaddr_in sa {
			.sin_family = AF_INET
		,	.sin_port   = 0
		,	.sin_addr   = { .s_addr=0 }
		,	.sin_zero   = {}
	} ;
	throw_unless( ::setsockopt( fd , SOL_SOCKET , SO_REUSEADDR , &True , sizeof(True) )==0 , "cannot set socket option SO_REUSEADR on ",self ) ;
	in_port_t trial_port = s_ports.first + Pdate(New).hash()%s_ports.second ;
	for( int i=1 ;; i++ ) {
		sa.sin_port = trial_port ;
		if ( ::bind( fd , &reinterpret_cast<struct sockaddr const&>(sa) , sizeof(sa) )==0 ) break ;
		switch (errno) {
			case EADDRINUSE :
			case EACCES     : break ;
			default         : FAIL(self,::strerror(errno)) ;
		}
		if (i>=NAddrInUseTrials) throw cat("cannot bind ",self," : ",::strerror(errno)) ;
		if (trial_port<s_ports.first+s_ports.second-PortInc) trial_port += PortInc                  ; // increment while staying within ephemeral range ...
		else                                                 trial_port -= s_ports.second - PortInc ; // ... and it is seems to be more efficient than incrementing by 1 ...
	}                                                                                                 // ... and curiously more efficient than successive random numbers as well
}

//
// ServerSockFd
//

ServerSockFd::ServerSockFd( NewType , int backlog , bool reuse_addr ) : SockFd{New,reuse_addr} {
	if (!backlog) backlog = 100 ;
	for( int i=1 ;; i++ ) {
		if ( ::listen(fd,backlog)==0 ) break ;
		SWEAR( errno==EADDRINUSE , self,backlog,reuse_addr ) ;
		if (i>=NAddrInUseTrials) throw cat("cannot listen to ",self," : ",::strerror(errno)) ;
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
	struct ::sockaddr_in sa               = s_sockaddr(server,port) ;
	Pdate                end              ;                           if (+timeout) end = Pdate(New) + timeout ;
	int                  i_addr_reuse = 1 ;
	int                  i_connect    = 1 ;
	for( int i=1 ;; i++ ) {
		if (+timeout) {
			Delay::TimeVal to ( ::max( Delay(0.001) , end-Pdate(New) ) ) ;                                                           // ensure to is positive
			::setsockopt( fd , SOL_SOCKET , SO_SNDTIMEO , &to , sizeof(to) ) ;
		}
		if ( ::connect( fd , reinterpret_cast<sockaddr*>(&sa) , sizeof(sa) )==0 ) {
			if (+timeout) ::setsockopt( fd , SOL_SOCKET , SO_SNDTIMEO , &::ref(Delay::TimeVal(Delay())) , sizeof(Delay::TimeVal) ) ; // restore no timeout
			break ;
		}
		switch (errno) {
			case EADDRNOTAVAIL :
				if (i_addr_reuse>=NAddrInUseTrials) throw cat("cannot connect to ",self," after ",NAddrInUseTrials," trials : ",::strerror(errno)) ;
				i_addr_reuse++ ;
				AddrInUseTick.sleep_for() ;
			break ;
			case ECONNREFUSED :
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

in_addr_t SockFd::s_addr(::string const& server) {
	if (!server) return LoopBackAddr ;
	// by standard dot notation
	{	in_addr_t addr   = 0     ;                                                                             // address being decoded
		int       byte   = 0     ;                                                                             // ensure component is less than 256
		int       n      = 0     ;                                                                             // ensure there are 4 components
		bool      first  = true  ;                                                                             // prevent empty components
		bool      first0 = false ;                                                                             // prevent leading 0's (unless component is 0)
		for( char c : server ) {
			if (c=='.') {
				if (first) goto ByName ;
				addr  = (addr<<8) | byte ;                                                                     // dot notation is big endian
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
	{	struct addrinfo  hint = {}                                                                           ; hint.ai_family = AF_INET ; hint.ai_socktype = SOCK_STREAM ;
		struct addrinfo* ai   ;
		int              rc   = ::getaddrinfo( server.c_str() , nullptr , &hint , &ai )                      ; throw_unless( rc==0 , "cannot get addr of ",server," (",rc,')' ) ;
		in_addr_t        addr = ntohl(reinterpret_cast<struct ::sockaddr_in*>(ai->ai_addr)->sin_addr.s_addr) ; // dont prefix with :: as ntohl may be a macro
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
		in_addr_t addr = ntohl(reinterpret_cast<struct ::sockaddr_in*>(p->ifa_addr)->sin_addr.s_addr) ; // dont prefix with :: as ntohl may be a macro
		if (+ifce) { if (p->ifa_name      !=ifce) continue ; }                                          // searching provided interface
		else       { if (((addr>>24)&0xff)==127 ) continue ; }                                          // searching for any non-loopback interface
		res.emplace_back(p->ifa_name,addr) ;
	}
	freeifaddrs(ifa) ;
	if (+res ) return res                 ;
	if (+ifce) return {{{},s_addr(ifce)}} ;                                                             // ifce may actually be a name
	/**/       return {{{},LoopBackAddr}} ;
}

