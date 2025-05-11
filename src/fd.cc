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

::string& operator+=( ::string& os , SockFd       const& fd ) { return os << "SockFd("       << fd.fd <<')' ; } // NO_COV
::string& operator+=( ::string& os , SlaveSockFd  const& fd ) { return os << "SlaveSockFd("  << fd.fd <<')' ; } // NO_COV
::string& operator+=( ::string& os , ServerSockFd const& fd ) { return os << "ServerSockFd(" << fd.fd <<')' ; } // NO_COV
::string& operator+=( ::string& os , ClientSockFd const& fd ) { return os << "ClientSockFd(" << fd.fd <<')' ; } // NO_COV

::string host() {
	char buf[HOST_NAME_MAX+1] ;
	int rc                    = ::gethostname(buf,sizeof(buf)) ;
	swear_prod(rc==0,"cannot get host name") ;
	return buf ;
}

::string fqdn() {
	char buf[DOMAIN_NAME_MAX+1] ;
	int rc                      = ::getdomainname(buf,sizeof(buf)) ;
	if (rc<0          ) return host()         ;
	if (!buf[0]       ) return host()         ;
	if (buf=="(none)"s) return host()         ;
	/**/                return host()+'.'+buf ;
}

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

SlaveSockFd ServerSockFd::accept() {
	SlaveSockFd slave_fd = ::accept( fd , nullptr , nullptr ) ;
	swear_prod(+slave_fd,"cannot accept from",self) ;
	return slave_fd ;
}

void ClientSockFd::connect( in_addr_t server , in_port_t port , Delay timeout ) {
	if (!self) init() ;
	swear_prod(fd>=0,"cannot create socket") ;
	struct sockaddr_in sa       = s_sockaddr(server,port) ;
	Pdate              end      ;
	bool               too_late = false                   ;
	for( int i=NTrials ; i>0 ; i-- ) {
		if (+timeout) {
			Pdate now = New ;
			if (!end    )   end = now + timeout ;
			if (now>=end) { too_late = true ; break ; }                                        // this cannot happen on first iteration, so we are guaranteed to try at least once
			Pdate::TimeVal to ( end-now ) ;
			::setsockopt( fd , SOL_SOCKET , SO_SNDTIMEO , &to , sizeof(to) ) ;
		}
		if ( ::connect( fd , reinterpret_cast<sockaddr*>(&sa) , sizeof(sockaddr) )==0 ) {
			if (+timeout) {
				Pdate::TimeVal to {} ;
				::setsockopt( fd , SOL_SOCKET , SO_SNDTIMEO , &to , sizeof(Pdate::TimeVal) ) ; // restore no timeout
			}
			return ;                                                                           // success
		}
		if (i>1) Delay(0.001).sleep_for() ;                                                    // wait a little bit before retrying if not last trial
	}
	int en = errno ;                                                                           // catch errno before any other syscall
	close() ;
	if      (too_late ) throw cat("cannot connect to ",s_addr_str(server),':',port," : ",::strerror(en)," after ",timeout.short_str() ) ;
	else if (NTrials>1) throw cat("cannot connect to ",s_addr_str(server),':',port," : ",::strerror(en)," after ",NTrials," trials"   ) ;
	else                throw cat("cannot connect to ",s_addr_str(server),':',port," : ",::strerror(en)                               ) ;
}

in_addr_t SockFd::s_addr(::string const& server) {
	if (!server) return LoopBackAddr ;
	// by standard dot notation
	{	in_addr_t addr   = 0     ;                                                                           // address being decoded
		int       byte   = 0     ;                                                                           // ensure component is less than 256
		int       n      = 0     ;                                                                           // ensure there are 4 components
		bool      first  = true  ;                                                                           // prevent empty components
		bool      first0 = false ;                                                                           // prevent leading 0's (unless component is 0)
		for( char c : server ) {
			if (c=='.') {
				if (first) goto ByName ;
				addr  = (addr<<8) | byte ;                                                                   // dot notation is big endian
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
	{	struct addrinfo  hint = {}                                                                         ; hint.ai_family = AF_INET ; hint.ai_socktype = SOCK_STREAM ;
		struct addrinfo* ai   ;
		int              rc   = ::getaddrinfo( server.c_str() , nullptr , &hint , &ai )                    ; throw_unless( rc==0 , "cannot get addr of ",server," (",rc,')' ) ;
		in_addr_t        addr = ntohl(reinterpret_cast<struct sockaddr_in*>(ai->ai_addr)->sin_addr.s_addr) ; // dont prefix with :: as ntohl may be a macro
		freeaddrinfo(ai) ;
		return addr ;
	}
}

::vmap_s<in_addr_t> SockFd::s_addrs_self(::string const& ifce) {
	::vmap_s<in_addr_t> res ;
	struct ifaddrs*     ifa ;
	if (::getifaddrs(&ifa)<0) return {{{},LoopBackAddr}} ;
	for( struct ifaddrs* p=ifa ; p ; p=p->ifa_next ) {
		if (!( p->ifa_addr && p->ifa_addr->sa_family==AF_INET )) continue ;
		in_addr_t addr = ntohl(reinterpret_cast<struct sockaddr_in*>(p->ifa_addr)->sin_addr.s_addr) ; // dont prefix with :: as ntohl may be a macro
		if (+ifce) { if (p->ifa_name      !=ifce) continue ; }                                        // searching provided interface
		else       { if (((addr>>24)&0xff)==127 ) continue ; }                                        // searching for any non-loopback interface
		res.emplace_back(p->ifa_name,addr) ;
	}
	freeifaddrs(ifa) ;
	if (+res ) return res                 ;
	if (+ifce) return {{{},s_addr(ifce)}} ;                                                           // ifce may actually be a name
	/**/       return {{{},LoopBackAddr}} ;
}

