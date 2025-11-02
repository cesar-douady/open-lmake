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

static constexpr bool ReuseAddr = false ; // XXX : need to do some trials to know if this is still required now that we have randomization of local communications

StaticUniqPtr<::uset<int>> _s_epoll_sigs = new ::uset<int> ;

::string& operator+=( ::string& os , SockFd       const& fd ) { return fd.append_to_str(os,"SockFd"      ) ; } // NO_COV
::string& operator+=( ::string& os , SlaveSockFd  const& fd ) { return fd.append_to_str(os,"SlaveSockFd" ) ; } // NO_COV
::string& operator+=( ::string& os , ServerSockFd const& fd ) { return fd.append_to_str(os,"ServerSockFd") ; } // NO_COV
::string& operator+=( ::string& os , ClientSockFd const& fd ) { return fd.append_to_str(os,"ClientSockFd") ; } // NO_COV
::string& operator+=( ::string& os , EventFd      const& fd ) { return fd.append_to_str(os,"EventFd"     ) ; } // NO_COV
::string& operator+=( ::string& os , SignalFd     const& fd ) { return fd.append_to_str(os,"SignalFd"    ) ; } // NO_COV

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

struct Ports {
	in_port_t first = 0 ;
	in_port_t sz    = 0 ;
} ;
static Ports const& _ports() {
	static Ports s_ports = []() {
		Ports      res   ;
		::vector_s ports = split(AcFd("/proc/sys/net/ipv4/ip_local_port_range").read()) ; SWEAR( ports.size()==2 , ports ) ;
		res.first  = from_string<in_port_t>(ports[0])               ;
		res.sz     = from_string<in_port_t>(ports[1])+1 - res.first ; // ephemeral range is specified as "first last" inclusive
		res.sz    /= 2                                              ; // only use half to ensure the other half is left for use by other services unrelate to lmake
		return res ;
	}() ;
	return s_ports ;
}

in_addr_t SockFd::s_random_loopback() {
	in_addr_t a = (LoopBackAddr&LoopBackMask) | (Pdate(New).hash()&~LoopBackMask) ;
	if (a==(LoopBackAddr|~LoopBackMask)) return LoopBackAddr ;                      // never generate broadcast address as it is not routable
	else                                 return a            ;
}

::string SockFd::s_addr_str(in_addr_t addr) {
	if (!addr              ) return {} ;      // no address available
	if (s_is_loopback(addr)) return {} ;
	//
	::string res ; res.reserve(15) ;          // 3 digits per level + 5 digits for the port
	res <<      ((addr>>24)&0xff) ;           // dot notation is big endian
	res <<'.'<< ((addr>>16)&0xff) ;
	res <<'.'<< ((addr>> 8)&0xff) ;
	res <<'.'<< ((addr>> 0)&0xff) ;
	return res ;
}

in_addr_t SockFd::s_addr(::string const& addr_str) {
	if (!addr_str) return 0 ;
	// by standard dot notation
	{	in_addr_t addr   = 0     ;         // address being decoded
		int       byte   = 0     ;         // ensure component is less than 256
		int       n      = 0     ;         // ensure there are 4 components
		bool      first  = true  ;         // prevent empty components
		bool      first0 = false ;         // prevent leading 0's (unless component is 0)
		for( char c : addr_str ) {
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
		if (first              ) goto ByName ;
		if (n!=4               ) goto ByName ;
		if (s_is_loopback(addr)) return 0    ;
		else                     return addr ;
	}
ByName :
	{	struct addrinfo  hint = {}                                                        ; hint.ai_family = AF_INET ; hint.ai_socktype = SOCK_STREAM ;
		struct addrinfo* ai   ;
		int              rc   = ::getaddrinfo( addr_str.c_str() , nullptr , &hint , &ai ) ; if (rc!=0) throw cat("cannot get addr of ",addr_str," (",::gai_strerror(rc),')') ;
		in_addr_t        addr = reinterpret_cast<SockAddr*>(ai->ai_addr)->addr()          ;
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
		if (+ifce)               { if (p->ifa_name!=ifce  ) continue ; }    // searching provided interface
		else                     { if (s_is_loopback(addr)) continue ; }    // searching for any non-loopback interface
		if (s_is_loopback(addr))   addr = 0 ;
		res.emplace_back(p->ifa_name,addr) ;
	}
	freeifaddrs(ifa) ;
	if (+res ) return res                 ;
	if (+ifce) return {{{},s_addr(ifce)}} ;                                 // ifce may actually be a name
	/**/       return {{{},0           }} ;
}

::string const& SockFd::s_host(in_addr_t a) { // implement a cache as getnameinfo implies network access and can be rather long
	static ::umap<in_addr_t,::string> s_tab ;
	static ::string                   empty ;
	//
	if (             !a ) return empty ;
	if (s_is_loopback(a)) return empty ;
	//
	auto it = s_tab.find(a) ;
	if (it==s_tab.end()) {
		char     buf[HOST_NAME_MAX+1] ;
		SockAddr sa                   { a }                                                                                                              ;
		int      rc                   = ::getnameinfo( &sa.as_sockaddr() , sizeof(sa) , buf , sizeof(buf) , nullptr/*serv*/ , 0/*servlen*/ , NI_NOFQDN ) ;
		if (rc) {
			it = s_tab.emplace(a,s_addr_str(a)).first ;
		} else {
			::string host = &buf[0] ; if ( size_t p=host.find('.') ; p!=Npos ) host.resize(p) ;
			it = s_tab.emplace(a,::move(host)).first ;
		}
	}
	return it->second ;
}

SockFd::SockFd( NewType , bool reuse_addr , in_addr_t local_addr , bool for_server ) {
	static constexpr in_port_t PortInc = 199 ;                                                                                                    // a prime number to ensure all ports are tried
	//
	self = ::socket( AF_INET , SOCK_STREAM|SOCK_CLOEXEC , 0/*protocol*/ ) ; no_std() ; if (!self) fail_prod("cannot create socket : ",StrErr()) ;
	if (!(ReuseAddr&&reuse_addr)) return ;                                                                                                        // dont reuse addr
	// if we want to set SO_REUSEADDR, we cannot auto-bind to an ephemeral port as SO_REUSEADDR is not taken into account in that case
	// so we try random port numbers in the ephemeral range until we find a free one
	//
	static in_port_t s_port_hint = 0 ;                        // only used for client
	//
	Ports const& ports = _ports()     ;
	SockAddr     sa    { local_addr } ;
	throw_unless( ::setsockopt( fd , SOL_SOCKET , SO_REUSEADDR , &::ref<int>(1) , sizeof(int) )==0 , "cannot set socket option SO_REUSEADR on ",self ) ;
	in_port_t random   = Pdate(New).hash()              ;
	uint64_t  n_trials = ::bit_ceil<uint64_t>(ports.sz) ;
	in_port_t mask     = n_trials - 1                   ;
	for( uint64_t i : iota(n_trials+1) ) {                    // try s_port_hint first, then all ports in random order (need bit_ceil(ports.sz) to ensure all ports are covered with the method below)
		in_port_t trial_port = for_server ? 0 : s_port_hint ;
		if      ( i!=0       ) trial_port = ports.first + (((i*PortInc)&mask)^random)%ports.sz ;
		else if ( !for_server && !trial_port ) continue ;                                        // no port hint, nothing to try first
		sa.set_port(trial_port) ;
		if ( ::bind( fd , &sa.as_sockaddr() , sizeof(sa))==0 ) {
			s_port_hint = trial_port ;
			return ;
		}
		switch (errno) {
			case EADDRINUSE :
			case EACCES     : break ;
			default         : FAIL(self,StrErr()) ;
		}
	}
	throw cat("cannot bind ",self," : ",StrErr()) ;                                              // we have trie all ports
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
	//
	if ( in_addr_t addr=peer_addr.addr() ; s_is_loopback(addr) ) return 0    ;
	else                                                         return addr ;
}

//
// ServerSockFd
//

ServerSockFd::ServerSockFd( NewType , int backlog , bool reuse_addr , in_addr_t local_addr ) : SockFd{ New , reuse_addr , local_addr , true/*for_server*/ } {
	if (!backlog) backlog = 100 ;
	for( uint32_t i=1 ;; i++ ) {
		if ( ::listen(fd,backlog)==0 ) break ;
		SWEAR( errno==EADDRINUSE , self,backlog,reuse_addr ) ;
		if (i>=NAddrInUseTrials) throw cat("cannot listen as ",local_addr?s_addr_str(local_addr):"any"s," : ",StrErr()) ;
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

ClientSockFd::ClientSockFd( in_addr_t server , in_port_t port , bool reuse_addr , Delay timeout ) : SockFd{ New , reuse_addr , server?0:s_random_loopback()/*local_addr*/ , false/*for_server*/ } {
	bool has_timeout = +timeout ;
	if (!server ) server  = s_random_loopback() ;
	if (!timeout) timeout = ConnectTimeout      ;
	//
	SockAddr sa           { server , port }      ;
	Pdate    end          = Pdate(New) + timeout ;
	uint32_t i_reuse_addr = 1                    ;
	uint32_t i_connect    = 1                    ;
	for( uint32_t i=1 ;; i++ ) {
		if (has_timeout) {
			TimeVal to ( ::max( Delay(0.001) , end-Pdate(New) ) ) ;                                                              // ensure to is positive
			::setsockopt( fd , SOL_SOCKET , SO_SNDTIMEO , &to , sizeof(to) ) ;
		}
		if ( ::connect( fd , &sa.as_sockaddr() , sizeof(sa) )==0 ) {
			if (has_timeout) ::setsockopt( fd , SOL_SOCKET , SO_SNDTIMEO , &::ref(TimeVal(Delay())) , sizeof(TimeVal) ) ; // restore no timeout
			break ;
		}
		switch (errno) {
			case EADDRNOTAVAIL :
				if (i_reuse_addr>=NAddrInUseTrials) throw cat("cannot connect to ",s_service(server,port)," after ",NAddrInUseTrials," trials : ",StrErr()) ;
				i_reuse_addr++ ;
				AddrInUseTick.sleep_for() ; // this error is local, so wait a little bit before retry
			break ;
			case EAGAIN :
			case EINTR  :
			break ;
			case ETIMEDOUT :                // happens even if no timeout is specifie on socket
				if ( Pdate now{New} ; now>end ) throw cat("cannot connect to ",s_service(server,port)," after ",(timeout+(now-end)).short_str()," : ",StrErr()) ;
				break ;
			default :                       // although not documented, various errors may happen when server is overloaded
				if (i_connect>=NConnectTrials) throw cat("cannot connect to ",s_service(server,port)," after ",NConnectTrials," trials : ",StrErr()) ;
				i_connect++ ;
		}
	}
}

