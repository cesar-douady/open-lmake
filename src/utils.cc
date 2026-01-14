// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "utils.hh"

#if HAS_CLOSE_RANGE
    #include <linux/close_range.h>
#endif

#if HAS_STACKTRACE        // must be after utils.hh so that HAS_STACKTRACE is defined
	#include <stacktrace>
	using std::stacktrace ;
#else
	#include <execinfo.h> // backtrace
#endif

#include "disk.hh"
#include "fd.hh"
#include "hash.hh"
#include "process.hh"
#include "time.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Time ;

//
// sanitize
//

#pragma GCC visibility push(default) // force visibility of functions defined hereinafter, until the corresponding pop
extern "C" {
	const char* __asan_default_options () { return "verify_asan_link_order=0,detect_leaks=0" ; }
	const char* __ubsan_default_options() { return "halt_on_error=1"                         ; }
	const char* __tsan_default_options () { return "report_signal_unsafe=0"                  ; }
}
#pragma GCC visibility pop

//
// File
//

template<class F> size_t _File<F>::hash() const {
	Fnv fnv ;                                          // good enough
	fnv += at                           ;
	fnv += ::hash<::decay_t<F>>()(file) ;
	return +fnv ;
}
template size_t _File<::string       >::hash() const ; // explicit instantiation
template size_t _File<::string const&>::hash() const ;
template size_t _File<::string_view  >::hash() const ;

//
// Fd
//

::string& operator+=( ::string& os , Fd   const& fd ) { return fd.append_to_str( os , "Fd"   ) ; }
::string& operator+=( ::string& os , AcFd const& fd ) { return fd.append_to_str( os , "AcFd" ) ; }

int Fd::_s_mk_fd( FileRef file , Action action ) {
	bool creat = action.flags&O_CREAT ;
	if (creat) {
		SWEAR(   action.mod!=mode_t(-1) , file,action.flags ) ;                                                                          // mod must be specified if creating
		SWEAR( !(action.mod&~0777)      , file,action.mod   ) ;                                                                          // mod must only specify perm
	}
	if (action.nfs_guard) {
		if (action.flags&O_DIRECTORY) {
			/**/                                                                 action.nfs_guard->access_dir_s({file.at,with_slash(file.file)}) ;
		} else {
			if ( (action.flags&O_ACCMODE)!=O_WRONLY || !(action.flags&O_TRUNC) ) action.nfs_guard->access      (                    file       ) ;
			if ( (action.flags&O_ACCMODE)!=O_RDONLY                            ) action.nfs_guard->change      (                    file       ) ;
		}
	}
	int  res   ;
	bool first = true ;
Retry :
	if      (+file.file      ) res = ::openat( file.at , file.file.c_str() , action.flags|O_CLOEXEC , action.mod ) ;
	else if (file.at==Fd::Cwd) res = ::openat( file.at , "."               , action.flags|O_CLOEXEC , action.mod ) ;
	else                       res = ::dup   ( file.at                                                           ) ;
	if (res<0) {
		if ( errno==ENOENT && creat && first && action.mk_dir ) {
			if (action.flags&O_TMPFILE) mk_dir_s ( {file.at,with_slash(file.file)} , {.force=action.force,.perm_ext=action.perm_ext} ) ;
			else                        dir_guard(  file                           , {.force=action.force,.perm_ext=action.perm_ext} ) ;
			first = false ;                                                                                                              // ensure we retry at most once
			goto Retry ;
		}
		throw_if( !action.err_ok , "cannot open (",StrErr(),") : ",file ) ;
		return res ;
	}
	//
	if ( creat && +action.perm_ext ) {
		static mode_t umask_ = get_umask() ;
		switch (action.perm_ext) {
			case PermExt::Other : if (!((action.mod&umask_)     )) goto PermOk ; break ;
			case PermExt::Group : if (!((action.mod&umask_)&0770)) goto PermOk ; break ;
		DN}
		//
		FileStat st ; throw_unless( ::fstat(res,&st)==0 , "cannot stat (",StrErr(),") to extend permissions : ",file ) ;
		//
		mode_t usr_mod = (st.st_mode>>6)&07 ;
		mode_t new_mod = st.st_mode         ;
		switch (action.perm_ext) {
			case PermExt::Other : new_mod |= usr_mod    ; [[fallthrough]] ;
			case PermExt::Group : new_mod |= usr_mod<<3 ; break           ;
		DN}
		if (new_mod!=st.st_mode) throw_unless( ::fchmod(res,new_mod)==0 , "cannot chmod (",StrErr(),") to extend permissions : ",file ) ;
	}
PermOk :
	return res ;
}

void Fd::write(::string_view data) const {
	for( size_t cnt=0 ; cnt<data.size() ;) {
		ssize_t c = ::write( fd , data.data()+cnt , data.size()-cnt ) ;
		if (c<=0) {
			switch (errno) {
				#if EWOULDBLOCK!=EAGAIN
					case EWOULDBLOCK :
				#endif
				case EAGAIN :
				case EINTR  : c = 0 ; break                                               ;
				default     :         throw cat("cannot write (",StrErr(),") to fd ",fd ) ;
			}
		}
		cnt += c ;
	}
}

::string Fd::read(size_t sz) const {
	::string res ;
	if (sz!=Npos) {
		res.resize(sz) ;
		size_t cnt = read_to(::span(res.data(),sz)) ;
		res.resize(cnt) ;
	} else {
		size_t goal_sz = 4096 ;
		for( size_t cnt=0 ;;) {
			res.resize(goal_sz) ;
			ssize_t c = ::read( fd , &res[cnt] , goal_sz-cnt ) ;
			if (c<=0) {
				if (c<0) {
					switch (errno) {
						#if EWOULDBLOCK!=EAGAIN
							case EWOULDBLOCK :
						#endif
						case EAGAIN     :
						case EINTR      : continue                                            ; // retry
						case ECONNRESET : break                                               ; // process as eof as this appears with sockets when peer dies abruptly
						default         : throw cat("cannot read (",StrErr(),") from fd ",fd) ; // consider ECONNRESET as eof as this appears with sockets when peer dies abruptly
					}
				}
				res.resize(cnt) ;                                                               // eof
				break ;
			}
			cnt += c ;
			if (cnt==goal_sz) goal_sz += goal_sz ;                                              // increase buf size as long as it is filled up
			else              goal_sz += c       ;                                              // we reach system limit, no interest to read more
		}
	}
	return res ;
}

size_t Fd::read_to(::span<char> dst) const {
	for( size_t pos=0 ; pos<dst.size() ;) {
		ssize_t cnt = ::read( fd , &dst[pos] , dst.size()-pos ) ;
		if (cnt<=0) {
			if (cnt<0) {
				switch (errno) {
					#if EWOULDBLOCK!=EAGAIN
						case EWOULDBLOCK :
					#endif
					case EAGAIN     :
					case EINTR      : continue                                                  ; // retry
					case ECONNRESET : break                                                     ; // process as eof as this appears with sockets when peer dies abruptly
					default         : throw cat("cannot read ",dst.size()," bytes from fd ",fd) ;
				}
			}
			return pos ;                                                                          // eof
		}
		pos += cnt ;
	}
	return dst.size() ;
}

::vector_s Fd::read_lines(bool partial_ok) const {
	/**/                        if (!self               ) return {} ;
	::string content = read() ; if (!content            ) return {} ;
	                            if (content.back()=='\n') content.pop_back() ;
	                            else                      throw_if( !partial_ok , "partial last line" ) ;
	/**/                                                  return split(content,'\n') ;
}

//
// NfsGuard
//

static void _nfs_guard_protect(FileRef dir_s) {
	::close( ::openat( dir_s.at , dir_s.file.c_str() , O_RDONLY|O_DIRECTORY ) ) ;
}

void NfsGuardDir::access(FileRef path) {
	if ( is_dir_name(path.file) ? path.file.ends_with("../") : path.file.ends_with("..") ) return ; // cannot go uphill
	if ( !has_dir(path.file)                                                             ) return ;
	access_dir_s({path.at,dir_name_s(path.file)}) ;
}

void NfsGuardDir::access_dir_s(FileRef dir_s) {
	access(dir_s) ;                                                       // we opend dir, we must ensure its dir is up-to-date w.r.t. NFS
	if (fetched_dirs_s.emplace(dir_s).second) _nfs_guard_protect(dir_s) ; // open to force NFS close to open coherence, close is useless
}

void NfsGuardDir::change(FileRef path) {
	if ( is_dir_name(path.file) ? path.file.ends_with("../") : path.file.ends_with("..") ) return ; // cannot go uphill
	if ( !has_dir(path.file)                                                             ) return ;
	File dir_s { path.at , dir_name_s(path.file) } ;
	access_dir_s(dir_s) ;
	to_stamp_dirs_s.emplace(::move(dir_s)) ;
}

void NfsGuardDir::flush() {
	for( auto const& f : to_stamp_dirs_s ) _nfs_guard_protect(f) ;
	to_stamp_dirs_s.clear() ;
}

//
// NfsGuardLock
//

static constexpr Delay FileLockFileTimeout { 10 } ; // a file based lock that is that old is ignored (fd based lcoks are spontaneously released when process dies)

void _LockerFcntl::lock() {
	struct flock lock {
		.l_type   = F_WRLCK
	,	.l_whence = SEEK_SET
	,	.l_start  = 0
	,	.l_len    = 1 // ensure a lock exists even if file is empty
	,	.l_pid    = 0
	} ;
	while (::fcntl(fd,F_SETLKW,&lock)!=0) SWEAR_PROD( errno==EINTR , fd,StrErr() ) ;
}

void _LockerFlock::lock() {
	while (::flock(fd,LOCK_EX)!=0) SWEAR_PROD( errno==EINTR , fd,StrErr() ) ;
}

// use different names for files based and fd based to avoid problems when reconfiguring (fd based leave their files)
_LockerFile::_LockerFile(FileRef f) : spec{f.at,f.file+"_tmp"} , date{Pdate(New).val()} {}

void _LockerFile::unlock( bool err_ok , bool is_dir ) {
	int rc = ::unlinkat( spec.at , spec.file.c_str() , is_dir?AT_REMOVEDIR:0 ) ;
	if (!err_ok) SWEAR( rc==0 , spec,StrErr() ) ;
}

void _LockerFile::keep_alive() {
	Pdate now { New } ;
	if (now<Pdate(New,date)+FileLockFileTimeout/2) return ;
	//
	touch(spec) ;
}

_LockerExcl::Trial _LockerExcl::try_lock() {
	int fd = ::openat( spec.at , spec.file.c_str() , O_WRONLY|O_CREAT|O_EXCL , 0000/*mod*/ ) ; // file is a marker only, it is not meant to be read/written
	if (fd>=0        ) { ::close(fd) ; return Trial::Locked ; }
	if (errno==EEXIST)                 return Trial::Retry  ;                                  // lock is held by someone else
	if (errno==ENOENT)                 return Trial::NoDir  ;                                  // lock is held by someone else
	/**/                               return Trial::Fail   ;                                  // not lockable
}

_LockerSymlink::Trial _LockerSymlink::try_lock() {
	int rc = ::symlinkat( "?" , spec.at,spec.file.c_str() ) ;
	if (rc==0        ) return Trial::Locked ;
	if (errno==EEXIST) return Trial::Retry  ; // lock is held by someone else
	if (errno==ENOENT) return Trial::NoDir  ;
	/**/               return Trial::Fail   ; // not lockable
}

_LockerLink::Trial _LockerLink::try_lock() {
	if (!tmp    ) tmp = cat(spec.file,'.',host(),'.',::getpid()) ;
	if (!has_tmp) {
		int fd = ::openat( spec.at , tmp.c_str() , O_WRONLY|O_CREAT , 0000/*mod*/ ) ;        // file is a marker only, it is not meant to be read/written
		if (fd<0) {
			if (errno==ENOENT) return Trial::NoDir ;
			else               return Trial::Fail  ;                                         // not lockable
		}
		::close(fd) ;
		has_tmp = true ;
	}
	int  rc     = ::linkat( spec.at,tmp.c_str() , spec.at,spec.file.c_str() , 0/*flags*/ ) ;
	bool exists = errno==EEXIST                                                            ;
	if (rc!=0) {                                                                             // it seems (cf man 2 open in section relative to O_EXCL) linkat may succeed while returning an error
		FileStat fs ;
		int      rc = ::fstatat( spec.at,tmp.c_str() , &fs , AT_SYMLINK_NOFOLLOW ) ; SWEAR( rc==0 , spec,StrErr() ) ;
		if (fs.st_nlink==2) rc = 0 ;  // NOLINT(clang-analyzer-deadcode.DeadStores) false positive /!\ if link count has increased, the link actually occurred
	}
	if (rc==0) {
		rc = ::unlinkat( spec.at , tmp.c_str() , 0/*flags*/ ) ; SWEAR( rc==0 , spec ) ;
		return Trial::Locked ;
	}
	if (exists) return Trial::Retry ; // if file exists, lock is held by someone else
	else        return Trial::Fail  ;
}

_LockerMkdir::Trial _LockerMkdir::try_lock() {
	int rc = ::mkdirat( spec.at , spec.file.c_str() , 0777/*mod*/ ) ;
	if (rc==0        ) return Trial::Locked ;
	if (errno==EEXIST) return Trial::Retry  ; // lock is held by someone else
	if (errno==ENOENT) return Trial::NoDir  ;
	/**/               return Trial::Fail   ; // not lockable
}

void _LockerMkdir::unlock(bool err_ok) {
	_LockerFile::unlock(err_ok,true/*is_dir*/) ;
}

// cannot put this cxtor in .hh file as Pdate is not available
template<class Locker> _FileLockFile<Locker>::_FileLockFile( FileRef f , Action a ) : Locker{f} {
	using Trial = typename Locker::Trial ;
	if (!spec) return ;
	//
	Pdate start { New } ;
	for(;;) {
		Pdate prev { New , FileInfo(spec).date.val() } ;                                                                     // lock dates are actually Pdate's
		Pdate now  { New                             } ;
		if ( +prev && now-prev>FileLockFileTimeout ) unlock(true/*err_ok*/) ;                                                // XXX! : find a way for this timeout-unlock to be atomic
		switch (try_lock()) {
			case Trial::Locked : touch    (spec)                           ;                                        return ;
			case Trial::NoDir  : dir_guard(spec)                           ;                                        break  ;
			case Trial::Retry  : ::min( now-start , Delay(1) ).sleep_for() ;                                        break  ; // ensure logarithmic trials, but try at least every second
			case Trial::Fail   : throw_if( !a.err_ok , "cannot create lock (",StrErr(),") ",spec ) ; spec.at = {} ; return ; // if err_ok, object is built empty
		DF}                                                                                                                  // NO_COV
	}
}

template _FileLockFile<_LockerExcl   >::_FileLockFile( FileRef , Action ) ; // explicit instantiation
template _FileLockFile<_LockerSymlink>::_FileLockFile( FileRef , Action ) ; // .
template _FileLockFile<_LockerLink   >::_FileLockFile( FileRef , Action ) ; // .
template _FileLockFile<_LockerMkdir  >::_FileLockFile( FileRef , Action ) ; // .

NfsGuardLock::NfsGuardLock( FileSync fs , FileRef file , Action a ) : NfsGuard{fs} {
	switch (fs) {                                                                             // PER_FILE_SYNC : add entry here
		// XXX/ : _LockerFcntl has been observed to takes 10's of seconds on manual trials on rocky9/NFSv4, but seems to exhibit very good behavior with a real lmake case
		// XXX/ : _LockerFlock has been observed as not working with rocky9/NFSv4 despite NVS being configured to support flock
		// XXX/ : _LockerMkdir has been observed as not working with rocky9/NFSv4
		// for each FileSync mechanism, we can choose any of the variant alternative (cf struct FileLock in utils.hh)
		// /!\ monostate fakes locks, for experimental purpose only
		case FileSync::None : _FileLock::emplace<_FileLockFd<_LockerFcntl>>(file,a) ; break ;
		case FileSync::Dir  : _FileLock::emplace<_FileLockFd<_LockerFcntl>>(file,a) ; break ;
		case FileSync::Sync : _FileLock::emplace<_FileLockFd<_LockerFcntl>>(file,a) ; break ;
	DF}                                                                                       // NO_COV
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
	return cat('\'',s,'\'') ;                                     // next case : single quotes around text
 DoubleQuote :
	for( char c : s ) switch (c) {
		case '!'  : goto Complex ;
		case '"'  : goto Complex ;
		case '$'  : goto Complex ;
		case '\\' : goto Complex ;
		case '`'  : goto Complex ;
	DN}
	return cat('"',s,'"') ;                                       // next case : double quotes around text
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
		if (!first.advance() && txt[pos++]!=',') goto Fail ;
		if (txt[pos++]!='"') goto Fail ;
		::string v = parse_printable<'"'>(txt,pos) ;
		if (txt[pos++]!='"') goto Fail ;
		if ( empty_ok || +v ) res.push_back(::move(v)) ;
	}
	if (txt[pos++]!=')') goto Fail ;
	return res ;
Fail :                    // NO_COV defensive programming
	throw "bad format"s ; // NO_COV .
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
Fail :                    // NO_COV defensive programming
	throw "bad format"s ; // NO_COV .
}

//
// assert
//

thread_local char t_thread_key = '?'   ;
bool              _crash_busy  = false ;

::string get_exe       () { return read_lnk(File("/proc/self/exe")) ; }
::string _crash_get_now() { return Pdate(New).str(3/*prec*/)        ; } // NO_COV for debug only

// START_OF_NO_COV for debug only

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
			if ( size_t l=it->source_line() )   bt <<':'   << widen(cat(l)                     ,wl,true/*right*/) ;
			else                                bt <<' '   << widen(""                         ,wl              ) ;
			/**/                                bt <<" : " <<       it->description()                             ;
			/**/                                bt <<'\n'                                                         ;
		}
		fd.write(bt) ;
	}

#else

	// if ::stacktrace is not available, try to mimic using addr2line, but this is of much lower quality :
	// - sometimes, the function is completely off
	// - quite often, line number is off by 1 or 2, either direction
	// however, this is better than nothing

	static size_t/*len*/ _beautify(char* file_name) {                     // does not call malloc for use in src_point
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
							if (in!=file_name) continue ;                 // 2 consecutive /, ignore 2nd if not first char
							min = file_name+1 ;
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
		return out-file_name ;
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
			if (ADDR2LINE[0]) ::execv( args[0] , const_cast<char**>(args) ) ;
			// if compiled in addr2line does not work, try standard path, maybe we'll find a working one
			::vector_s std_path = split(STD_PATH,':') ;
			for( ::string const& path_entry : std_path ) {
				if (!path_entry) continue ;
				::string std_addr2line = path_entry+"/addr2line" ;                                          // bits to hold c-string
				args[0] = std_addr2line.c_str() ;
				::execv( args[0] , const_cast<char**>(args) ) ;
			}
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
			_beautify(sp.file) ;                                                   // system files may contain a lot of .., making long filenames, and alignment makes all lines very long
		}
	Return :
		::close(c2p.read) ;
		::waitpid( pid , nullptr/*wstatus*/ , 0/*options*/ ) ;
		return n_sp ;
	}

	void write_backtrace( Fd fd , int hide_cnt ) {
		static constexpr size_t StackSize = 100 ;
		//
		static void*    stack         [StackSize] ;       // avoid big allocation on stack
		static SrcPoint symbolic_stack[StackSize] ;       // .
		//
		int backtrace_sz = ::backtrace(stack,StackSize) ; // XXX! : dont know how to avoid malloc here
		int stack_sz     = 0                            ;
		for( int i : iota( hide_cnt+1 , backtrace_sz ) ) {
			stack_sz += fill_src_points( stack[i] , symbolic_stack+stack_sz , StackSize-stack_sz ) ;
			if (::strcmp(symbolic_stack[stack_sz-1].func,"main")==0) break ;
		}
		size_t  wf = 0 ;
		uint8_t wl = 0 ;
		for( int i : iota(stack_sz) ) {
			uint8_t w = 0 ; for( size_t l=symbolic_stack[i].line ; l ; l/=10 ) w++ ;
			wf = ::max( wf , ::strnlen(symbolic_stack[i].file,PATH_MAX) ) ;
			wl = ::max( wl , w                                          ) ;
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

#endif

// END_OF_NO_COV

//
// mutexes
//

thread_local MutexLvl t_mutex_lvl = MutexLvl::None ;

//
// miscellaneous
//

::string& operator+=( ::string& os , StrErr const& se ) {
	return os << ::string(se) ;
}

::string const& host() {
	static ::string s_res = []()->::string {
		char buf[HOST_NAME_MAX+1] ;
		int  rc                   = ::gethostname( buf , sizeof(buf) ) ; SWEAR( rc==0 , errno ) ;
		return buf ;
	}() ;
	return s_res ;
}

::string const& mail() {
	static ::string s_res = cat(::getuid(),'@',host()) ;
	return s_res ;
}
