// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "utils.hh"

#if HAS_CLOSE_RANGE
    #include <linux/close_range.h>
#endif

#if HAS_STACKTRACE        // must be after utils.hh so that HAS_STACKTRACE and HAS_ADDR2LINE are defined
	#include <stacktrace>
#elif HAS_ADDR2LINE
	#include <execinfo.h> // backtrace
	#include <link.h>     // struct link_map
	#include "process.hh"
#endif

#include "disk.hh"
#include "fd.hh"

using namespace Disk ;

//
// mutexes
//

thread_local MutexLvl t_mutex_lvl = MutexLvl::None ;

//
// Fd
//

void Fd::write(::string_view data) const {
	for( size_t cnt=0 ; cnt<data.size() ;) {
		ssize_t c = ::write( fd , data.data()+cnt , data.size()-cnt ) ;
		if (c<=0) throw "cannot write to fd "s+fd ;
		cnt += c ;
	}
}

::string Fd::read( bool no_file_ok , size_t sz ) const {
	if ( no_file_ok && !self ) return {} ;
	::string res ; res.reserve(::min(sz,size_t(4096))) ;
	while (res.size()<sz) {
		size_t old_sz = res.size()                    ;
		size_t rd_sz  = ::min(sz-old_sz,size_t(4096)) ;
		res.resize( old_sz + rd_sz ) ;
		ssize_t c = ::read( fd , &res[old_sz] , rd_sz ) ;
		if (c<0 ) throw "cannot read from fd "s+fd ;
		res.resize( old_sz + c ) ;
		if (c==0) break ;
	}
	return res ;
}

::vector_s Fd::read_lines(bool no_file_ok) const {
	::string content = read(no_file_ok) ;
	if (!content            ) return {} ;
	if (content.back()=='\n') content.pop_back() ;
	return split(content,'\n') ;
}

//
// string
//

::string mk_py_str(::string_view s) {
	::string res {'\''} ; res.reserve(s.size()+(s.size()>>4)+2) ; // take a little bit of margin + initial and final quotes
	for( char c : s ) {
		switch (c) {
			case '\a' : res += "\\a"  ; break ;                   // special case
			case '\b' : res += "\\b"  ; break ;                   // .
			case '\f' : res += "\\f"  ; break ;                   // .
			case '\n' : res += "\\n"  ; break ;                   // .
			case '\r' : res += "\\r"  ; break ;                   // .
			case '\t' : res += "\\t"  ; break ;                   // .
			case '\v' : res += "\\v"  ; break ;                   // .
			case '\\' : res += "\\\\" ; break ;                   // .
			case '\'' : res += "\\'"  ; break ;                   // .
			default :
				if (is_printable(c)) res << c                                               ;
				else                 res << "\\x"<<('0'+uint8_t(c)/16)<<('0'+uint8_t(c)%16) ;
		}
	}
	res += '\'' ;
	return res ;
}

::string mk_json_str(::string_view s) {
	::string res {'"'} ; res.reserve(s.size()+(s.size()>>4)+2) ; // take a little bit of margin + initial and final quotes
	for( char c : s ) {
		switch (c) {
			case '\b' : res += "\\b"  ; break ;                  // special case
			case '\f' : res += "\\f"  ; break ;                  // .
			case '\n' : res += "\\n"  ; break ;                  // .
			case '\r' : res += "\\r"  ; break ;                  // .
			case '\t' : res += "\\t"  ; break ;                  // .
			case '\\' : res += "\\\\" ; break ;                  // .
			case '"'  : res += "\\\"" ; break ;                  // .
			default :
				if (is_printable(c)) res << c                                               ;
				else                 res << "\\x"<<('0'+uint8_t(c)/16)<<('0'+uint8_t(c)%16) ;
		}
	}
	res += '"' ;
	return res ;
}

::string mk_shell_str(::string_view s) {
	for( char c : s ) switch (c) {
		case '+' : continue ;
		case ',' : continue ;
		case '-' : continue ;
		case '.' : continue ;
		case '/' : continue ;
		case ':' : continue ;
		case '=' : continue ;
		case '@' : continue ;
		case '^' : continue ;
		case '_' : continue ;
		default :
			if ( c>='0' && c<='9' ) continue         ;
			if ( c>='a' && c<='z' ) continue         ;
			if ( c>='A' && c<='Z' ) continue         ;
			/**/                    goto SingleQuote ;
	}
	return ::string(s) ;                                          // simplest case : no quotes necessary
SingleQuote :
	if (s.find('\'')!=Npos) goto DoubleQuote ;
	return "'"s+s+'\'' ;                                          // next case : single quotes around text
 DoubleQuote :
	for( char c : s ) switch (c) {
		case '!'  : goto Complex ;
		case '"'  : goto Complex ;
		case '$'  : goto Complex ;
		case '\\' : goto Complex ;
		case '`'  : goto Complex ;
	DN}
	return "\""s+s+'"' ;                                          // next case : double quotes around text
Complex :
	::string res {'\''} ; res.reserve(s.size()+(s.size()>>4)+2) ; // take a little bit of margin + quotes
	for( char c : s )
		switch (c) {
			case '\'' : res += "\'\\\'\'" ; break ;               // no way to escape a ' in a shell '-string, you have to exit, insert the ', and enter back, i.e. : '\''
			default   : res += c          ;
		}
	res += '\'' ;
	return res ;                                                  // complex case : single quotes with internal protections
}

template<> ::string mk_printable( ::vector_s const& v , bool empty_ok ) {
	::string res   ;
	First    first ;
	res << '(' ;
	for( ::string const& s : v ) if ( empty_ok || +v ) res << first("",",")<< '"'<<mk_printable<'"'>(s)<<'"' ;
	res << ')' ;
	return res ;
}

template<> ::vector_s parse_printable( ::string const& txt , size_t& pos , bool empty_ok ) {
	::vector_s res ;
	if (txt[pos++]!='(') goto Fail ;
	for ( First first ; txt[pos]!=')' ;) {
		if (!first() && txt[pos++]!=',') goto Fail ;
		if (txt[pos++]!='"') goto Fail ;
		::string v = parse_printable<'"'>(txt,pos) ;
		if (txt[pos++]!='"') goto Fail ;
		if ( empty_ok || +v ) res.push_back(::move(v)) ;
	}
	if (txt[pos++]!=')') goto Fail ;
	return res ;
Fail :
	throw "bad format"s ;
}

template<> ::string mk_printable( ::vmap_ss const& m , bool empty_ok ) {
	::string res   ;
	First    first ;
	res << '{' ;
	for( auto const& [k,v] : m ) if ( empty_ok || +v ) res << first("",",") << '"'<<mk_printable<'"'>(k)<<'"' <<':'<< '"'<<mk_printable<'"'>(v)<<'"' ;
	res << '}' ;
	return res ;
}

template<> ::vmap_ss parse_printable( ::string const& txt , size_t& pos , bool empty_ok ) {
	::vmap_ss res ;
	if (txt[pos++]!='{') goto Fail ;
	for ( First first ; txt[pos]!='}' ;) {
		if (!first() && txt[pos++]!=',') goto Fail ;
		//
		if (txt[pos++]!='"') goto Fail ;
		::string k = parse_printable<'"'>(txt,pos) ;
		if (txt[pos++]!='"') goto Fail ;
		//
		if (txt[pos++]!=':') goto Fail ;
		//
		if (txt[pos++]!='"') goto Fail ;
		::string v = parse_printable<'"'>(txt,pos) ;
		if (txt[pos++]!='"') goto Fail ;
		//
		if ( empty_ok || +v ) res.emplace_back(k,v) ;
	}
	if (txt[pos++]!='}') goto Fail ;
	return res ;
Fail :
	throw "bad format"s ;
}

template<> ::string mk_printable( ::vmap_s<::vector_s> const& m , bool empty_ok ) {
	::string res    ;
	bool     first1 = true ;
	res << '{' ;
	for( auto const& [k,v] : m ) {
		if (!( empty_ok || +v )) continue ;
		if (!first1) res << ',' ; else first1 = false ;
		res << '"'<<mk_printable<'"'>(k)<<'"' ;
		res << ':' ;
		bool first2 = true ;
		res << '(' ;
		for( ::string const& x : v ) {
			if (!first2) res <<',' ; else first2 = false ;
			res << '"'<<mk_printable<'"'>(x)<<'"' ;
		}
		res << ')' ;
	}
	res << '}' ;
	return res ;
}

template<> ::vmap_s<::vector_s> parse_printable( ::string const& txt , size_t& pos , bool empty_ok ) {
	::vmap_s<::vector_s> res ;
	if (txt[pos++]!='{') goto Fail ;
	for ( bool first1=true ; txt[pos]!='}' ; first1=false ) {
		if (!first1 && txt[pos++]!=',') goto Fail ;
		//
		if (txt[pos++]!='"') goto Fail ;
		::string k = parse_printable<'"'>(txt,pos) ;
		if (txt[pos++]!='"') goto Fail ;
		//
		if (txt[pos++]!=':') goto Fail ;
		//
		::vector_s v ;
		if (txt[pos++]!='(') goto Fail ;
		for ( bool first2=true ; txt[pos]!=')' ; first2=false ) {
			if (!first2 && txt[pos++]!=',') goto Fail ;
			if (txt[pos++]!='"') goto Fail ;
			::string x = parse_printable<'"'>(txt,pos) ;
			if (txt[pos++]!='"') goto Fail ;
			v.push_back(::move(x)) ;
		}
		if (txt[pos++]!=')') goto Fail ;
		if ( empty_ok || +v ) res.emplace_back(k,v) ;
	}
	if (txt[pos++]!='}') goto Fail ;
	return res ;
Fail :
	throw "bad format"s ;
}

::string glb_subst( ::string&& txt , ::string const& sub , ::string const& repl ) {
	SWEAR(+sub) ;
	size_t   pos = txt.find(sub)     ; if (pos==Npos) return ::move(txt) ;
	::string res = txt.substr(0,pos) ; res.reserve(txt.size()+repl.size()-sub.size()) ; // assume single replacement, which is the common case when there is one
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

thread_local char t_thread_key = '?'   ;
bool              _crash_busy  = false ;

::string get_exe() {
	return read_lnk("/proc/self/exe") ;
}

#if HAS_STACKTRACE

	// /!\ if called from signal handler, we should not use malloc, but stupid ::stacktrace does not provide customized allocators, so hope or the best
	void write_backtrace( Fd fd , int hide_cnt ) {
		::stacktrace stack = ::stacktrace::current() ;
		//
		auto begin_frame = stack.begin() ; for  ( int i=0 ; i<=hide_cnt ; i++                                ) begin_frame++ ; // hide_cnt+1 to account for this very function
		auto end_frame   = begin_frame   ; while( end_frame!=stack.end() && end_frame->description()!="main" ) end_frame  ++ ; // dont trace above main
		/**/                               if   ( end_frame!=stack.end()                                     ) end_frame  ++ ; // but include main
		//
		size_t   wf = 0 ; for( auto it=begin_frame ; it!=end_frame ; it++ ) try { wf = ::max( wf , mk_canon (it->source_file()).size() ) ; } catch (::string const&) {}
		size_t   wl = 0 ; for( auto it=begin_frame ; it!=end_frame ; it++ )       wl = ::max( wl , to_string(it->source_line()).size() ) ;
		::string bt ;
		for( auto it=begin_frame ; it!=end_frame ; it++ ) {
			try                               { bt <<         widen(mk_canon(it->source_file()),wf              ) ; } catch (::string const&) { bt << widen("",wf) ; }
			if ( size_t l=it->source_line() )   bt <<':'   << widen(""s+l                      ,wl,true/*right*/) ;
			else                                bt <<' '   << widen(""                         ,wl              ) ;
			/**/                                bt <<" : " <<       it->description()                             ;
			/**/                                bt <<'\n'                                                         ;
		}
		fd.write(bt) ;
	}

#elif HAS_ADDR2LINE

	// if ::stacktrace is not available, try to mimic using addr2line, but this is of much lower quality :
	// - sometimes, the function is completely off
	// - quite often, line number is off by 1 or 2, either direction
	// however, this is better than nothing

	static size_t/*len*/ _beautify(char* filename) {                      // does not call malloc for use in src_point
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
							if (in!=filename) continue ;                  // 2 consecutive /, ignore 2nd if not first char
							min = filename+1 ;
						break ;
						case Dot :                                        // after a ., suppress it
							out-- ;
							continue ;
						case DotDot :
							if (out>=min+4) {
								out -= 3 ;                                // suppress /..
								while ( out>min && out[-1]!='/' ) out-- ; // and previous dir
								continue ;
							}
							min = out ;                                   // prevent following .. from clobbering this one
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

	struct SrcPoint {
		char     file[PATH_MAX] ;
		size_t   line           = 0 ;
		char     func[1000]     ;
	} ;

	static size_t fill_src_points( void* addr , SrcPoint* src_points , size_t n_src_points ) {
		char             exe[PATH_MAX]  ;
		ssize_t          cnt            = ::readlink("/proc/self/exe",exe,sizeof(exe)) ; exe[cnt] = 0 ;
		Dl_info          info           ;
		struct link_map* map            ; ::dladdr1( addr , &info , (void**)(&map) , RTLD_DL_LINKMAP ) ;
		const char*      file           = *map->l_name ? map->l_name : exe ;
		uintptr_t        offset         = (uintptr_t)addr - map->l_addr    ;
		//
		char             hex_offset[20] ; ::snprintf(hex_offset,sizeof(hex_offset),"0x%lx",(ulong)offset) ;
		Pipe             c2p            { New } ;                                                           // dont use Child as if called from signal handler, malloc is forbidden
		int pid = ::fork() ;
		if (!pid) {                                                                                         // child
			::close(c2p.read) ;
			if (c2p.write!=Fd::Stdout) { ::dup2(c2p.write,Fd::Stdout) ; ::close(c2p.write) ; }
			::close(Fd::Stdin) ;                                                                            // no input
			del_env("LD_PRELOAD") ;                                                                         // ensure no recursive errors
			del_env("LD_AUDIT"  ) ;                                                                         // .
			const char* args[] = { ADDR2LINE , "-f" , "-i" , "-C" , "-e" , file , hex_offset , nullptr } ;
			::execv( args[0] , const_cast<char**>(args) ) ;
			exit(Rc::System) ;                                                                              // in case exec fails
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
				/**/                        sp.file[col] = 0                                                   ;
				try                       { sp.line      = from_string<size_t>(sp.file+col+1,true/*empty_ok*/) ; }
				catch (::string const& e) { sp.line      = 0                                                   ; }
			}
			_beautify(sp.file) ;                                                   // system files may contain a lot of .., making long file names, and alignment makes all lines very long
		}
	Return :
		::close(c2p.read) ;
		::waitpid(pid,nullptr,0) ;
		return n_sp ;
	}

	void write_backtrace( Fd fd , int hide_cnt ) {
		static constexpr size_t StackSize = 100 ;
		//
		static void*    stack         [StackSize] ;     // avoid big allocation on stack
		static SrcPoint symbolic_stack[StackSize] ;     // .
		//
		int backtrace_sz = backtrace(stack,StackSize) ; // XXX! : dont know how to avoid malloc here
		int stack_sz     = 0                          ;
		for( int i : iota( hide_cnt+1 , backtrace_sz ) ) {
			stack_sz += fill_src_points( stack[i] , symbolic_stack+stack_sz , StackSize-stack_sz ) ;
			if (strcmp(symbolic_stack[stack_sz-1].func,"main")==0) break ;
		}
		size_t  wf = 0 ;
		uint8_t wl = 0 ;
		for( int i : iota(stack_sz) ) {
			uint8_t w = 0 ; for( size_t l=symbolic_stack[i].line ; l ; l/=10 ) w++ ;
			wf = ::max( wf , strnlen(symbolic_stack[i].file,PATH_MAX) ) ;
			wl = ::max( wl , w                                        ) ;
		}
		::string bt = "approximately\n" ;
		for( int i : iota(stack_sz) ) {
			/**/                        bt <<         widen(    symbolic_stack[i].file ,wf              ) ;
			if (symbolic_stack[i].line) bt <<':'   << widen(cat(symbolic_stack[i].line),wl,true/*right*/) ;
			else                        bt <<' '   << widen(""                         ,wl              ) ;
			/**/                        bt <<" : " <<           symbolic_stack[i].func                    ;
			/**/                        bt <<'\n' ;
		}
		fd.write(bt) ;
	}

#else

	void write_backtrace( Fd , int /*hide_cnt*/ ) {}

#endif
