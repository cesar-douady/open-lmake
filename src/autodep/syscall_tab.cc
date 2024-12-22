// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <syscall.h>   // for SYS_* macros
#include <sys/mount.h>

#include "ptrace.hh"
#include "record.hh"

#include "syscall_tab.hh"

// return null terminated string pointed by src in process pid's space
[[maybe_unused]] static ::string _get_str( pid_t pid , uint64_t src ) {
	if (!pid) return {reinterpret_cast<const char*>(src)} ;
	::string res ;
	errno = 0 ;
	for(;;) {
		uint64_t offset = src%sizeof(long)                                               ;
		long     word   = ::ptrace( PTRACE_PEEKDATA , pid , src-offset , nullptr/*data*/ ) ;
		if (errno) throw errno ;
		char buf[sizeof(long)] ; ::memcpy( buf , &word , sizeof(long) ) ;
		for( uint64_t len : iota(sizeof(long)-offset) ) if (!buf[offset+len]) { res.append( buf+offset , len                 ) ; return res ; }
		/**/                                                                    res.append( buf+offset , sizeof(long)-offset ) ;
		src += sizeof(long)-offset ;
	}
}

// copy process pid's space @ src to dst
[[maybe_unused]] static void _peek( pid_t pid , char* dst , uint64_t src , size_t sz ) {
	SWEAR(pid) ;
	errno = 0 ;
	for( size_t chunk ; sz ; src+=chunk , dst+=chunk , sz-=chunk) {                   // invariant : copy src[i:sz] to dst
		size_t offset = src%sizeof(long) ;
		long   word   = ::ptrace( PTRACE_PEEKDATA , pid , src-offset , nullptr/*data*/ ) ;
		if (errno) throw errno ;
		chunk = ::min( sizeof(long) - offset , sz ) ;
		::memcpy( dst , reinterpret_cast<char*>(&word)+offset , chunk ) ;
	}
}
// copy src to process pid's space @ dst
[[maybe_unused]] static void _poke( pid_t pid , uint64_t dst , const char* src , size_t sz ) {
	SWEAR(pid) ;
	errno = 0 ;
	for( size_t chunk ; sz ; src+=chunk , dst+=chunk , sz-=chunk) {                   // invariant : copy src[i:sz] to dst
		size_t offset = dst%sizeof(long) ;
		long   word   = 0/*garbage*/     ;
		chunk = ::min( sizeof(long) - offset , sz ) ;
		if ( offset || offset+chunk<sizeof(long) ) {                                  // partial word
			word = ::ptrace( PTRACE_PEEKDATA , pid , dst-offset , nullptr/*data*/ ) ;
			if (errno) throw errno ;
		}
		::memcpy( reinterpret_cast<char*>(&word)+offset , src , chunk ) ;
		::ptrace( PTRACE_POKEDATA , pid , dst-offset , word ) ;
		if (errno) throw errno ;
	}
	}

template<bool At> [[maybe_unused]] static Record::Path _path( pid_t pid , uint64_t const* args ) {
	::string arg = _get_str(pid,args[At]) ;
	if (Record::s_is_simple(arg.c_str())) throw 0                      ;
	if (At                              ) return { Fd(args[0]) , arg } ;
	else                                  return {               arg } ;
}

static constexpr int FlagAlways = -1 ;
static constexpr int FlagNever  = -2 ;
template<int FlagArg> [[maybe_unused]] static bool _flag( uint64_t args[6] , int flag ) {
	switch (FlagArg) {
		case FlagAlways : return true                 ;
		case FlagNever  : return false                ;
		default         : return args[FlagArg] & flag ;
	}
}

// chdir
template<bool At> [[maybe_unused]] static void _entry_chdir( void* & ctx , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		if (At) { Record::Chdir* cd = new Record::Chdir( r , {Fd(args[0])          } , comment ) ; ctx = cd ; }
		else    { Record::Chdir* cd = new Record::Chdir( r , {_path<At>(pid,args+0)} , comment ) ; ctx = cd ; }
	} catch (int) {}
}
[[maybe_unused]] static int64_t/*res*/ _exit_chdir( void* ctx , Record& r , pid_t , int64_t res ) {
	if (ctx) {
		Record::Chdir* cd = static_cast<Record::Chdir*>(ctx) ;
		(*cd)(r,res) ;
		delete cd ;
	}
	return res ;
}

// chmod
template<bool At,int FlagArg> [[maybe_unused]] static void _entry_chmod( void* & ctx , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		ctx = new Record::Chmod( r , _path<At>(pid,args+0) , args[1+At]&S_IXUSR , _flag<FlagArg>(args,AT_SYMLINK_NOFOLLOW) , comment ) ;
	} catch (int) {}
}
[[maybe_unused]] static int64_t/*res*/ _exit_chmod( void* ctx , Record& r , pid_t , int64_t res ) {
	if (ctx) {
		Record::Chmod* cm = static_cast<Record::Chmod*>(ctx) ;
		(*cm)(r,res) ;
		delete cm ;
	}
	return res ;
}

// creat
[[maybe_unused]] static void _entry_creat( void* & ctx , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		ctx = new Record::Open( r , _path<false>(pid,args+0) , O_WRONLY|O_CREAT|O_TRUNC , comment ) ;
	} catch (int) {}
}
// use _exit_open as exit proc

// execve
// must be called before actual syscall execution as after execution, info is no more available
template<bool At,int FlagArg> [[maybe_unused]] static void _entry_execve( void* & /*ctx*/ , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		Record::Exec( r , _path<At>(pid,args+0) , _flag<FlagArg>(args,AT_SYMLINK_NOFOLLOW) , comment ) ;
	} catch (int) {}
}

// hard link
template<bool At,int FlagArg> [[maybe_unused]] static void _entry_lnk( void* & ctx , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	Record::Path old ;
	try           { old = _path<At>(pid,args+0) ; }
	catch (int e) { if (e) return ;               } // if e==0, old is simple, else there is an error and access will not be done
	try {
		ctx = new Record::Lnk( r , ::move(old) , _path<At>(pid,args+1+At) , _flag<FlagArg>(args,AT_SYMLINK_NOFOLLOW) , comment ) ;
	} catch (int) {}
}
[[maybe_unused]] static int64_t/*res*/ _exit_lnk( void* ctx , Record& r , pid_t /*pid */, int64_t res ) {
	if (ctx) {
		Record::Lnk* l = static_cast<Record::Lnk*>(ctx) ;
		(*l)(r,res) ;
		delete l ;
	}
	return res ;
}

// mkdir
template<bool At> [[maybe_unused]] static void _entry_mkdir( void* & /*ctx*/ , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		Record::Mkdir( r , _path<At>(pid,args+0) , comment ) ;
	} catch (int) {}
}

// mount
[[maybe_unused]] static void _entry_mount( void* & /*ctx*/ , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		if (args[3]&MS_BIND) Record::Mount( r , _path<false>(pid,args+0) , _path<false>(pid,args+1) , comment ) ;
	} catch (int) {}
}

// open
template<bool At> [[maybe_unused]] static void _entry_open( void* & ctx , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		ctx = new Record::Open( r , _path<At>(pid,args+0) , args[1+At]/*flags*/ , comment ) ;
	} catch (int) {}
}
[[maybe_unused]] static int64_t/*res*/ _exit_open( void* ctx , Record& r , pid_t /*pid*/ , int64_t res ) {
	if (ctx) {
		Record::Open* o = static_cast<Record::Open*>(ctx) ;
		(*o)( r , res ) ;
		delete o ;
	}
	return res ;
}

// read_lnk
using RLB = ::pair<Record::Readlink,uint64_t/*buf*/> ;
template<bool At> [[maybe_unused]] static void _entry_read_lnk( void* & ctx , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		uint64_t orig_buf = args[At+1]                                             ;
		size_t   sz       = args[At+2]                                             ;
		char*    buf      = pid ? new char[sz] : reinterpret_cast<char*>(orig_buf) ;
		ctx = new RLB( Record::Readlink( r , _path<At>(pid,args+0) , buf , sz , comment ) , orig_buf ) ;
	} catch (int) {}
}
[[maybe_unused]] static int64_t/*res*/ _exit_read_lnk( void* ctx , Record& r , pid_t pid , int64_t res ) {
	if (ctx) {
		RLB* rlb = static_cast<RLB*>(ctx) ;
		SWEAR( res<=ssize_t(rlb->first.sz) , res , rlb->first.sz ) ;
		if ( pid && res>=0 ) _peek( pid , rlb->first.buf , rlb->second , res ) ;
		res = (rlb->first)(r,res) ;
		if (pid) {
			if ( rlb->first.emulated && res>=0 ) _poke( pid , rlb->second , rlb->first.buf , res ) ; // access to backdoor was emulated, we must transport result to actual user space
			delete[] rlb->first.buf ;
		}
		delete rlb ;
	}
	return res ;
}

// rename
template<bool At,int FlagArg> [[maybe_unused]] static void _entry_rename( void* & ctx , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
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
		// renaming a simple file (either src or dst) is non-sense, non need to take precautions
		ctx = new Record::Rename{ r , _path<At>(pid,args+0) , _path<At>(pid,args+1+At) , exchange , no_replace , comment } ;
	} catch (int) {}
}
[[maybe_unused]] static int64_t/*res*/ _exit_rename( void* ctx , Record& r , pid_t /*pid*/ , int64_t res ) {
	if (ctx) {
		Record::Rename* rn = static_cast<Record::Rename*>(ctx) ;
		(*rn)(r,res) ;
		delete rn ;
	}
	return res ;
}

// symlink
template<bool At> [[maybe_unused]] static void _entry_symlink( void* & ctx , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		ctx = new Record::Symlink{ r , _path<At>(pid,args+1) , comment } ;
	} catch (int) {}
}
[[maybe_unused]] static int64_t/*res*/ _exit_sym_lnk( void* ctx , Record& r , pid_t , int64_t res ) {
	if (ctx) {
		Record::Symlink* sl = static_cast<Record::Symlink*>(ctx) ;
		(*sl)(r,res) ;
		delete sl ;
	}
	return res ;
}

// unlink
template<bool At,int FlagArg> [[maybe_unused]] static void _entry_unlink( void* & ctx , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	try {
		bool rmdir = _flag<FlagArg>(args,AT_REMOVEDIR) ;
		if (rmdir)           Record::Unlnk( r , _path<At>(pid,args+0) , rmdir , comment ) ; // rmdir calls us without exit, and we must not set ctx in that case
		else       ctx = new Record::Unlnk{ r , _path<At>(pid,args+0) , rmdir , comment } ;
	} catch (int) {}
}
[[maybe_unused]] static int64_t/*res*/ _exit_unlnk( void* ctx , Record& r , pid_t , int64_t res ) {
	if (ctx) {
		Record::Unlnk* u = static_cast<Record::Unlnk*>(ctx) ;
		(*u)(r,res) ;
		delete u ;
	}
	return res ;
}

// access
template<bool At,int FlagArg> [[maybe_unused]] static void _do_stat( Record& r , pid_t pid , uint64_t args[6] , Accesses a , const char* comment ) {
	try {
		Record::Stat( r , _path<At>(pid,args+0) , _flag<FlagArg>(args,AT_SYMLINK_NOFOLLOW) , a , comment ) ;
	} catch (int) {}
}
template<bool At,int FlagArg> [[maybe_unused]] static void _entry_access( void* & /*ctx*/ , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	Accesses a ; if (args[At+1]&X_OK) a |= Access::Reg ;
	_do_stat<At,FlagArg>(r,pid,args,a,comment) ;
}
template<bool At,int FlagArg> [[maybe_unused]] static void _entry_open_tree( void* & /*ctx*/ , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	_do_stat<At,FlagArg>(r,pid,args,Accesses(),comment) ;
}
template<bool At,int FlagArg> [[maybe_unused]] static void _entry_stat( void* & /*ctx*/ , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
	_do_stat<At,FlagArg>(r,pid,args,~Accesses(),comment) ;
}
#ifdef SYS_statx
	// protect statx as STATX_* macros are not defined when statx is not implemented, leading to compile errors
	[[maybe_unused]] static void _entry_statx( void* & /*ctx*/ , Record& r , pid_t pid , uint64_t args[6] , const char* comment ) {
		#if defined(STATX_TYPE) && defined(STATX_SIZE) && defined(STATX_BLOCKS) && defined(STATX_MODE)
			uint     msk = args[3] ;
			Accesses a   ;
			if      (msk&(STATX_TYPE|STATX_SIZE|STATX_BLOCKS)) a = ~Accesses() ; // user can distinguish all content
			else if (msk& STATX_MODE                         ) a = Access::Reg ; // user can distinguish executable files, which is part of crc for regular files
		#else
			Accesses a = ~Accesses() ;                                           // if access macros are not defined, be pessimistic
		#endif
		_do_stat<true,2>(r,pid,args,a,comment) ;
	}
#endif

// XXX! : find a way to put one entry per line instead of 3 lines(would be much more readable)
static constexpr SyscallDescr::Tab _build_syscall_descr_tab() {
	constexpr long NSyscalls = SyscallDescr::NSyscalls ;
	SyscallDescr::Tab s_tab = {} ;
	//	/!\ prio must be non-zero as zero means entry is not allocated
	//	entries marked NFS_GUARD are deemed data access as they touch their enclosing dir and hence must be guarded against strange NFS notion of coherence
	//	entries marked filter (i.e. field is !=0) means that processing can be skipped if corresponding arg is a file name known to require no processing
	//	                                                                                { entry           <At   ,flag      > , exit           filter,prio,is_stat  access comment      }
	#ifdef SYS_access
		static_assert(SYS_access           <NSyscalls) ; s_tab[SYS_access           ] = { _entry_access   <false,FlagNever > , nullptr        ,1    , 1  , true  , "Access"            } ;
	#endif
	#ifdef SYS_faccessat
		static_assert(SYS_faccessat        <NSyscalls) ; s_tab[SYS_faccessat        ] = { _entry_access   <true ,3         > , nullptr        ,2    , 2  , true  , "Faccessat"         } ;
	#endif
	#ifdef SYS_faccessat2
		static_assert(SYS_faccessat2       <NSyscalls) ; s_tab[SYS_faccessat2       ] = { _entry_access   <true ,3         > , nullptr        ,2    , 2  , true  , "Faccessat2"        } ;
	#endif
	#ifdef SYS_chdir
		static_assert(SYS_chdir            <NSyscalls) ; s_tab[SYS_chdir            ] = { _entry_chdir    <false           > , _exit_chdir    ,0    , 1  , false , "Chdir"             } ;
	#endif
	#ifdef SYS_fchdir
		static_assert(SYS_fchdir           <NSyscalls) ; s_tab[SYS_fchdir           ] = { _entry_chdir    <true            > , _exit_chdir    ,0    , 1  , false , "Fchdir"            } ;
	#endif
	#ifdef SYS_chmod
		static_assert(SYS_chmod            <NSyscalls) ; s_tab[SYS_chmod            ] = { _entry_chmod    <false,FlagNever > , _exit_chmod    ,1    , 1  , false , "Chmod"             } ;
	#endif
	#ifdef SYS_fchmodat
		static_assert(SYS_fchmodat         <NSyscalls) ; s_tab[SYS_fchmodat         ] = { _entry_chmod    <true ,3         > , _exit_chmod    ,2    , 1  , false , "Fchmodat"          } ;
	#endif
	#ifdef SYS_clone
		static_assert(SYS_clone            <NSyscalls) ; s_tab[SYS_clone            ] = { nullptr                            , nullptr        ,0    , 2  , false , "Clone"             } ;
	#endif
	#ifdef SYS_clone2
		static_assert(SYS_clone2           <NSyscalls) ; s_tab[SYS_clone2           ] = { nullptr                            , nullptr        ,0    , 2  , false , "Clone2"            } ;
	#endif
	#ifdef SYS_clone3
		static_assert(SYS_clone3           <NSyscalls) ; s_tab[SYS_clone3           ] = { nullptr                            , nullptr        ,0    , 2  , false , "Clone3"            } ;
	#endif
	#ifdef SYS_creat
		static_assert(SYS_creat            <NSyscalls) ; s_tab[SYS_creat            ] = { _entry_creat                       , _exit_open     ,1    , 2  , false , "Creat"             } ;
	#endif
	#ifdef SYS_execve
		static_assert(SYS_execve           <NSyscalls) ; s_tab[SYS_execve           ] = { _entry_execve   <false,FlagNever > , nullptr        ,0    , 1  , false , "Execve"            } ;
	#endif
	#ifdef SYS_execveat
		static_assert(SYS_execveat         <NSyscalls) ; s_tab[SYS_execveat         ] = { _entry_execve   <true ,4         > , nullptr        ,0    , 1  , false , "Execveat"          } ;
	#endif
	#ifdef SYS_fork
		static_assert(SYS_fork             <NSyscalls) ; s_tab[SYS_fork             ] = { nullptr                            , nullptr        ,0    , 2  , false , "Fork"              } ;
	#endif
	#ifdef SYS_link
		static_assert(SYS_link             <NSyscalls) ; s_tab[SYS_link             ] = { _entry_lnk      <false,FlagNever > , _exit_lnk      ,2    , 1  , false , "Link"              } ;
	#endif
	#ifdef SYS_linkat
		static_assert(SYS_linkat           <NSyscalls) ; s_tab[SYS_linkat           ] = { _entry_lnk      <true ,4         > , _exit_lnk      ,4    , 1  , false , "Linkat"            } ;
	#endif
	#ifdef SYS_mkdir
		static_assert(SYS_mkdir            <NSyscalls) ; s_tab[SYS_mkdir            ] = { _entry_mkdir    <false           > , nullptr        ,1    , 1  , false , "Mkdir"             } ;
	#endif
	#ifdef SYS_mkdirat
		static_assert(SYS_mkdirat          <NSyscalls) ; s_tab[SYS_mkdirat          ] = { _entry_mkdir    <true            > , nullptr        ,2    , 1  , false , "Mkdirat"           } ;
	#endif
	#ifdef SYS_mount
		static_assert(SYS_mount            <NSyscalls) ; s_tab[SYS_mount            ] = { _entry_mount                       , nullptr        ,0    , 2  , false , "Mount"             } ;
	#endif
	#ifdef SYS_name_to_handle_at
		static_assert(SYS_name_to_handle_at<NSyscalls) ; s_tab[SYS_name_to_handle_at] = { _entry_open     <true            > , _exit_open     ,2    , 1  , false , "Name_to_handle_at" } ;
	#endif
	#ifdef SYS_open
		static_assert(SYS_open             <NSyscalls) ; s_tab[SYS_open             ] = { _entry_open     <false           > , _exit_open     ,1    , 2  , false , "Open"              } ;
	#endif
	#ifdef SYS_openat
		static_assert(SYS_openat           <NSyscalls) ; s_tab[SYS_openat           ] = { _entry_open     <true            > , _exit_open     ,2    , 2  , false , "Openat"            } ;
	#endif
	#ifdef SYS_openat2
		static_assert(SYS_openat2          <NSyscalls) ; s_tab[SYS_openat2          ] = { _entry_open     <true            > , _exit_open     ,2    , 2  , false , "Openat2"           } ;
	#endif
	#ifdef SYS_open_tree
		static_assert(SYS_open_tree        <NSyscalls) ; s_tab[SYS_open_tree        ] = { _entry_open_tree<true ,2         > , nullptr        ,2    , 1  , false , "Open_tree"         } ;
	#endif
	#ifdef SYS_readlink
		static_assert(SYS_readlink         <NSyscalls) ; s_tab[SYS_readlink         ] = { _entry_read_lnk <false           > , _exit_read_lnk ,1    , 2  , false , "Readlink"          } ;
	#endif
	#ifdef SYS_readlinkat
		static_assert(SYS_readlinkat       <NSyscalls) ; s_tab[SYS_readlinkat       ] = { _entry_read_lnk <true            > , _exit_read_lnk ,2    , 2  , false , "Readlinkat"        } ;
	#endif
	#if SYS_rename
		static_assert(SYS_rename           <NSyscalls) ; s_tab[SYS_rename           ] = { _entry_rename   <false,FlagNever > , _exit_rename   ,2    , 1  , false , "Rename"            } ;
	#endif
	#ifdef SYS_renameat
		static_assert(SYS_renameat         <NSyscalls) ; s_tab[SYS_renameat         ] = { _entry_rename   <true ,FlagNever > , _exit_rename   ,4    , 1  , false , "Renameat"          } ;
	#endif
	#ifdef SYS_renameat2
		static_assert(SYS_renameat2        <NSyscalls) ; s_tab[SYS_renameat2        ] = { _entry_rename   <true ,4         > , _exit_rename   ,4    , 1  , false , "Renameat2"         } ;
	#endif
	#ifdef SYS_rmdir
		static_assert(SYS_rmdir            <NSyscalls) ; s_tab[SYS_rmdir            ] = { _entry_unlink   <false,FlagAlways> , nullptr        ,1    , 1  , false , "Rmdir"             } ;
	#endif
	#ifdef SYS_stat
		static_assert(SYS_stat             <NSyscalls) ; s_tab[SYS_stat             ] = { _entry_stat     <false,FlagNever > , nullptr        ,1    , 2  , true  , "Stat"              } ;
	#endif
	#ifdef SYS_stat64
		static_assert(SYS_stat64           <NSyscalls) ; s_tab[SYS_stat64           ] = { _entry_stat     <false,FlagNever > , nullptr        ,1    , 1  , true  , "Stat64"            } ;
	#endif
	#ifdef SYS_fstatat64
		static_assert(SYS_fstatat64        <NSyscalls) ; s_tab[SYS_fstatat64        ] = { _entry_stat     <true ,3         > , nullptr        ,2    , 1  , true  , "Fstatat64"         } ;
	#endif
	#ifdef SYS_lstat
		static_assert(SYS_lstat            <NSyscalls) ; s_tab[SYS_lstat            ] = { _entry_stat     <false,FlagAlways> , nullptr        ,1    , 2  , true  , "Lstat"             } ;
	#endif
	#ifdef SYS_lstat64
		static_assert(SYS_lstat64          <NSyscalls) ; s_tab[SYS_lstat64          ] = { _entry_stat     <false,FlagAlways> , nullptr        ,1    , 1  , true  , "Lstat64"           } ;
	#endif
	#ifdef SYS_statx
		static_assert(SYS_statx            <NSyscalls) ; s_tab[SYS_statx            ] = { _entry_statx                       , nullptr        ,2    , 1  , true  , "Statx"             } ;
	#endif
	#if SYS_newfstatat
		static_assert(SYS_newfstatat       <NSyscalls) ; s_tab[SYS_newfstatat       ] = { _entry_stat     <true ,3         > , nullptr        ,2    , 2  , true  , "Newfstatat"        } ;
	#endif
	#ifdef SYS_oldstat
		static_assert(SYS_oldstat          <NSyscalls) ; s_tab[SYS_oldstat          ] = { _entry_stat     <false,FlagNever > , nullptr        ,1    , 1  , true  , "Oldstat"           } ;
	#endif
	#ifdef SYS_oldlstat
		static_assert(SYS_oldlstat         <NSyscalls) ; s_tab[SYS_oldlstat         ] = { _entry_stat     <false,FlagAlways> , nullptr        ,1    , 1  , true  , "Oldlstat"          } ;
	#endif
	#ifdef SYS_symlink
		static_assert(SYS_symlink          <NSyscalls) ; s_tab[SYS_symlink          ] = { _entry_symlink  <false           > , _exit_sym_lnk  ,2    , 1  , false , "Symlink"           } ;
	#endif
	#ifdef SYS_symlinkat
		static_assert(SYS_symlinkat        <NSyscalls) ; s_tab[SYS_symlinkat        ] = { _entry_symlink  <true            > , _exit_sym_lnk  ,3    , 1  , false , "Symlinkat"         } ;
	#endif
	#ifdef SYS_unlink
		static_assert(SYS_unlink           <NSyscalls) ; s_tab[SYS_unlink           ] = { _entry_unlink   <false,FlagNever > , _exit_unlnk    ,1    , 1  , false , "Unlink"            } ;
	#endif
	#ifdef SYS_unlinkat
		static_assert(SYS_unlinkat         <NSyscalls) ; s_tab[SYS_unlinkat         ] = { _entry_unlink   <true ,2         > , _exit_unlnk    ,2    , 1  , false , "Unlinkat"          } ;
	#endif
	#if MAP_VFORK && defined(SYS_vfork)
		static_assert(SYS_vfork            <NSyscalls) ; s_tab[SYS_vfork            ] = { nullptr                            , nullptr        ,0    , 2  , false , "Vfork"             } ;
	#endif
	return s_tab ;
}

constexpr SyscallDescr::Tab _syscall_descr_tab = _build_syscall_descr_tab() ;
SyscallDescr::Tab const& SyscallDescr::s_tab = _syscall_descr_tab ;
