// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <syscall.h> // for SYS_* macros

#include "ptrace.hh"
#include "record.hh"

#include "syscall_tab.hh"

::pair<char*,ssize_t> fix_cwd( char* buf , size_t buf_sz , ssize_t sz , Bool3 allocated ) noexcept { // allocated=Maybe means allocated of fixed size, getcwd unused if no error is generated
	if ( !buf || sz<0              ) return {buf,sz} ;                                               // error
	if ( !Record::s_has_tmp_view() ) return {buf,sz} ;                                               // no tmp mapping
	//
	::string const& tmp_dir  = Record::s_autodep_env().tmp_dir  ;
	::string const& tmp_view = Record::s_autodep_env().tmp_view ;
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
		char* _ [[maybe_unused]] = getcwd(&x,1) ;                               // force an error in user land as we have not enough space (a cwd cannot fit within 1 byte with its terminating null)
		if (allocated!=No) free(buf) ;
		return {nullptr,ERANGE} ;
	}
	if (tmp_view.size()>tmp_dir.size()) ::memmove( buf+tmp_view.size() , buf+tmp_dir.size() , sz-tmp_view.size()+1 ) ; // +1 for the terminating null
	/**/                                ::memcpy ( buf                 , tmp_view.c_str()   ,    tmp_view.size()   ) ;
	if (tmp_view.size()<tmp_dir.size()) ::memmove( buf+tmp_view.size() , buf+tmp_dir.size() , sz-tmp_view.size()+1 ) ; // .
	return {buf,sz} ;
}

// return null terminated string pointed by src in process pid's space
static ::string _get_str( pid_t pid , uint64_t src ) {
	if (!pid) return {reinterpret_cast<const char*>(src)} ;
	::string res ;
	errno = 0 ;
	for(;;) {
		uint64_t offset = src%sizeof(long)                                               ;
		long     word   = ptrace( PTRACE_PEEKDATA , pid , src-offset , nullptr/*data*/ ) ;
		if (errno) throw 0 ;
		char buf[sizeof(long)] ; ::memcpy( buf , &word , sizeof(long) ) ;
		for( uint64_t len=0 ; len<sizeof(long)-offset ; len++ ) if (!buf[offset+len]) { res.append( buf+offset , len                 ) ; return res ; }
		/**/                                                                            res.append( buf+offset , sizeof(long)-offset ) ;
		src += sizeof(long)-offset ;
	}
}

// copy src to process pid's space @ dst
static void _poke( pid_t pid , uint64_t dst , const char* src , size_t sz ) {
	SWEAR(pid) ;
	errno = 0 ;
	for( size_t chunk ; sz ; src+=chunk , dst+=chunk , sz-=chunk) {                 // invariant : copy src[i:sz] to dst
		uint64_t offset = dst%sizeof(long) ;
		long     word   = 0/*garbage*/     ;
		chunk = ::min( sizeof(long) - offset , sz ) ;
		if ( offset || offset+chunk<sizeof(long) ) {                                // partial word
			word = ptrace( PTRACE_PEEKDATA , pid , dst-offset , nullptr/*data*/ ) ;
			if (errno) throw 0 ;
		}
		::memcpy( reinterpret_cast<char*>(&word)+offset , src , chunk ) ;
		ptrace( PTRACE_POKEDATA , pid , dst-offset , word ) ;
		if (errno) throw 0 ;
	}
}

// copy src to process pid's space @ dst
static void _peek( pid_t pid , char* dst , uint64_t src , size_t sz ) {
	SWEAR(pid) ;
	errno = 0 ;
	for( size_t chunk ; sz ; src+=chunk , dst+=chunk , sz-=chunk) { // invariant : copy src[i:sz] to dst
		uint64_t offset = src%sizeof(long) ;
		long     word   = ptrace( PTRACE_PEEKDATA , pid , src-offset , nullptr/*data*/ ) ;
		if (errno) throw 0 ;
		chunk = ::min( sizeof(long) - offset , sz ) ;
		::memcpy( dst , reinterpret_cast<char*>(&word)+offset , chunk ) ;
	}
}

template<bool At> static Record::Path _path( pid_t pid , uint64_t const* args ) {
	if (At) return { Fd(args[0]) , _get_str(pid,args[1]) } ;
	else    return {               _get_str(pid,args[0]) } ;
}

// updating args is meaningful only when processing calls to the syscall function with ld_audit & ld_preload
// when autodep is ptrace, tmp mapping is not supported and such args updating is ignored as args have been copied from tracee and are not copied back to it
template<bool At> void _update( uint64_t* args , Record::Path const& p ) {
	SWEAR(p.has_at==At) ;
	if (At) args[0 ] =                            p.at    ;
	/**/    args[At] = reinterpret_cast<uint64_t>(p.file) ;
}

static constexpr int FlagAlways = -1 ;
static constexpr int FlagNever  = -2 ;
template<int FlagArg> bool _flag( uint64_t args[6] , int flag ) {
	switch (FlagArg) {
		case FlagAlways : return true                 ;
		case FlagNever  : return false                ;
		default         : return args[FlagArg] & flag ;
	}
}

// chdir
#if defined(SYS_chdir) || defined(SYS_fchdir)
template<bool At> void _entry_chdir( void* & ctx , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		if (At) { Record::Chdir* cd = new Record::Chdir( r , {Fd(args[0])          } , comment ) ; ctx = cd ;                           }
		else    { Record::Chdir* cd = new Record::Chdir( r , {_path<At>(pid,args+0)} , comment ) ; ctx = cd ; _update<At>(args+0,*cd) ; }
	} catch (int) {}
}
static int64_t/*res*/ _exit_chdir( void* ctx , Record& r , pid_t pid , int64_t res ) {
	if (!ctx) return res ;
	Record::Chdir* cd = static_cast<Record::Chdir*>(ctx) ;
	(*cd)(r,res,pid) ;
	delete cd ;
	return res ;
}
#endif

// chmod
#if defined(SYS_chmod) || defined (SYS_fchmodat)
template<bool At,int FlagArg> void _entry_chmod( void* & ctx , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		Record::Chmod* cm = new Record::Chmod( r , _path<At>(pid,args+0) , args[1+At]&S_IXUSR , _flag<FlagArg>(args,AT_SYMLINK_NOFOLLOW) , comment ) ;
		ctx = cm ;
		_update<At>(args+0,*cm) ;
	} catch (int) {}
}
static int64_t/*res*/ _exit_chmod( void* ctx , Record& r , pid_t , int64_t res ) {
	if (!ctx) return res ;
	Record::Chmod* cm = static_cast<Record::Chmod*>(ctx) ;
	(*cm)(r,res) ;
	delete cm ;
	return res ;
}
#endif

// creat
#ifdef SYS_creat
static void _entry_creat( void* & ctx , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		Record::Open* o = new Record::Open( r , _path<false>(pid,args+0) , O_WRONLY|O_CREAT|O_TRUNC , comment ) ;
		ctx = o ;
		_update<false>(args+0,*o) ;
	}
	catch (int) {}
}
#endif
// use _exit_open as exit proc

// execve
#if defined(SYS_execve) || defined(SYS_execveat)
// must be called before actual syscall execution as after execution, info is no more available
template<bool At,int FlagArg> static void _entry_execve( void* & /*ctx*/ , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		Record::Exec e{ r , _path<At>(pid,args+0) , _flag<FlagArg>(args,AT_SYMLINK_NOFOLLOW) , comment } ;
		_update<At>(args+0,e) ;
	} catch (int) {}
}
#endif

// getcwd
#ifdef SYS_getcwd
// getcwd is only necessary if tmp is mapped (not in table with ptrace)
static void _entry_getcwd( void* & ctx , Record& , pid_t , uint64_t args[6] , const char* /*comment*/ ) {
	size_t* sz = new size_t{args[1]} ;
	ctx = sz ;
}
static int64_t/*res*/ _exit_getcwd( void* ctx , Record& , pid_t pid , int64_t res ) {
	if (!res                     ) return res ;                                       // in case of error, man getcwd says buffer is undefined => nothing to do
	if (!Record::s_has_tmp_view()) return res ;                                       // no tmp mapping                                        => nothing to do
	SWEAR(pid==0,pid) ;                                                               // tmp mapping is not supported with ptrace (need to report fixed result to caller)
	char*   buf = reinterpret_cast<char*  >(res) ;
	size_t* sz  = reinterpret_cast<size_t*>(ctx) ;
	res = fix_cwd(buf,*sz,res).second ;
	delete sz ;
	return res ;
}
#endif

// hard link
#if defined(SYS_link) || defined(SYS_linkat)
template<bool At,int FlagArg> void _entry_lnk( void* & ctx , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		Record::Lnk* l = new Record::Lnk( r , _path<At>(pid,args+0) , _path<At>(pid,args+1+At) , _flag<FlagArg>(args,AT_SYMLINK_NOFOLLOW) , comment ) ;
		ctx = l ;
		_update<At>(args+0,l->src) ;
		_update<At>(args+2,l->dst) ;
	} catch (int) {}
}
static int64_t/*res*/ _exit_lnk( void* ctx , Record& r , pid_t /*pid */, int64_t res ) {
	if (!ctx) return res ;
	Record::Lnk* l = static_cast<Record::Lnk*>(ctx) ;
	(*l)(r,res) ;
	delete l ;
	return res ;
}
#endif

// mkdir
#if defined(SYS_mkdir) || defined(SYS_mkdirat)
template<bool At> void _entry_mkdir( void* & /*ctx*/ , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		Record::Mkdir m{ r , _path<At>(pid,args+0) , comment } ;
		_update<At>(args+0,m) ;
	} catch (int) {}
}
#endif

// open
#if defined(SYS_name_to_handle_at) || defined(SYS_open) || defined(SYS_openat) || defined(SYS_openat2)
template<bool At> void _entry_open( void* & ctx , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		Record::Open* o = new Record::Open( r , _path<At>(pid,args+0) , args[1+At]/*flags*/ , comment ) ;
		ctx = o ;
		_update<At>(args+0,*o) ;
	}
	catch (int) {}
}
static int64_t/*res*/ _exit_open( void* ctx , Record& r , pid_t /*pid*/ , int64_t res ) {
	if (!ctx) return res ;
	Record::Open* o = static_cast<Record::Open*>(ctx) ;
	(*o)( r , res ) ;
	delete o ;
	return res ;
}
#endif

// read_lnk
#if defined(SYS_readlink) || defined(SYS_readlinkat)
using RLB = ::pair<Record::Readlink,uint64_t/*buf*/> ;
template<bool At> void _entry_read_lnk( void* & ctx , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		uint64_t orig_buf = args[At+1]                                                                               ;
		size_t   sz       = args[At+2]                                                                               ;
		char*    buf      = pid ? new char[sz] : reinterpret_cast<char*>(orig_buf)                                   ;
		RLB*     rlb      = new RLB( Record::Readlink( r , _path<At>(pid,args+0) , buf , sz , comment ) , orig_buf ) ;
		ctx = rlb ;
		_update<At>(args+0,rlb->first) ;
	} catch (int) {}
}
static int64_t/*res*/ _exit_read_lnk( void* ctx , Record& r , pid_t pid , int64_t res ) {
	if (!ctx) return res ;
	RLB* rlb = static_cast<RLB*>(ctx) ;
	SWEAR( res<=ssize_t(rlb->first.sz) , res , rlb->first.sz ) ;
	if ( pid && res>=0 ) _peek( pid , rlb->first.buf , rlb->second , res ) ;
	res = (rlb->first)(r,res) ;
	if (pid) {
		if (rlb->first.emulated) _poke( pid , rlb->second , rlb->first.buf , res ) ; // access to backdoor was emulated, we must transport result to actual use space
		delete[] rlb->first.buf ;
	}
	delete rlb ;
	return res ;
}
#endif

// rename
#if defined(SYS_rename) || defined(SYS_renameat) || defined(SYS_renameat2)
template<bool At,int FlagArg> void _entry_rename( void* & ctx , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		#ifdef RENAME_EXCHANGE
			bool exchange = _flag<FlagArg>(args,RENAME_EXCHANGE) ;
		#else
			bool exchange = false                                ;
		#endif
		#ifdef RENAME_NOREPLACE
			bool no_replace = _flag<FlagArg>(args,RENAME_NOREPLACE) ;
		#else
			bool no_replace = false                                 ;
		#endif
		Record::Rename* rn = new Record::Rename( r , _path<At>(pid,args+0) , _path<At>(pid,args+1+At) , exchange , no_replace , comment ) ;
		ctx = rn ;
		_update<At>(args+0,rn->src) ;
		_update<At>(args+2,rn->dst) ;
	} catch (int) {}
}
static int64_t/*res*/ _exit_rename( void* ctx , Record& r , pid_t /*pid*/ , int64_t res ) {
	if (!ctx) return res ;
	Record::Rename* rn = static_cast<Record::Rename*>(ctx) ;
	(*rn)(r,res) ;
	delete rn ;
	return res ;
}
#endif

// symlink
#if defined(SYS_symlink) || defined(SYS_symlinkat)
template<bool At> void _entry_sym_lnk( void* & ctx , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		Record::Symlnk* sl = new Record::Symlnk( r , _path<At>(pid,args+1) , comment ) ;
		ctx = sl ;
		_update<At>(args+1,*sl) ;
	} catch (int) {}
}
static int64_t/*res*/ _exit_sym_lnk( void* ctx , Record& r , pid_t , int64_t res ) {
	if (!ctx) return res ;
	Record::Symlnk* sl = static_cast<Record::Symlnk*>(ctx) ;
	(*sl)(r,res) ;
	delete sl ;
	return res ;
}
#endif

// unlink
#if defined(SYS_rmdir) || defined(SYS_unlink) || defined(SYS_unlinkat)
template<bool At,int FlagArg> void _entry_unlnk( void* & ctx , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		bool           rmdir = _flag<FlagArg>(args,AT_REMOVEDIR)                                ;
		Record::Unlnk* u     = new Record::Unlnk( r , _path<At>(pid,args+0) , rmdir , comment ) ;
		if (!rmdir) ctx = u ;                                                                     // rmdir calls us without exit, and we must not set ctx in that case
		_update<At>(args+0,*u) ;
	} catch (int) {}
}
static int64_t/*res*/ _exit_unlnk( void* ctx , Record& r , pid_t , int64_t res ) {
	if (!ctx) return res ;
	Record::Unlnk* u = static_cast<Record::Unlnk*>(ctx) ;
	(*u)(r,res) ;
	delete u ;
	return res ;
}
#endif

// access
template<bool At,int FlagArg> void _entry_stat( void* & /*ctx*/ , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		Record::Stat s{ r , _path<At>(pid,args+0) , _flag<FlagArg>(args,AT_SYMLINK_NOFOLLOW) , comment } ;
		_update<At>(args+0,s) ;
		s(r) ;
	} catch (int) {}
}

// XXX : find a way to put one entry per line instead of 3 lines(would be much more readable)
SyscallDescr::Tab const& SyscallDescr::s_tab(bool for_ptrace) {        // this must *not* do any mem allocation (or syscall impl in ld.cc breaks), so it cannot be a umap
	static Tab                         s_tab    = {}    ;
	static ::atomic<bool>              s_inited = false ;              // set to true once s_tab is initialized
	if (s_inited) return s_tab ;                                       // fast test not even taking the lock
	static Mutex<MutexLvl::SyscallTab> s_mutex  ;
	Lock lock{s_mutex} ;
	if (s_inited) return s_tab ;                                       // repeat test now that we have the lock in case another thread while in the middle of initializing s_tab
	//	/!\ prio must be non-zero as zero means entry is not allocated
	//	entries marked NFS_GUARD are deemed data access as they touch their enclosing dir and hence must be guarded against strange NFS notion of coherence
	//	entries marked filter (i.e. field is !=0) means that processing can be skipped if corresponding arg is a file name known to require no processing
	#define NFS_GUARD true
	//	                                                                                { entry          <At   ,flag      > , exit           filter,prio,data access comment             }
	#ifdef SYS_access
		static_assert(SYS_access           <NSyscalls) ; s_tab[SYS_access           ] = { _entry_stat    <false,FlagNever > , nullptr        ,1    , 1  , false    , "Access"            } ;
	#endif
	#ifdef SYS_faccessat
		static_assert(SYS_faccessat        <NSyscalls) ; s_tab[SYS_faccessat        ] = { _entry_stat    <true ,3         > , nullptr        ,2    , 2  , false    , "Faccessat"         } ;
	#endif
	#ifdef SYS_faccessat2
		static_assert(SYS_faccessat2       <NSyscalls) ; s_tab[SYS_faccessat2       ] = { _entry_stat    <true ,3         > , nullptr        ,2    , 2  , false    , "Faccessat2"        } ;
	#endif
	#ifdef SYS_chdir
		static_assert(SYS_chdir            <NSyscalls) ; s_tab[SYS_chdir            ] = { _entry_chdir   <false           > , _exit_chdir    ,0    , 1  , true     , "Chdir"             } ;
	#endif
	#ifdef SYS_fchdir
		static_assert(SYS_fchdir           <NSyscalls) ; s_tab[SYS_fchdir           ] = { _entry_chdir   <true            > , _exit_chdir    ,0    , 1  , true     , "Fchdir"            } ;
	#endif
	#ifdef SYS_chmod
		static_assert(SYS_chmod            <NSyscalls) ; s_tab[SYS_chmod            ] = { _entry_chmod   <false,FlagNever > , _exit_chmod    ,1    , 1  , true     , "Chmod"             } ;
	#endif
	#ifdef SYS_fchmodat
		static_assert(SYS_fchmodat         <NSyscalls) ; s_tab[SYS_fchmodat         ] = { _entry_chmod   <true ,3         > , _exit_chmod    ,2    , 1  , true     , "Fchmodat"          } ;
	#endif
	#ifdef SYS_creat
		static_assert(SYS_creat            <NSyscalls) ; s_tab[SYS_creat            ] = { _entry_creat                      , _exit_open     ,1    , 2  , true     , "Creat"             } ;
	#endif
	#ifdef SYS_execve
		static_assert(SYS_execve           <NSyscalls) ; s_tab[SYS_execve           ] = { _entry_execve  <false,FlagNever > , nullptr        ,0    , 1  , true     , "Execve"            } ;
	#endif
	#ifdef SYS_execveat
		static_assert(SYS_execveat         <NSyscalls) ; s_tab[SYS_execveat         ] = { _entry_execve  <true ,4         > , nullptr        ,0    , 1  , true     , "Execveat"          } ;
	#endif
	#ifdef SYS_getcwd
		// tmp mapping is not supported with ptrace
		if (!for_ptrace) {
		static_assert(SYS_getcwd           <NSyscalls) ; s_tab[SYS_getcwd           ] = { _entry_getcwd                     , _exit_getcwd   ,0    , 1  , true     , "Getcwd"            } ;
        }
	#endif
	#ifdef SYS_link
		static_assert(SYS_link             <NSyscalls) ; s_tab[SYS_link             ] = { _entry_lnk     <false,FlagNever > , _exit_lnk      ,2    , 1  , true     , "Link"              } ;
	#endif
	#ifdef SYS_linkat
		static_assert(SYS_linkat           <NSyscalls) ; s_tab[SYS_linkat           ] = { _entry_lnk     <true ,4         > , _exit_lnk      ,4    , 1  , true     , "Linkat"            } ;
	#endif
	#ifdef SYS_mkdir
		static_assert(SYS_mkdir            <NSyscalls) ; s_tab[SYS_mkdir            ] = { _entry_mkdir   <false           > , nullptr        ,1    , 1  , NFS_GUARD, "Mkdir"             } ;
	#endif
	#ifdef SYS_mkdirat
		static_assert(SYS_mkdirat          <NSyscalls) ; s_tab[SYS_mkdirat          ] = { _entry_mkdir   <true            > , nullptr        ,2    , 1  , NFS_GUARD, "Mkdirat"           } ;
	#endif
	#ifdef SYS_name_to_handle_at
		static_assert(SYS_name_to_handle_at<NSyscalls) ; s_tab[SYS_name_to_handle_at] = { _entry_open    <true            > , _exit_open     ,2    , 1  , true     , "Name_to_handle_at" } ;
	#endif
	#ifdef SYS_open
		static_assert(SYS_open             <NSyscalls) ; s_tab[SYS_open             ] = { _entry_open    <false           > , _exit_open     ,1    , 2  , true     , "Open"              } ;
	#endif
	#ifdef SYS_openat
		static_assert(SYS_openat           <NSyscalls) ; s_tab[SYS_openat           ] = { _entry_open    <true            > , _exit_open     ,2    , 2  , true     , "Openat"            } ;
	#endif
	#ifdef SYS_openat2
		static_assert(SYS_openat2          <NSyscalls) ; s_tab[SYS_openat2          ] = { _entry_open    <true            > , _exit_open     ,2    , 2  , true     , "Openat2"           } ;
	#endif
	#ifdef SYS_open_tree
		static_assert(SYS_open_tree        <NSyscalls) ; s_tab[SYS_open_tree        ] = { _entry_stat    <true ,2         > , nullptr        ,2    , 1  , false    , "Open_tree"         } ;
	#endif
	#ifdef SYS_readlink
		static_assert(SYS_readlink         <NSyscalls) ; s_tab[SYS_readlink         ] = { _entry_read_lnk<false           > , _exit_read_lnk ,1    , 2  , true     , "Readlink"          } ;
	#endif
	#ifdef SYS_readlinkat
		static_assert(SYS_readlinkat       <NSyscalls) ; s_tab[SYS_readlinkat       ] = { _entry_read_lnk<true            > , _exit_read_lnk ,2    , 2  , true     , "Readlinkat"        } ;
	#endif
	#if SYS_rename
		static_assert(SYS_rename           <NSyscalls) ; s_tab[SYS_rename           ] = { _entry_rename  <false,FlagNever > , _exit_rename   ,2    , 1  , true     , "Rename"            } ;
	#endif
	#ifdef SYS_renameat
		static_assert(SYS_renameat         <NSyscalls) ; s_tab[SYS_renameat         ] = { _entry_rename  <true ,FlagNever > , _exit_rename   ,4    , 1  , true     , "Renameat"          } ;
	#endif
	#ifdef SYS_renameat2
		static_assert(SYS_renameat2        <NSyscalls) ; s_tab[SYS_renameat2        ] = { _entry_rename  <true ,4         > , _exit_rename   ,4    , 1  , true     , "Renameat2"         } ;
	#endif
	#ifdef SYS_rmdir
		static_assert(SYS_rmdir            <NSyscalls) ; s_tab[SYS_rmdir            ] = { _entry_unlnk   <false,FlagAlways> , nullptr        ,1    , 1  , NFS_GUARD, "Rmdir"             } ;
	#endif
	#ifdef SYS_stat
		static_assert(SYS_stat             <NSyscalls) ; s_tab[SYS_stat             ] = { _entry_stat    <false,FlagNever > , nullptr        ,1    , 2  , false    , "Stat"              } ;
	#endif
	#ifdef SYS_stat64
		static_assert(SYS_stat64           <NSyscalls) ; s_tab[SYS_stat64           ] = { _entry_stat    <false,FlagNever > , nullptr        ,1    , 1  , false    , "Stat64"            } ;
	#endif
	#ifdef SYS_fstatat64
		static_assert(SYS_fstatat64        <NSyscalls) ; s_tab[SYS_fstatat64        ] = { _entry_stat    <true ,3         > , nullptr        ,2    , 1  , false    , "Fstatat64"         } ;
	#endif
	#ifdef SYS_lstat
		static_assert(SYS_lstat            <NSyscalls) ; s_tab[SYS_lstat            ] = { _entry_stat    <false,FlagAlways> , nullptr        ,1    , 2  , false    , "Lstat"             } ;
	#endif
	#ifdef SYS_lstat64
		static_assert(SYS_lstat64          <NSyscalls) ; s_tab[SYS_lstat64          ] = { _entry_stat    <false,FlagAlways> , nullptr        ,1    , 1  , false    , "Lstat64"           } ;
	#endif
	#ifdef SYS_statx
		static_assert(SYS_statx            <NSyscalls) ; s_tab[SYS_statx            ] = { _entry_stat    <true ,2         > , nullptr        ,2    , 1  , false    , "Statx"             } ;
	#endif
	#if SYS_newfstatat
		static_assert(SYS_newfstatat       <NSyscalls) ; s_tab[SYS_newfstatat       ] = { _entry_stat    <true ,3         > , nullptr        ,2    , 2  , false    , "Newfstatat"        } ;
	#endif
	#ifdef SYS_oldstat
		static_assert(SYS_oldstat          <NSyscalls) ; s_tab[SYS_oldstat          ] = { _entry_stat    <false,FlagNever > , nullptr        ,1    , 1  , false    , "Oldstat"           } ;
	#endif
	#ifdef SYS_oldlstat
		static_assert(SYS_oldlstat         <NSyscalls) ; s_tab[SYS_oldlstat         ] = { _entry_stat    <false,FlagAlways> , nullptr        ,1    , 1  , false    , "Oldlstat"          } ;
	#endif
	#ifdef SYS_symlink
		static_assert(SYS_symlink          <NSyscalls) ; s_tab[SYS_symlink          ] = { _entry_sym_lnk <false           > , _exit_sym_lnk  ,2    , 1  , true     , "Symlink"           } ;
	#endif
	#ifdef SYS_symlinkat
		static_assert(SYS_symlinkat        <NSyscalls) ; s_tab[SYS_symlinkat        ] = { _entry_sym_lnk <true            > , _exit_sym_lnk  ,3    , 1  , true     , "Symlinkat"         } ;
	#endif
	#ifdef SYS_unlink
		static_assert(SYS_unlink           <NSyscalls) ; s_tab[SYS_unlink           ] = { _entry_unlnk   <false,FlagNever > , _exit_unlnk    ,1    , 1  , true     , "Unlink"            } ;
	#endif
	#ifdef SYS_unlinkat
		static_assert(SYS_unlinkat         <NSyscalls) ; s_tab[SYS_unlinkat         ] = { _entry_unlnk   <true ,2         > , _exit_unlnk    ,2    , 1  , true     , "Unlinkat"          } ;
	#endif
	#undef NFS_GUARD
	fence() ;                                                          // ensure serializatino
	s_inited = true ;
	return s_tab ;
}
