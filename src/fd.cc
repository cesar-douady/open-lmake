
// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "fd.hh"

#include "time.hh"

using namespace Time ;

ostream& operator<<( ostream& os , Epoll::Event const& e ) {
	return os << "Event(" << e.fd() <<','<< e.data() <<')' ;
}

::vector<Epoll::Event> Epoll::wait(uint64_t timeout_ns) const {
	struct ::timespec now ;
	struct ::timespec end ;
	SWEAR(cnt) ;
	bool has_timeout = timeout_ns>0 && timeout_ns<Forever ;
	if (has_timeout) {
		::clock_gettime(CLOCK_MONOTONIC,&now) ;
		end.tv_sec  = now.tv_sec  + timeout_ns/1'000'000'000l ;
		end.tv_nsec = now.tv_nsec + timeout_ns%1'000'000'000l ;
		if (end.tv_nsec>=1'000'000'000l) {
			end.tv_nsec -= 1'000'000'000l ;
			end.tv_sec  += 1              ;
		}
	}
	for(;;) {
		::vector<Event> events        ; events.resize(cnt) ;
		int             cnt_          ;
		int             wait_ms       = -1    ;
		bool            wait_overflow = false ;
		if (has_timeout) {
			time_t wait_s   = end.tv_sec - now.tv_sec               ;
			time_t wait_max = ::numeric_limits<int>::max()/1000 - 1 ;
			if ((wait_overflow=(wait_s>wait_max))) wait_s = wait_max ;
			wait_ms  = wait_s                    * 1'000      ;
			wait_ms += (end.tv_nsec-now.tv_nsec) / 1'000'000l ;                // protect against possible conversion to time_t which may be unsigned
		} else {
			wait_ms = timeout_ns ? -1 : 0 ;
		}
		cnt_ = ::epoll_wait( fd , events.data() , cnt , wait_ms ) ;
		Ddate::s_refresh_now() ;                                               // we have waited, refresh Ddate::s_now
		switch (cnt_) {
			case  0 : if (!wait_overflow)             return {}     ; break ;  // timeout
			case -1 : SWEAR( errno==EINTR , errno ) ;                 break ;
			default : events.resize(cnt_) ;           return events ;
		}
		if (wait_overflow) ::clock_gettime(CLOCK_MONOTONIC,&now) ;
	}
}

::ostream& operator<<( ::ostream& os , Fd           const& fd ) { return os << "Fd("           << fd.fd <<')' ; }
::ostream& operator<<( ::ostream& os , AutoCloseFd  const& fd ) { return os << "AutoCloseFd("  << fd.fd <<')' ; }
::ostream& operator<<( ::ostream& os , SockFd       const& fd ) { return os << "SockFd("       << fd.fd <<')' ; }
::ostream& operator<<( ::ostream& os , SlaveSockFd  const& fd ) { return os << "SlaveSockFd("  << fd.fd <<')' ; }
::ostream& operator<<( ::ostream& os , ServerSockFd const& fd ) { return os << "ServerSockFd(" << fd.fd <<')' ; }
::ostream& operator<<( ::ostream& os , ClientSockFd const& fd ) { return os << "ClientSockFd(" << fd.fd <<')' ; }

::string host() {
	char buf[HOST_NAME_MAX+1] ;
	int rc = ::gethostname(buf,sizeof(buf)) ;
	swear_prod(rc==0,"cannot get host name") ;
	return buf ;
}

void ClientSockFd::connect( in_addr_t server , in_port_t port , int n_trials ) {
	if (!*this) init() ;
	swear_prod(fd>=0,"cannot create socket") ;
	static_assert( sizeof(in_port_t)==2 && sizeof(in_addr_t)==4 ) ;            // else use adequate htons/htonl according to the sizes
	struct sockaddr_in addr {
		.sin_family = AF_INET
	,	.sin_port   = htons(port)
	,	.sin_addr   = { .s_addr=htonl(server) }
	,	.sin_zero   = {}
	} ;
	for( int i=n_trials ;; i-- ) {
		if (::connect( fd , reinterpret_cast<sockaddr*>(&addr) , sizeof(sockaddr) )==0 ) return ; // success
		if (i<=1) {
			int en = errno ;                                                   // catch errno before any other syscall
			close() ;
			if (n_trials>1) throw to_string(strerror(en)," after ",n_trials," trials") ;
			else            throw to_string(strerror(en)                             ) ;
		} else {
			Delay(0.001).sleep_for() ;                                         // wait a little bit before retrying if not last trial
		}
	}
	FAIL() ;
}

in_addr_t SockFd::s_addr(::string const& server) {
	if (server.empty()) return LoopBackAddr ;
	// by standard dot notation
	{	in_addr_t addr  = 0     ;                                              // address being decoded
		int       byte  = 0     ;                                              // ensure component is less than 256
		int       n     = 0     ;                                              // ensure there are 4 components
		bool      first = true  ;                                              // prevent empty components
		bool      last  = false ;                                              // prevent leading 0's (unless component is 0)
		for( char c : server ) {
			if (c=='.') {
				if (n>=4 ) goto Next ;
				if (first) goto Next ;
				addr  = (addr<<8) | byte ;                                     // network order is big endian
				byte  = 0                ;
				first = true             ;
				n++ ;
				continue ;
			}
			if ( c>='0' && c<='9' ) {
				if (last) goto Next ;
				if ( first && c=='0' ) last = true ;
				last  = false             ;
				first = false             ;
				byte  = byte*10 + (c-'0') ;
				if (byte>=256) goto Next ;
				continue ;
			}
			goto Next ;
		}
		if (first) goto Next ;
		if (n!=4 ) goto Next ;
		return addr ;
	Next : ;
	}
	{	struct ifaddrs* ifa ;
		if (::getifaddrs(&ifa)==0) {
			for( struct ifaddrs* p=ifa ; p ; p=p->ifa_next )
				if ( p->ifa_addr && p->ifa_addr->sa_family==AF_INET  && p->ifa_name==server ) {
					in_addr_t addr = ::ntohl( reinterpret_cast<struct sockaddr_in*>(p->ifa_addr)->sin_addr.s_addr ) ;
					freeifaddrs(ifa) ;
					return addr ;
				}
			freeifaddrs(ifa) ;
		}
	}
	// by name
	{	struct addrinfo hint = {} ;
		hint.ai_family   = AF_INET     ;
		hint.ai_socktype = SOCK_STREAM ;
		struct addrinfo* ai ;
		int              rc  = ::getaddrinfo( server.c_str() , nullptr , &hint , &ai ) ;
		if (rc!=0) throw to_string("cannot get addr of ",server," (",rc,')') ;
		static_assert(sizeof(in_addr_t)==4) ;                                                 // else use adequate ntohl/ntohs
		in_addr_t addr = ::ntohl(reinterpret_cast<struct sockaddr_in*>(ai->ai_addr)->sin_addr.s_addr) ;
		freeaddrinfo(ai) ;
		return addr ;
	}
}
