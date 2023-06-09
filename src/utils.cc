// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#if HAS_CLOSE_RANGE
    #include <linux/close_range.h>
#endif

#include "utils.hh"

//
// string
//
::string mk_printable(::string const& s) {
	::string res ; res.reserve(s.size()) ;                                     // typically, all characters are printable and nothing to add
	for( char c : s ) {
		if ( ::isprint(c) && c!='\\' ) { res += c ; continue ; }
		switch (c) {
			case '\a' : res += "\\a"                       ; break ;
			case '\b' : res += "\\b"                       ; break ;
			case 0x1b : res += "\\e"                       ; break ;
			case '\f' : res += "\\f"                       ; break ;
			case '\n' : res += "\\n"                       ; break ;
			case '\r' : res += "\\r"                       ; break ;
			case '\t' : res += "\\t"                       ; break ;
			case '\v' : res += "\\v"                       ; break ;
			case '\\' : res += "\\\\"                      ; break ;
			default   : res += to_string("\\x",hex,int(c)) ;
		}
	}
	return res ;
}

::string mk_py_str(::string const& s) {
	::string res {'\''} ; res.reserve(s.size()+(s.size()>>4)+2) ;              // take a little bit of margin + initial and final quotes
	for( char c : s ) {
		switch (c) {
			case ' '  : res += c ;               break ;
			case '\\' :
			case '\'' : res += '\\' ; res += c ; break ;
			case '\a' : res += "\\a" ;           break ;
			case '\b' : res += "\\b" ;           break ;
			case '\f' : res += "\\f" ;           break ;
			case '\n' : res += "\\n" ;           break ;
			case '\r' : res += "\\r" ;           break ;
			case '\t' : res += "\\t" ;           break ;
			case '\v' : res += "\\v" ;           break ;
			default :
				if ( ::isprint(c) && !::isspace(c) ) {
					res += c ;
				} else {
					char buf[5] ; snprintf(buf,sizeof(buf),"\\x%02x",uint8_t(c)) ;
					res += buf ;
				}
		}
	}
	res += '\'' ;
	return res ;
}

::string mk_shell_str(::string const& s) {
	::string res {'\''} ; res.reserve(s.size()+(s.size()>>4)+2) ;              // take a little bit of margin + initial and final quotes
	for( char c : s )
		if (c=='\'') res += "'\\''" ;
		else         res += c       ;
	res += '\'' ;
	return res ;
}

void set_sig_handler( int sig_num , void (*handler)(int) ) {
sigset_t empty ; ::sigemptyset(&empty) ;
struct sigaction action ;
action.sa_handler = handler    ;
action.sa_mask    = empty      ;
	action.sa_flags   = SA_RESTART ;
	::sigaction( sig_num , &action , nullptr ) ;
}

static size_t/*len*/ _beautify(char* file_name) {                              // does not call malloc for use in src_point
	enum State { Plain , Slash , Dot , DotDot } ;
	char*       out = file_name ;
	const char* min = file_name ;
	State  state = Slash ;
	for( const char* in=file_name ; *in ; in++) {
		char c = *in ;
		switch (c) {
			case '/' : {
				State s = state ;
				state = Slash ;
				switch (s) {
					case Plain : break ;
					case Slash  :
						if (in!=file_name) continue ;                          // 2 consecutive /, ignore 2nd if not first char
						min = file_name+1 ;
					break ;
					case Dot :                                                 // after a ., suppress it
						out-- ;
						continue ;
					case DotDot :
						if (out>=min+4) {
							out -= 3 ;                                         // suppress /..
							while ( out>min && out[-1]!='/' ) out-- ;          // and previous dir
							continue ;
						}
						min = out ;                                            // prevent following .. from clobbering this one
					break ;
				}
			} break ;
			case '.' :
				switch (state) {
					case Plain  :                  break ;
					case Slash  : state = Dot    ; break ;
					case Dot    : state = DotDot ; break ;
					case DotDot : state = Plain  ; break ;
				}
			break ;
			default :
				state = Plain ;
		}
		*out++ = c ;
	}
	*out = 0 ;
	return out-file_name ;
}

::string beautify_file_name(::string const& file_name) {                                 // normal interface
	::string res = file_name                   ;
	size_t   len = _beautify(res.data()) ;
	res.resize(len) ;
	return res ;
}

struct SrcPoint {
	char     file[PATH_MAX] ;
	size_t   line           = 0 ;
	char     func[1000]     ;
} ;

// XXX : use/mimic https://github.com/ianlancetaylor/libbacktrace or libbfd
static size_t fill_src_points( void* addr , SrcPoint* src_points , size_t n_src_points ) {
	char             exe[PATH_MAX]  ;
	ssize_t          cnt            = ::readlink("/proc/self/exe",exe,sizeof(exe)) ; exe[cnt] = 0 ;
	Dl_info          info           ;
	struct link_map* map            ; ::dladdr1( addr , &info , (void**)(&map) , RTLD_DL_LINKMAP ) ;
	const char*      file           = *map->l_name ? map->l_name : exe ;
	uintptr_t        offset         = (uintptr_t)addr - map->l_addr    ;
	//
	char             hex_offset[20] ; ::snprintf(hex_offset,sizeof(hex_offset),"0x%lx",(unsigned long)offset) ;
	::array<Fd,2>    c2p            = pipe() ;                                                                  // dont use Child as if called from signal handler, malloc is forbidden
	int pid = ::fork() ;
	if (!pid) {                                                                // child
		::close(c2p[0]) ;
		if (c2p[1]!=Fd::Stdout) { ::dup2(c2p[1],Fd::Stdout) ; ::close(c2p[1]) ; }
		::close(Fd::Stdin) ;                                                                                  // no input
		const char* args[] = { "/bin/addr2line" , "-f" , "-i" , "-C" , "-e" , file , hex_offset , nullptr } ;
		::execv( args[0] , const_cast<char**>(args) ) ;
		exit(2) ;                                                              // in case exec fails
	}
	::close(c2p[1]) ;
	size_t n_sp ;
	for( n_sp=0 ; n_sp<n_src_points ; n_sp++ ) {
		SrcPoint& sp = src_points[n_sp] ;
		for( size_t i=0 ;;) {                                                  // read first line to the end, even in case of sp.func overflows
			char c ;
			if (::read(c2p[0],&c,1)!=1) {                    goto Return ; }   // if we cannot read a line, we are done
			if (c=='\n'               ) { sp.func[i] = 0   ; break       ; }
			if (i<sizeof(sp.func)-1   ) { sp.func[i++] = c ;               }
		}
		size_t col = -1 ;
		for( size_t i=0 ;;) {                                                  // read first line to the end, even in case of sp.func overflows
			char c ;
			if (::read(c2p[0],&c,1)!=1) {                    goto Return ; }   // if we cannot read a line, we are done
			if (c==':'                ) { col = i          ;               }
			if (c=='\n'               ) { sp.file[i] = 0   ; break       ; }
			if (i<sizeof(sp.file)-1   ) { sp.file[i++] = c ;               }
		}
		if (col<sizeof(sp.file)-1) {
			sp.file[col] = 0                     ;
			sp.line      = ::atol(sp.file+col+1) ;
		}
		_beautify(sp.file) ;                                                   // system files may contain a lot of .., making long file names and alignment makes all lines very long
	}
Return :
	::close(c2p[0]) ;
	::waitpid(pid,nullptr,0) ;
	return n_sp ;
}

void write_backtrace( ::ostream& os , int hide_cnt ) {
	static constexpr size_t StackSize = 100 ;
	//
	static void*    stack         [StackSize] ;                                // avoid big allocation on stack
	static SrcPoint symbolic_stack[StackSize] ;                                // .
	//
	int backtrace_sz = backtrace(stack,StackSize) ;                             // XXX : dont know how to avoid malloc here
	int stack_sz     = 0                          ;
	for( int i=hide_cnt+1 ; i<backtrace_sz ; i++ ) {
		stack_sz += fill_src_points( stack[i] , symbolic_stack+stack_sz , StackSize-stack_sz ) ;
		if (strcmp(symbolic_stack[stack_sz-1].func,"main")==0) break ;
	}
	size_t  wf = 0 ;
	uint8_t wl = 0 ;
	for( int i=0 ; i<stack_sz ; i++ ) {
		uint8_t w = 0 ; for( size_t l=symbolic_stack[i].line ; l ; l/=10 ) w++ ;
		wf = ::max( wf , strnlen(symbolic_stack[i].file,PATH_MAX) ) ;
		wl = ::max( wl , w                                        ) ;
	}
	os <<::left ;
	for( int i=0 ; i<stack_sz ; i++ ) {
		/**/                        os <<         ::setw(wf)<<          symbolic_stack[i].file         ;
		if (symbolic_stack[i].line) os <<':'   << ::setw(wl)<< ::right<<symbolic_stack[i].line<<::left ;
		else                        os <<' '   << ::setw(wl)<< ""                                      ;
		/**/                        os <<" : " << ::setw(0 )<<          symbolic_stack[i].func         ;
		/**/                        os <<::endl ;
	}
}

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
		switch (cnt_) {
			case  0 :                       if (!wait_overflow) return {}     ; break ; // timeout
			case -1 : SWEAR(errno==EINTR) ;                                     break ;
			default : events.resize(cnt_) ;                     return events ;
		}
		if (wait_overflow) ::clock_gettime(CLOCK_MONOTONIC,&now) ;
	}
}

//
// sockets
//

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

void ClientSockFd::connect( in_addr_t server , in_port_t port ) {
	if (!*this) init() ;
	swear_prod(fd>=0,"cannot create socket") ;
	static_assert( sizeof(in_port_t)==2 && sizeof(in_addr_t)==4 ) ;            // else use adequate htons/htonl according to the sizes
	struct sockaddr_in addr {
		.sin_family = AF_INET
	,	.sin_port   = htons(port)
	,	.sin_addr   = { .s_addr=htonl(server) }
	,	.sin_zero   = {}
	} ;
	int rc = ::connect( fd , reinterpret_cast<sockaddr*>(&addr) , sizeof(sockaddr) ) ;
	if (rc!=0) close() ;
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
		swear_prod(rc==0,"cannot get addr of ",server," (",rc,')') ;
		static_assert(sizeof(in_addr_t)==4) ;                                                 // else use adequate ntohl/ntohs
		in_addr_t addr = ::ntohl(reinterpret_cast<struct sockaddr_in*>(ai->ai_addr)->sin_addr.s_addr) ;
		freeaddrinfo(ai) ;
		return addr ;
	}
}

//
// processes
//

bool/*parent*/ Child::spawn(
	bool            as_group_ , ::vector_s const& args
,	Fd              stdin_fd  , Fd                stdout_fd , Fd stderr_fd
,	::map_ss const* env       , ::map_ss   const* add_env
,	::string const& chroot_
,	::string const& cwd_
,	void (*pre_exec)()
) {
	SWEAR( !stdin_fd  || stdin_fd ==Fd::Stdin  || stdin_fd >Fd::Std ) ;        // ensure reasonably simple situations
	SWEAR( !stdout_fd || stdout_fd>=Fd::Stdout                      ) ;        // .
	SWEAR( !stderr_fd || stderr_fd>=Fd::Stdout                      ) ;        // .
	SWEAR(!( stderr_fd==Fd::Stdout && stdout_fd==Fd::Stderr        )) ;        // .
	::array<Fd,2> p2c  ;
	::array<Fd,2> c2po ;
	::array<Fd,2> c2pe ;
	if (stdin_fd ==Pipe) p2c  = pipe() ; else if (+stdin_fd ) p2c [0] = stdin_fd  ;
	if (stdout_fd==Pipe) c2po = pipe() ; else if (+stdout_fd) c2po[1] = stdout_fd ;
	if (stderr_fd==Pipe) c2pe = pipe() ; else if (+stderr_fd) c2pe[1] = stderr_fd ;
	as_group = as_group_ ;
	pid      = fork()    ;
	if (!pid) { // child
		if (as_group) ::setpgid(0,0) ;
		sigset_t full_mask ; ::sigfillset(&full_mask) ;
		::sigprocmask(SIG_UNBLOCK,&full_mask,nullptr) ;                        // restore default behavior
		if (stdin_fd ==Pipe) { p2c [1].close() ; p2c [0].no_std() ; }          // could be optimized, but too complex to manage
		if (stdout_fd==Pipe) { c2po[0].close() ; c2po[1].no_std() ; }          // .
		if (stderr_fd==Pipe) { c2pe[0].close() ; c2pe[1].no_std() ; }          // .
		// set up std fd
		if      (stdin_fd==None   ) ::close(Fd::Stdin)        ;
		else if (p2c[0]!=Fd::Stdin) ::dup2(p2c[0],Fd::Stdin ) ;
		//
		if      (stdout_fd==None    ) { ::close(Fd::Stdout)        ; } // save stdout in case it is modified and we want to redirect stderr to it
		else if (c2po[1]!=Fd::Stdout) { ::dup2(c2po[1],Fd::Stdout) ; } // .
		//
		if      (stderr_fd==None    ) ::close(Fd::Stderr)        ;
		else if (c2pe[1]!=Fd::Stderr) ::dup2(c2pe[1],Fd::Stderr) ;
		//
		if (p2c [0]>Fd::Std) p2c [0].close() ;                                 // clean up : we only want to set up standard fd, other ones are necessarily temporary constructions
		if (c2po[1]>Fd::Std) c2po[1].close() ;                                 // .
		if (c2pe[1]>Fd::Std) c2pe[1].close() ;                                 // .
		//
		const char** child_env  = const_cast<const char**>(environ) ;
		::vector_s   env_vector ;                                              // ensure actual env strings lifetime last until execve call
		if (env) {
			SWEAR(!args.empty()) ;                                             // cannot fork with env
			size_t n_env = env->size() + (add_env?add_env->size():0) ;
			env_vector.reserve(n_env) ;
			for( auto const* e : {env,add_env} )
				if (e)
					for( auto const& [k,v] : *e )
						env_vector.push_back(k+'='+v) ;
			child_env = new const char*[n_env+1] ;
			// /!\ : c_str() seems to be invalidated by vector reallocation although this does not appear in doc : https://en.cppreference.com/w/cpp/string/basic_string/c_str
			for( size_t i=0 ; i<n_env ; i++ ) child_env[i] = env_vector[i].c_str() ;
			child_env[n_env] = nullptr ;
		} else {
			if (add_env) for( auto const& [k,v] : *add_env ) set_env(k,v) ;
		}
		if (!chroot_.empty()) { if (::chroot(chroot_.c_str())!=0) throw to_string("cannot chroot to : ",chroot_) ; }
		if (!cwd_   .empty()) { if (::chdir (cwd_   .c_str())!=0) throw to_string("cannot chdir to : " ,cwd_   ) ; }
		if (pre_exec        ) { pre_exec() ;                                                                       }
		//
		if (args.empty()) return false ;
		#if HAS_CLOSE_RANGE
			//::close_range(3,~0u,CLOSE_RANGE_UNSHARE) ;                       // activate this code (uncomment) as an alternative to set CLOEXEC in IFStream/OFStream
		#endif
		const char** child_args = new const char*[args.size()+1] ;
		for( size_t i=0 ; i<args.size() ; i++ ) child_args[i] = args[i].c_str() ;
		child_args[args.size()] = nullptr ;
		if (env) ::execve( child_args[0] , const_cast<char**>(child_args) , const_cast<char**>(child_env) ) ;
		else     ::execv ( child_args[0] , const_cast<char**>(child_args)                                 ) ;
		pid = -1 ;
		throw to_string("cannot exec (",strerror(errno),") : ",args) ;         // in case exec fails
	}
	if (stdin_fd ==Pipe) { stdin  = p2c [1] ; p2c [0].close() ; }
	if (stdout_fd==Pipe) { stdout = c2po[0] ; c2po[1].close() ; }
	if (stderr_fd==Pipe) { stderr = c2pe[0] ; c2pe[1].close() ; }
	return true ;
}
