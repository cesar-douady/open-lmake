// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <execinfo.h>                  // backtrace
#include <link.h>                      // struct link_map

#if HAS_CLOSE_RANGE
    #include <linux/close_range.h>
#endif

#include "utils.hh"

#include "fd.hh"
#include "process.hh"

//
// string
//

::string mk_py_str(::string const& s) {
	::string res {'\''} ; res.reserve(s.size()+(s.size()>>4)+2) ;              // take a little bit of margin + initial and final quotes
	for( char c : s ) {
		switch (c) {
			case '\a' : res += "\\a" ; break ;           // special case
			case '\b' : res += "\\b" ; break ;           // .
			case '\f' : res += "\\f" ; break ;           // .
			case '\n' : res += "\\n" ; break ;           // .
			case '\r' : res += "\\r" ; break ;           // .
			case '\t' : res += "\\t" ; break ;           // .
			case '\v' : res += "\\v" ; break ;           // .
			case '\\' :                                  // must be escaped
			case '\'' : res += '\\'  ; [[fallthrough]] ; // .
			default :
				if (is_printable(c)) res +=                                                            c   ;
				else                 res += to_string("\\x",::right,::setfill('0'),::hex,::setw(2),int(c)) ;
		}
	}
	res += '\'' ;
	return res ;
}

::string mk_shell_str(::string const& s) {
	::string res {'\''} ; res.reserve(s.size()+(s.size()>>4)+2) ;              // take a little bit of margin + quotes
	for( char c : s )
		switch (c) {
			case '\'' : res += "\'\\\'\'" ; break ;                            // no way to escape a ' in a shell '-string, you have to exit, insert the ', and enter back, i.e. : '\''
			default   : res += c          ;
		}
	res += '\'' ;
	return res ;
}

::string glb_subst( ::string&& txt , ::string const& sub , ::string const& repl ) {
	SWEAR(+sub) ;
	size_t      pos   = txt.find(sub)     ; if (pos==Npos) return ::move(txt) ;
	::string    res   = txt.substr(0,pos) ; res.reserve(txt.size()+repl.size()-sub.size()) ; // assume single replacement, which is the common case when there is one
	while (pos!=Npos) {
		size_t p = pos+sub.size() ;
		pos = txt.find(sub,p) ;
		res.append(repl       ) ;
		res.append(txt,p,pos-p) ;
	}
	return res ;
}

//
// assert
//

thread_local char t_thread_key = '?' ;

void set_sig_handler( int sig , void (*handler)(int) ) {
	sigset_t         empty  ; ::sigemptyset(&empty) ;
	struct sigaction action ;
	action.sa_handler = handler    ;
	action.sa_mask    = empty      ;
	action.sa_flags   = SA_RESTART ;
	::sigaction( sig , &action , nullptr ) ;
}

static size_t/*len*/ _beautify(char* filename) {                               // does not call malloc for use in src_point
	enum State { Plain , Slash , Dot , DotDot } ;
	char*       out = filename ;
	const char* min = filename ;
	State  state = Slash ;
	for( const char* in=filename ; *in ; in++) {
		char c = *in ;
		switch (c) {
			case '/' : {
				State s = state ;
				state = Slash ;
				switch (s) {
					case Plain : break ;
					case Slash  :
						if (in!=filename) continue ;                           // 2 consecutive /, ignore 2nd if not first char
						min = filename+1 ;
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
	return out-filename ;
}

::string beautify_filename(::string const& filename) {                         // normal interface
	::string res = filename              ;
	size_t   len = _beautify(res.data()) ;
	res.resize(len) ;
	return res ;
}

struct SrcPoint {
	char     file[PATH_MAX] ;
	size_t   line           = 0 ;
	char     func[1000]     ;
} ;

// XXX : use c++23 backtrace facility rather than use/mimic https://github.com/ianlancetaylor/libbacktrace or libbfd
static size_t fill_src_points( void* addr , SrcPoint* src_points , size_t n_src_points ) {
	char             exe[PATH_MAX]  ;
	ssize_t          cnt            = ::readlink("/proc/self/exe",exe,sizeof(exe)) ; exe[cnt] = 0 ;
	Dl_info          info           ;
	struct link_map* map            ; ::dladdr1( addr , &info , (void**)(&map) , RTLD_DL_LINKMAP ) ;
	const char*      file           = *map->l_name ? map->l_name : exe ;
	uintptr_t        offset         = (uintptr_t)addr - map->l_addr    ;
	//
	char             hex_offset[20] ; ::snprintf(hex_offset,sizeof(hex_offset),"0x%lx",(unsigned long)offset) ;
	Pipe             c2p            { New } ;                                                                   // dont use Child as if called from signal handler, malloc is forbidden
	int pid = ::fork() ;
	if (!pid) {                                                                // child
		::close(c2p.read) ;
		if (c2p.write!=Fd::Stdout) { ::dup2(c2p.write,Fd::Stdout) ; ::close(c2p.write) ; }
		::close(Fd::Stdin) ;                                                                                  // no input
		const char* args[] = { "/bin/addr2line" , "-f" , "-i" , "-C" , "-e" , file , hex_offset , nullptr } ;
		::execv( args[0] , const_cast<char**>(args) ) ;
		exit(2) ;                                                              // in case exec fails
	}
	::close(c2p.write) ;
	size_t n_sp ;
	for( n_sp=0 ; n_sp<n_src_points ; n_sp++ ) {
		SrcPoint& sp = src_points[n_sp] ;
		for( size_t i=0 ;;) {                                                  // read first line to the end, even in case of sp.func overflows
			char c ;
			if (::read(c2p.read,&c,1)!=1) {                    goto Return ; } // if we cannot read a line, we are done
			if (c=='\n'                 ) { sp.func[i] = 0   ; break       ; }
			if (i<sizeof(sp.func)-1     ) { sp.func[i++] = c ;               }
		}
		size_t col = -1 ;
		for( size_t i=0 ;;) {                                                  // read first line to the end, even in case of sp.func overflows
			char c ;
			if (::read(c2p.read,&c,1)!=1) {                  goto Return ; }   // if we cannot read a line, we are done
			if (c==':'                ) { col = i          ;               }
			if (c=='\n'               ) { sp.file[i] = 0   ; break       ; }
			if (i<sizeof(sp.file)-1   ) { sp.file[i++] = c ;               }
		}
		if (col<sizeof(sp.file)-1) {
			/**/                        sp.file[col] = 0                                                  ;
			try                       { sp.line      = from_chars<size_t>(sp.file+col+1,true/*empty_ok*/) ; }
			catch (::string const& e) { sp.line      = 0                                                  ; }
		}
		_beautify(sp.file) ;                                                   // system files may contain a lot of .., making long file names and alignment makes all lines very long
	}
Return :
	::close(c2p.read) ;
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
	for( int i=0 ; i<stack_sz ; i++ ) {
		/**/                        os <<         ::setw(wf)<<          symbolic_stack[i].file         ;
		if (symbolic_stack[i].line) os <<':'   << ::setw(wl)<< ::right<<symbolic_stack[i].line<<::left ;
		else                        os <<' '   << ::setw(wl)<< ""                                      ;
		/**/                        os <<" : " << ::setw(0 )<<          symbolic_stack[i].func         ;
		/**/                        os <<::endl ;
	}
}
