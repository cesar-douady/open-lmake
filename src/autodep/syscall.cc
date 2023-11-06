// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <syscall.h>                   // for SYS_* macros

#include "record.hh"

::pair<char*,ssize_t> fix_cwd( char* buf , size_t buf_sz , ssize_t sz , Bool3 allocated=No ) noexcept { // allocated=Maybe means allocated of fixed size, getcwd unused if no error is generated
	if ( !buf || sz<0              ) return {buf,sz} ;                                                  // error
	if ( !Record::s_has_tmp_view() ) return {buf,sz} ;                                                  // no tmp mapping
	//
	::string const& tmp_dir  = Record::s_autodep_env().tmp_dir  ;              // we need to call auditer() even for a static field to ensure it is initialized
	::string const& tmp_view = Record::s_autodep_env().tmp_view ;              // .
	//
	if ( !::string_view(buf,buf_sz).starts_with(tmp_dir)    ) return {buf,sz} ; // no match
	if ( buf[tmp_dir.size()]!='/' && buf[tmp_dir.size()]!=0 ) return {buf,sz} ; // false match
	//
	if (!sz) sz = strlen(buf) ;
	sz += tmp_view.size() - tmp_dir.size() ;
	if (allocated==Yes) {
		buf = static_cast<char*>(realloc(buf,sz)) ;
	} else if (sz>=ssize_t(buf_sz)) {
		char  x                  ;
		char* _ [[maybe_unused]] = getcwd(&x,1) ;                              // force an error in user land as we have not enough space (a cwd cannot fit within 1 byte with its terminating null)
		if (allocated!=No) free(buf) ;
		return {nullptr,ERANGE} ;
	}
	if (tmp_view.size()>tmp_dir.size()) ::memmove( buf+tmp_view.size() , buf+tmp_dir.size() , sz-tmp_view.size()+1 ) ; // +1 for the terminating null
	/**/                                ::memcpy ( buf                 , tmp_view.c_str()   ,    tmp_view.size()   ) ;
	if (tmp_view.size()<tmp_dir.size()) ::memmove( buf+tmp_view.size() , buf+tmp_dir.size() , sz-tmp_view.size()+1 ) ; // .
	return {buf,sz} ;
}

struct SyscallDescr {
	// static data
	static ::umap<int/*syscall*/,SyscallDescr> const& s_tab() ;
	// data
	bool/*skip*/   (*entry)( void*& , Record& , pid_t , uint64_t args[6] , const char* comment ) = nullptr ;
	int64_t/*res*/ (*exit )( void*  , Record& , pid_t , int64_t res , int errno_               ) = nullptr ;
	uint8_t        prio                                                                          = 0       ;
	bool           data_access                                                                   = false   ;
	const char*    comment                                                                       = nullptr ;
} ;

template<bool At> static inline Record::Path _path( pid_t pid , uint64_t const* args ) {
	if (At) return { Fd(args[0]) , get_str(pid,args[1]) } ;
	else    return {               get_str(pid,args[0]) } ;
}

// updating args is meaningful only when processing calls to the syscall function with ld_audit & ld_preload
// when autodep is ptrace, tmp mapping is not supported and such args updating is ignored as args have been copied from tracee and are not copied back to it
template<bool At> static inline void _update( uint64_t* args , Record::Path const& p ) {
	SWEAR(p.has_at==At) ;
	if (At) { args[0] = p.at ; args[1] = reinterpret_cast<uint64_t>(p.file) ; }
	else    {                  args[0] = reinterpret_cast<uint64_t>(p.file) ; }
}

// chdir
template<bool At,bool Path> bool/*skip_syscall*/ entry_chdir( void* & ctx , Record& r , pid_t pid , uint64_t args[6] , const char* /*comment*/ ) {
	try {
		static_assert(At!=Path) ;
		if (Path) { Record::ChDir* cd = new Record::ChDir( r , {_path<At>(pid,args+0)} ) ; ctx = cd ; _update<At>(args+0,*cd) ; }
		else      { Record::ChDir* cd = new Record::ChDir( r , {Fd(args[0])          } ) ; ctx = cd ;                           }
	} catch (int) {}
	return false ;
}
int64_t/*res*/ exit_chdir( void* ctx , Record& r , pid_t pid , int64_t res , int /*errno_*/ ) {
	if (!ctx) return res ;
	Record::ChDir* cd = static_cast<Record::ChDir*>(ctx) ;
	(*cd)(r,res,pid) ;
	delete cd ;
	return res ;
}

// chmod
template<bool At,bool Path,bool Flags> bool/*skip_syscall*/ entry_chmod( void* & ctx , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		Record::Write* cm = new Record::Write( r , _path<At>(pid,args+0) , Access::Reg , Flags&&(args[2+At]&AT_SYMLINK_NOFOLLOW) , comment ) ;
		ctx = cm ;
		_update<At>(args+0,*cm) ;
	} catch (int) {}
	return false ;
}
int64_t/*res*/ exit_chmod( void* ctx , Record& r , pid_t , int64_t res , int errno_ ) {
	if (!ctx) return res ;
	Record::Write* cm = static_cast<Record::Write*>(ctx) ;
	(*cm)(r,res,errno_==ENOENT) ;
	delete cm ;
	return res ;
}

// execve
// must be called before actual syscall execution as after execution, info is no more available
template<bool At,bool Flags> bool/*skip_syscall*/ entry_execve( void* & /*ctx*/ , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		Record::Exec e{ r , _path<At>(pid,args+0) , Flags&&(args[3+At]&AT_SYMLINK_NOFOLLOW) , comment } ;
		_update<At>(args+0,e) ;
	} catch (int) {}
	return false ;
}

// getcwd
// getcwd is only necessary if tmp is mapped (not in table with ptrace)
bool/*skip_syscall*/ entry_getcwd( void* & ctx , Record& , pid_t , uint64_t args[6] , const char* /*comment*/ ) {
	size_t* sz = new size_t{args[1]} ;
	ctx = sz ;
	return false ;
}
int64_t/*res*/ exit_getcwd( void* ctx , Record& , pid_t pid , int64_t res , int errno_ ) {
	if (errno_                  ) return res ;                                             // in case of error, man getcwd says buffer is undefined => nothing to do
	if (Record::s_has_tmp_view()) return res ;                                             // no tmp mapping                                        => nothing to do
	SWEAR(pid==0,pid) ;                                                                    // tmp mapping is not supported with ptrace (need to report fixed result to caller)
	char*   buf = reinterpret_cast<char*  >(res) ;
	size_t* sz  = reinterpret_cast<size_t*>(ctx) ;
	res = fix_cwd(buf,*sz,res).second ;
	delete sz ;
	return res ;
}

// hard link
template<bool At,bool Flags> bool/*skip_syscall*/ entry_lnk( void* & ctx , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		Record::Lnk* l = new Record::Lnk( r , _path<At>(pid,args+0) , _path<At>(pid,args+1+At) , Flags?args[2+At*2]:0 , comment ) ;
		ctx = l ;
		_update<At>(args+0,l->src) ;
		_update<At>(args+2,l->dst) ;
	} catch (int) {}
	return false ;
}
int64_t/*res*/ exit_lnk( void* ctx , Record& r , pid_t /*pid */, int64_t res , int errno_ ) {
	if (!ctx) return res ;
	Record::Lnk* l = static_cast<Record::Lnk*>(ctx) ;
	(*l)(r,res,errno_==ENOENT) ;
	delete l ;
	return res ;
}

// open
template<bool At> bool/*skip_syscall*/ entry_open( void* & ctx , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		Record::Open* o = new Record::Open( r , _path<At>(pid,args+0) , args[1+At]/*flags*/ , comment ) ;
		ctx = o ;
		_update<At>(args+0,*o) ;
	}
	catch (int) {}
	return false ;
}
int64_t/*res*/ exit_open( void* ctx , Record& r , pid_t /*pid*/ , int64_t res , int errno_ ) {
	if (!ctx) return res ;
	Record::Open* o = static_cast<Record::Open*>(ctx) ;
	(*o)( r , false/*has_fd*/ , res , errno_==ENOENT ) ;
	delete o ;
	return res ;
}

// read_lnk
template<bool At> bool/*skip_syscall*/ entry_read_lnk( void* & ctx , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	using Len = MsgBuf::Len ;
	try {
		if ( At && Fd(args[0])==Backdoor ) {
			::string data = get_str( pid , args[1] , sizeof(Len)+get<size_t>(pid,args[1]) ) ; // for backdoor accesses, file name starts with its length
			::string buf  ( args[3] , char(0) )                                             ;
			ssize_t  len  = r.backdoor( data.data(), buf.data() , buf.size() )              ;
			buf.resize(len) ;
			put_str( pid , args[1+At] , buf ) ;
			return true/*skip_syscall*/ ;                                      // we just executed the syscall, do not do the real one
		} else {
			Record::ReadLnk* rl = new Record::ReadLnk( r , _path<At>(pid,args+0) , comment ) ;
			ctx = rl ;
			_update<At>(args+0,*rl) ;
		}
	} catch (int) {}
	return false ;
}
int64_t/*res*/ exit_read_lnk( void* ctx , Record& r , pid_t pid , int64_t res , int /*errno_*/ ) {
	if (!ctx) return res ;                                                                         // backdoor case
	Record::ReadLnk* rl = static_cast<Record::ReadLnk*>(ctx) ;
	SWEAR( pid==0 || !Record::s_has_tmp_view() , pid ) ;                       // tmp mapping is not supported with ptrace (need to report new value to caller)
	(*rl)(r,res) ;
	delete rl ;
	return res ;
}

// rename
template<bool At,bool Flags> bool/*skip_syscall*/ entry_rename( void* & ctx , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		Record::Rename* rn = new Record::Rename( r , _path<At>(pid,args+0) , _path<At>(pid,args+1+At) , Flags?args[2+At*2]:0 , comment ) ;
		ctx = rn ;
		_update<At>(args+0,rn->src) ;
		_update<At>(args+2,rn->dst) ;
	} catch (int) {}
	return false ;
}
int64_t/*res*/ exit_rename( void* ctx , Record& r , pid_t /*pid*/ , int64_t res , int errno_ ) {
	if (!ctx) return res ;
	Record::Rename* rn = static_cast<Record::Rename*>(ctx) ;
	(*rn)(r,res,errno_==ENOENT) ;
	delete rn ;
	return res ;
}

// symlink
template<bool At> bool/*skip_syscall*/ entry_sym_lnk( void* & ctx , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		Record::Write* sl = new Record::Write( r , _path<At>(pid,args+1) , {}/*read*/ , true/*no_follow*/ , comment ) ;
		ctx = sl ;
		_update<At>(args+1,*sl) ;
	} catch (int) {}
	return false ;
}
int64_t/*res*/ exit_sym_lnk( void* ctx , Record& r , pid_t , int64_t res , int /*errno_*/ ) {
	if (!ctx) return res ;
	Record::Write* sl = static_cast<Record::Write*>(ctx) ;
	(*sl)(r,res) ;
	delete sl ;
	return res ;
}

// unlink
template<bool At,bool Flags> bool/*skip_syscall*/ entry_unlink( void* & ctx , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		Record::Unlink* u = new Record::Unlink( r , _path<At>(pid,args+0) , Flags&&(args[1+At]&AT_REMOVEDIR) , comment ) ;
		ctx = u ;
		_update<At>(args+0,*u) ;
	} catch (int) {}
	return false ;
}
int64_t/*res*/ exit_unlink( void* ctx , Record& r , pid_t , int64_t res , int /*errno_*/ ) {
	if (!ctx) return res ;
	Record::Unlink* u = static_cast<Record::Unlink*>(ctx) ;
	(*u)(r,res) ;
	delete u ;
	return res ;
}

// access
static constexpr int FlagAlways = -1 ;
static constexpr int FlagNever  = -2 ;
template<bool At,int FlagArg> bool/*skip_syscall*/ entry_stat( void* & /*ctx*/ , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	bool no_follow ;
	switch (FlagArg) {
		case FlagAlways : no_follow = true                                   ; break ;
		case FlagNever  : no_follow = false                                  ; break ;
		default         : no_follow = args[FlagArg+At] & AT_SYMLINK_NOFOLLOW ;
	}
	try {
		Record::Stat s{ r , _path<At>(pid,args+0) , no_follow , comment } ;
		_update<At>(args+0,s) ;
		s(r) ;
	} catch (int) {}
	return false ;
}
template<bool At,int FlagArg> bool/*skip_syscall*/ entry_solve( void* & /*ctx*/ , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	bool no_follow ;
	switch (FlagArg) {
		case FlagAlways : no_follow = true                                   ; break ;
		case FlagNever  : no_follow = false                                  ; break ;
		default         : no_follow = args[FlagArg+At] & AT_SYMLINK_NOFOLLOW ;
	}
	try {
		Record::Solve s{ r , _path<At>(pid,args+0) , no_follow , comment } ;
		_update<At>(args+0,s) ;
	} catch (int) {}
	return false ;
}

::umap<int/*syscall*/,SyscallDescr> const& SyscallDescr::s_tab() {             // use a func to access s_tab to avoid accessing it before it is constructed
	static ::umap<int/*syscall*/,SyscallDescr> const s_tab = {
		{-1,{}}                                                                // first entry is ignored so each active line contains a ','
	#ifdef SYS_faccessat
	,	{ SYS_faccessat         , { entry_stat    <true /*At*/              ,2             > , nullptr       , 2 , false , "faccessat"         } }
	#endif
	#ifdef SYS_access
	,	{ SYS_access            , { entry_stat    <false/*At*/              ,FlagNever     > , nullptr       , 1 , false , "access"            } }
	#endif
	#ifdef SYS_faccessat2
	,	{ SYS_faccessat2        , { entry_stat    <true /*At*/              ,2             > , nullptr       , 2 , false , "faccessat2"        } }
	#endif
	#ifdef SYS_chdir
	,	{ SYS_chdir             , { entry_chdir   <false/*At*/,true /*Path*/               > , exit_chdir    , 1 , true                        } }
	#endif
	#ifdef SYS_fchdir
	,	{ SYS_fchdir            , { entry_chdir   <true /*At*/,false/*Path*/               > , exit_chdir    , 1 , true                        } }
	#endif
	#ifdef SYS_chmod
	,	{ SYS_chmod             , { entry_chmod   <false/*At*/,true/*Path*/ ,false/*Flags*/> , exit_chmod    , 1 , true  , "chmod"             } }
	#endif
	#ifdef SYS_fchmod
	,	{ SYS_fchmod            , { entry_chmod   <true /*At*/,false/*Path*/,false/*Flags*/> , exit_chmod    , 1 , true  , "fchmod"            } }
	#endif
	#ifdef SYS_fchmodat
	,	{ SYS_fchmodat          , { entry_chmod   <true /*At*/,true /*Path*/,true /*Flags*/> , exit_chmod    , 1 , true  , "fchmodat"          } }
	#endif
	#ifdef SYS_execve
	,	{ SYS_execve            , { entry_execve  <false/*At*/              ,false/*Flags*/> , nullptr       , 1 , true  , "execve"            } }
	#endif
	#ifdef SYS_execveat
	,	{ SYS_execveat          , { entry_execve  <true /*At*/              ,true /*Flags*/> , nullptr       , 1 , true  , "execveat"          } }
	#endif
	#if defined(SYS_getcwd) && !defined(PTRACE)                                                                                                    // tmp mapping is not supported with ptrace
	,	{ SYS_getcwd            , { entry_getcwd                                             , exit_getcwd   , 1 , true                        } }
	#endif
	#ifdef SYS_link
	,	{ SYS_link              , { entry_lnk     <false/*At*/              ,false/*Flags*/> , exit_lnk      , 1 , true  , "link"              } }
	#endif
	#ifdef SYS_linkat
	,	{ SYS_linkat            , { entry_lnk     <true /*At*/              ,true /*Flags*/> , exit_lnk      , 1 , true  , "linkat"            } }
	#endif
	#ifdef SYS_mkdir
	,	{ SYS_mkdir             , { entry_solve   <false/*At*/              ,FlagNever     > , nullptr       , 1 , false , "mkdir"             } }
	#endif
	#ifdef SYS_mkdirat
	,	{ SYS_mkdirat           , { entry_solve   <true /*At*/              ,FlagNever     > , nullptr       , 1 , false , "mkdirat"           } }
	#endif
	#ifdef SYS_name_to_handle_at
	,	{ SYS_name_to_handle_at , { entry_open    <true /*At*/                             > , exit_open     , 1 , true  , "name_to_handle_at" } }
	#endif
	#ifdef SYS_open
	,	{ SYS_open              , { entry_open    <false/*At*/                             > , exit_open     , 2 , true  , "open"              } }
	#endif
	#ifdef SYS_openat
	,	{ SYS_openat            , { entry_open    <true /*At*/                             > , exit_open     , 2 , true  , "openat"            } }
	#endif
	#ifdef SYS_openat2
	,	{ SYS_openat2           , { entry_open    <true /*At*/                             > , exit_open     , 2 , true  , "openat2"           } }
	#endif
	#ifdef SYS_open_tree
	,	{ SYS_open_tree         , { entry_stat    <true /*At*/              ,1             > , nullptr       , 1 , false , "open_tree"         } }
	#endif
	#ifdef SYS_readlink
	,	{ SYS_readlink          , { entry_read_lnk<false/*At*/                             > , exit_read_lnk , 2 , true  , "readlink"          } }
	#endif
	#ifdef SYS_readlinkat
	,	{ SYS_readlinkat        , { entry_read_lnk<true /*At*/                             > , exit_read_lnk , 2 , true  , "readlinkat"        } }
	#endif
	#if SYS_rename
	,	{ SYS_rename            , { entry_rename  <false/*At*/              ,false/*Flags*/> , exit_rename   , 1 , true  , "rename"            } }
	#endif
	#ifdef SYS_renameat
	,	{ SYS_renameat          , { entry_rename  <true /*At*/              ,false/*Flags*/> , exit_rename   , 1 , true  , "renameat"          } }
	#endif
	#ifdef SYS_renameat2
	,	{ SYS_renameat2         , { entry_rename  <true /*At*/              ,true /*Flags*/> , exit_rename   , 1 , true  , "renameat2"         } }
	#endif
	#ifdef SYS_rmdir
	,	{ SYS_rmdir             , { entry_stat    <false/*At*/              ,FlagAlways    > , nullptr       , 1 , false , "rmdir"             } }
	#endif
	#ifdef SYS_stat
	,	{ SYS_stat              , { entry_stat    <false/*At*/              ,FlagNever     > , nullptr       , 2 , false , "stat"              } }
	#endif
	#ifdef SYS_stat64
	,	{ SYS_stat64            , { entry_stat    <false/*At*/              ,FlagNever     > , nullptr       , 1 , false , "stat64"            } }
	#endif
	#ifdef SYS_fstatat64
	,	{ SYS_fstatat64         , { entry_stat    <true /*At*/              ,2             > , nullptr       , 1 , false , "fstatat64"         } }
	#endif
	#ifdef SYS_lstat
	,	{ SYS_lstat             , { entry_stat    <false/*At*/              ,FlagAlways    > , nullptr       , 2 , false , "lstat"             } }
	#endif
	#ifdef SYS_lstat64
	,	{ SYS_lstat64           , { entry_stat    <false/*At*/              ,FlagAlways    > , nullptr       , 1 , false , "lstat64"           } }
	#endif
	#ifdef SYS_statx
	,	{ SYS_statx             , { entry_stat    <true /*At*/              ,1             > , nullptr       , 1 , false , "statx"             } }
	#endif
	#if SYS_newfstatat
	,	{ SYS_newfstatat        , { entry_stat    <true /*At*/              ,2             > , nullptr       , 2 , false , "newfstatat"        } }
	#endif
	#ifdef SYS_oldstat
	,	{ SYS_oldstat           , { entry_stat    <false/*At*/              ,FlagNever     > , nullptr       , 1 , false , "oldstat"           } }
	#endif
	#ifdef SYS_oldlstat
	,	{ SYS_oldlstat          , { entry_stat    <false/*At*/              ,FlagAlways    > , nullptr       , 1 , false , "oldlstat"          } }
	#endif
	#ifdef SYS_symlink
	,	{ SYS_symlink           , { entry_sym_lnk <false/*At*/                             > , exit_sym_lnk  , 1 , true  , "symlink"           } }
	#endif
	#ifdef SYS_symlinkat
	,	{ SYS_symlinkat         , { entry_sym_lnk <true /*At*/                             > , exit_sym_lnk  , 1 , true  , "symlinkat"         } }
	#endif
	#ifdef SYS_unlink
	,	{ SYS_unlink            , { entry_unlink  <false/*At*/              ,false/*Flags*/> , exit_unlink   , 1 , true  , "unlink"            } }
	#endif
	#ifdef SYS_unlinkat
	,	{ SYS_unlinkat          , { entry_unlink  <true /*At*/              ,true /*Flags*/> , exit_unlink   , 1 , true  , "unlinkat"          } }
	#endif
	} ;
	return s_tab ;
}
