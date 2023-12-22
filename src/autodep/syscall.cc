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
	static constexpr long NSyscalls = 1024 ;                                   // must larger than higher syscall number, 1024 is plenty, actual upper value is around 450
	using Tab = ::array<SyscallDescr,NSyscalls> ;
	// static data
	static Tab const& s_tab() ;
	// accesses
	constexpr bool operator+() const { return prio    ; }                      // prio=0 means entry is not allocated
	constexpr bool operator!() const { return !+*this ; }
	// data
	bool/*skip*/   (*entry)( void*& , Record& , pid_t , uint64_t args[6] , const char* comment ) = nullptr ;
	int64_t/*res*/ (*exit )( void*  , Record& , pid_t , int64_t res                            ) = nullptr ;
	uint8_t        prio                                                                          = 0       ; // prio for libseccomp (0 means entry is not allocated)
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
int64_t/*res*/ exit_chdir( void* ctx , Record& r , pid_t pid , int64_t res ) {
	if (!ctx) return res ;
	Record::ChDir* cd = static_cast<Record::ChDir*>(ctx) ;
	(*cd)(r,res,pid) ;
	delete cd ;
	return res ;
}

// chmod
template<bool At,bool Path,bool Flags> bool/*skip_syscall*/ entry_chmod( void* & ctx , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		Record::Chmod* cm = new Record::Chmod( r , _path<At>(pid,args+0) , args[1+At]&S_IXUSR , Flags&&(args[2+At]&AT_SYMLINK_NOFOLLOW) , comment ) ;
		ctx = cm ;
		_update<At>(args+0,*cm) ;
	} catch (int) {}
	return false ;
}
int64_t/*res*/ exit_chmod( void* ctx , Record& r , pid_t , int64_t res ) {
	if (!ctx) return res ;
	Record::Chmod* cm = static_cast<Record::Chmod*>(ctx) ;
	(*cm)(r,res) ;
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
int64_t/*res*/ exit_getcwd( void* ctx , Record& , pid_t pid , int64_t res ) {
	if (!res                     ) return res ;                                            // in case of error, man getcwd says buffer is undefined => nothing to do
	if (!Record::s_has_tmp_view()) return res ;                                            // no tmp mapping                                        => nothing to do
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
int64_t/*res*/ exit_lnk( void* ctx , Record& r , pid_t /*pid */, int64_t res ) {
	if (!ctx) return res ;
	Record::Lnk* l = static_cast<Record::Lnk*>(ctx) ;
	(*l)(r,res) ;
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
int64_t/*res*/ exit_open( void* ctx , Record& r , pid_t /*pid*/ , int64_t res ) {
	if (!ctx) return res ;
	Record::Open* o = static_cast<Record::Open*>(ctx) ;
	(*o)( r , res ) ;
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
int64_t/*res*/ exit_read_lnk( void* ctx , Record& r , pid_t pid , int64_t res ) {
	if (!ctx) return res ;                                                        // backdoor case
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
int64_t/*res*/ exit_rename( void* ctx , Record& r , pid_t /*pid*/ , int64_t res ) {
	if (!ctx) return res ;
	Record::Rename* rn = static_cast<Record::Rename*>(ctx) ;
	(*rn)(r,res) ;
	delete rn ;
	return res ;
}

// symlink
template<bool At> bool/*skip_syscall*/ entry_sym_lnk( void* & ctx , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		Record::Symlnk* sl = new Record::Symlnk( r , _path<At>(pid,args+1) , comment ) ;
		ctx = sl ;
		_update<At>(args+1,*sl) ;
	} catch (int) {}
	return false ;
}
int64_t/*res*/ exit_sym_lnk( void* ctx , Record& r , pid_t , int64_t res ) {
	if (!ctx) return res ;
	Record::Symlnk* sl = static_cast<Record::Symlnk*>(ctx) ;
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
int64_t/*res*/ exit_unlink( void* ctx , Record& r , pid_t , int64_t res ) {
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
template<bool At,int FlagArg> bool/*skip_syscall*/ entry_mkdir( void* & /*ctx*/ , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	bool no_follow ;
	switch (FlagArg) {
		case FlagAlways : no_follow = true                                   ; break ;
		case FlagNever  : no_follow = false                                  ; break ;
		default         : no_follow = args[FlagArg+At] & AT_SYMLINK_NOFOLLOW ;
	}
	try {
		Record::Solve s{ r , _path<At>(pid,args+0) , no_follow , false/*read*/ , comment } ;
		_update<At>(args+0,s) ;
	} catch (int) {}
	return false ;
}

// XXX : find a way to put one entry per line instead of 3 lines(would be much more readable)
SyscallDescr::Tab const& SyscallDescr::s_tab() {           // this must *not* do any mem allocation (or syscall impl in ld.cc breaks), so it cannot be a umap
	static Tab s_tab = {} ;
	//	/!\ prio must be non-zero as zero means entry is not allocated
	//	                                                                                 entry           At    Path  flag         exit            prio data access comment
	#ifdef SYS_faccessat
		static_assert(SYS_faccessat        <NSyscalls) ; s_tab[SYS_faccessat        ] = { entry_stat    <true       ,2         > , nullptr       , 2  , false     , "Faccessat"         } ;
	#endif
	#ifdef SYS_access
		static_assert(SYS_access           <NSyscalls) ; s_tab[SYS_access           ] = { entry_stat    <false      ,FlagNever > , nullptr       , 1  , false     , "Access"            } ;
	#endif
	#ifdef SYS_faccessat2
		static_assert(SYS_faccessat2       <NSyscalls) ; s_tab[SYS_faccessat2       ] = { entry_stat    <true       ,2         > , nullptr       , 2  , false     , "Faccessat2"        } ;
	#endif
	#ifdef SYS_chdir
		static_assert(SYS_chdir            <NSyscalls) ; s_tab[SYS_chdir            ] = { entry_chdir   <false,true            > , exit_chdir    , 1  , true                            } ;
	#endif
	#ifdef SYS_fchdir
		static_assert(SYS_fchdir           <NSyscalls) ; s_tab[SYS_fchdir           ] = { entry_chdir   <true ,false           > , exit_chdir    , 1  , true                            } ;
	#endif
	#ifdef SYS_chmod
		static_assert(SYS_chmod            <NSyscalls) ; s_tab[SYS_chmod            ] = { entry_chmod   <false,true ,false     > , exit_chmod    , 1  , true      , "Chmod"             } ;
	#endif
	#ifdef SYS_fchmod
		static_assert(SYS_fchmod           <NSyscalls) ; s_tab[SYS_fchmod           ] = { entry_chmod   <true ,false,false     > , exit_chmod    , 1  , true      , "Fchmod"            } ;
	#endif
	#ifdef SYS_fchmodat
		static_assert(SYS_fchmodat         <NSyscalls) ; s_tab[SYS_fchmodat         ] = { entry_chmod   <true ,true ,true      > , exit_chmod    , 1  , true      , "Fchmodat"          } ;
	#endif
	#ifdef SYS_execve
		static_assert(SYS_execve           <NSyscalls) ; s_tab[SYS_execve           ] = { entry_execve  <false      ,false     > , nullptr       , 1  , true      , "Execve"            } ;
	#endif
	#ifdef SYS_execveat
		static_assert(SYS_execveat         <NSyscalls) ; s_tab[SYS_execveat         ] = { entry_execve  <true       ,true      > , nullptr       , 1  , true      , "Execveat"          } ;
	#endif
	#if defined(SYS_getcwd) && !defined(PTRACE) // tmp mapping is not supported with ptrace
		static_assert(SYS_getcwd           <NSyscalls) ; s_tab[SYS_getcwd           ] = { entry_getcwd                           , exit_getcwd   , 1  , true                            } ;
	#endif
	#ifdef SYS_link
		static_assert(SYS_link             <NSyscalls) ; s_tab[SYS_link             ] = { entry_lnk     <false      ,false     > , exit_lnk      , 1  , true      , "Link"              } ;
	#endif
	#ifdef SYS_linkat
		static_assert(SYS_linkat           <NSyscalls) ; s_tab[SYS_linkat           ] = { entry_lnk     <true       ,true      > , exit_lnk      , 1  , true      , "Linkat"            } ;
	#endif
	#ifdef SYS_mkdir
		static_assert(SYS_mkdir            <NSyscalls) ; s_tab[SYS_mkdir            ] = { entry_mkdir   <false      ,FlagNever > , nullptr       , 1  , false     , "Mkdir"             } ;
	#endif
	#ifdef SYS_mkdirat
		static_assert(SYS_mkdirat          <NSyscalls) ; s_tab[SYS_mkdirat          ] = { entry_mkdir   <true       ,FlagNever > , nullptr       , 1  , false     , "Mkdirat"           } ;
	#endif
	#ifdef SYS_name_to_handle_at
		static_assert(SYS_name_to_handle_at<NSyscalls) ; s_tab[SYS_name_to_handle_at] = { entry_open    <true                  > , exit_open     , 1  , true      , "Name_to_handle_at" } ;
	#endif
	#ifdef SYS_open
		static_assert(SYS_open             <NSyscalls) ; s_tab[SYS_open             ] = { entry_open    <false                 > , exit_open     , 2  , true      , "Open"              } ;
	#endif
	#ifdef SYS_openat
		static_assert(SYS_openat           <NSyscalls) ; s_tab[SYS_openat           ] = { entry_open    <true                  > , exit_open     , 2  , true      , "Openat"            } ;
	#endif
	#ifdef SYS_openat2
		static_assert(SYS_openat2          <NSyscalls) ; s_tab[SYS_openat2          ] = { entry_open    <true                  > , exit_open     , 2  , true      , "Openat2"           } ;
	#endif
	#ifdef SYS_open_tree
		static_assert(SYS_open_tree        <NSyscalls) ; s_tab[SYS_open_tree        ] = { entry_stat    <true       ,1         > , nullptr       , 1  , false     , "Open_tree"         } ;
	#endif
	#ifdef SYS_readlink
		static_assert(SYS_readlink         <NSyscalls) ; s_tab[SYS_readlink         ] = { entry_read_lnk<false                 > , exit_read_lnk , 2  , true      , "Readlink"          } ;
	#endif
	#ifdef SYS_readlinkat
		static_assert(SYS_readlinkat       <NSyscalls) ; s_tab[SYS_readlinkat       ] = { entry_read_lnk<true                  > , exit_read_lnk , 2  , true      , "Readlinkat"        } ;
	#endif
	#if SYS_rename
		static_assert(SYS_rename           <NSyscalls) ; s_tab[SYS_rename           ] = { entry_rename  <false      ,false     > , exit_rename   , 1  , true      , "Rename"            } ;
	#endif
	#ifdef SYS_renameat
		static_assert(SYS_renameat         <NSyscalls) ; s_tab[SYS_renameat         ] = { entry_rename  <true       ,false     > , exit_rename   , 1  , true      , "Renameat"          } ;
	#endif
	#ifdef SYS_renameat2
		static_assert(SYS_renameat2        <NSyscalls) ; s_tab[SYS_renameat2        ] = { entry_rename  <true       ,true      > , exit_rename   , 1  , true      , "Renameat2"         } ;
	#endif
	#ifdef SYS_rmdir
		static_assert(SYS_rmdir            <NSyscalls) ; s_tab[SYS_rmdir            ] = { entry_stat    <false      ,FlagAlways> , nullptr       , 1  , false     , "Rmdir"             } ;
	#endif
	#ifdef SYS_stat
		static_assert(SYS_stat             <NSyscalls) ; s_tab[SYS_stat             ] = { entry_stat    <false      ,FlagNever > , nullptr       , 2  , false     , "Stat"              } ;
	#endif
	#ifdef SYS_stat64
		static_assert(SYS_stat64           <NSyscalls) ; s_tab[SYS_stat64           ] = { entry_stat    <false      ,FlagNever > , nullptr       , 1  , false     , "Stat64"            } ;
	#endif
	#ifdef SYS_fstatat64
		static_assert(SYS_fstatat64        <NSyscalls) ; s_tab[SYS_fstatat64        ] = { entry_stat    <true       ,2         > , nullptr       , 1  , false     , "Fstatat64"         } ;
	#endif
	#ifdef SYS_lstat
		static_assert(SYS_lstat            <NSyscalls) ; s_tab[SYS_lstat            ] = { entry_stat    <false      ,FlagAlways> , nullptr       , 2  , false     , "Lstat"             } ;
	#endif
	#ifdef SYS_lstat64
		static_assert(SYS_lstat64          <NSyscalls) ; s_tab[SYS_lstat64          ] = { entry_stat    <false      ,FlagAlways> , nullptr       , 1  , false     , "Lstat64"           } ;
	#endif
	#ifdef SYS_statx
		static_assert(SYS_statx            <NSyscalls) ; s_tab[SYS_statx            ] = { entry_stat    <true       ,1         > , nullptr       , 1  , false     , "Statx"             } ;
	#endif
	#if SYS_newfstatat
		static_assert(SYS_newfstatat       <NSyscalls) ; s_tab[SYS_newfstatat       ] = { entry_stat    <true       ,2         > , nullptr       , 2  , false     , "Newfstatat"        } ;
	#endif
	#ifdef SYS_oldstat
		static_assert(SYS_oldstat          <NSyscalls) ; s_tab[SYS_oldstat          ] = { entry_stat    <false      ,FlagNever > , nullptr       , 1  , false     , "Oldstat"           } ;
	#endif
	#ifdef SYS_oldlstat
		static_assert(SYS_oldlstat         <NSyscalls) ; s_tab[SYS_oldlstat         ] = { entry_stat    <false      ,FlagAlways> , nullptr       , 1  , false     , "Oldlstat"          } ;
	#endif
	#ifdef SYS_symlink
		static_assert(SYS_symlink          <NSyscalls) ; s_tab[SYS_symlink          ] = { entry_sym_lnk <false                 > , exit_sym_lnk  , 1  , true      , "Symlink"           } ;
	#endif
	#ifdef SYS_symlinkat
		static_assert(SYS_symlinkat        <NSyscalls) ; s_tab[SYS_symlinkat        ] = { entry_sym_lnk <true                  > , exit_sym_lnk  , 1  , true      , "Symlinkat"         } ;
	#endif
	#ifdef SYS_unlink
		static_assert(SYS_unlink           <NSyscalls) ; s_tab[SYS_unlink           ] = { entry_unlink  <false      ,false     > , exit_unlink   , 1  , true      , "Unlink"            } ;
	#endif
	#ifdef SYS_unlinkat
		static_assert(SYS_unlinkat         <NSyscalls) ; s_tab[SYS_unlinkat         ] = { entry_unlink  <true       ,true      > , exit_unlink   , 1  , true      , "Unlinkat"          } ;
	#endif
	return s_tab ;
}
