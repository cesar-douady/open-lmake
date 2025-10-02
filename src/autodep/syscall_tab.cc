// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <syscall.h> // for SYS_* macros

#include "backdoor.hh"
#include "ptrace.hh"
#include "record.hh"

#include "syscall_tab.hh"

// return null terminated string pointed by src in process pid's space
[[maybe_unused]] static ::string _get_str( pid_t pid , uint64_t src ) {
	if (!pid) return {reinterpret_cast<const char*>(src)} ;
	::string res ;
	errno = 0 ;
	for(;;) {
		uint64_t offset = src%sizeof(long)                                                 ;
		long     word   = ::ptrace( PTRACE_PEEKDATA , pid , src-offset , nullptr/*data*/ ) ;
		throw_if( errno , "cannot peek data from address ",src-offset ) ;
		char* word_char = ::launder(reinterpret_cast<char*>(&word)) ;
		for( uint64_t len : iota(sizeof(long)-offset) ) if (!word_char[offset+len]) { res.append( word_char+offset , len                 ) ; return res ; }
		/**/                                                                          res.append( word_char+offset , sizeof(long)-offset ) ;
		src += sizeof(long)-offset ;
	}
}

// copy process pid's space @ src to dst
[[maybe_unused]] static void _peek( pid_t pid , char* dst , uint64_t src , size_t sz ) {
	SWEAR(pid) ;
	errno = 0 ;
	for( size_t copied ; sz ; src+=copied,dst+=copied,sz-=copied ) {                       // invariant : copy src[0:sz] to dst
		size_t offset = src%sizeof(long)                                                 ;
		long   word   = ::ptrace( PTRACE_PEEKDATA , pid , src-offset , nullptr/*data*/ ) ;
		throw_if( errno , "cannot peek data from address ",src-offset ) ;
		copied = ::min( sizeof(long) - offset , sz ) ;
		::memcpy( dst , ::launder(reinterpret_cast<char*>(&word))+offset , copied ) ;
	}
}
// copy src to process pid's space @ dst
[[maybe_unused]] static void _poke( pid_t pid , uint64_t dst , const char* src , size_t sz ) {
	SWEAR(pid) ;
	errno = 0 ;
	for( size_t copied ; sz ; src+=copied,dst+=copied,sz-=copied) {                        // invariant : copy src[0:sz] to dst
		size_t offset = dst%sizeof(long) ;
		long   word   = 0/*garbage*/     ;
		copied = ::min( sizeof(long) - offset , sz ) ;
		if ( offset || offset+copied<sizeof(long) ) {                                      // partial word
			word = ::ptrace( PTRACE_PEEKDATA , pid , dst-offset , nullptr/*data*/ ) ;
			throw_if( errno , "cannot peek data from address ",dst-offset ) ;
		}
		::memcpy( ::launder(reinterpret_cast<char*>(&word))+offset , src , copied ) ;
		::ptrace( PTRACE_POKEDATA , pid , dst-offset , word ) ;
		throw_if( errno , "cannot poke data to address ",dst-offset ) ;
	}
	}

template<bool At> [[maybe_unused]] static Record::Path _path( pid_t pid , uint64_t const* args , bool simple_ok=false ) {
	::string arg = _get_str(pid,args[At]) ;
	if ( !simple_ok && Record::s_is_simple(arg) ) throw ""s                    ;
	if ( At                                     ) return { Fd(args[0]) , arg } ;
	else                                          return {               arg } ;
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
template<bool At> [[maybe_unused]] static void _entry_chdir( void*& ctx , Record& r , pid_t pid , uint64_t args[6] , Comment c ) {
	try {
		if (At) { Record::Chdir* cd = new Record::Chdir{ r , Fd(args[0])                             , c } ; ctx = cd ; }
		else    { Record::Chdir* cd = new Record::Chdir{ r , _path<At>(pid,args+0,true/*simple_ok*/) , c } ; ctx = cd ; }
	} catch (::string const&) {}
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
template<bool At,int FlagArg> [[maybe_unused]] static void _entry_chmod( void*& ctx , Record& r , pid_t pid , uint64_t args[6] , Comment c ) {
	try {
		ctx = new Record::Chmod{ r , _path<At>(pid,args+0) , bool(args[1+At]&S_IXUSR) , _flag<FlagArg>(args,AT_SYMLINK_NOFOLLOW) , c } ;
	} catch (::string const&) {}
}
[[maybe_unused]] static int64_t/*res*/ _exit_chmod( void* ctx , Record& r , pid_t , int64_t res ) {
	if (ctx) {
		Record::Chmod* cm = static_cast<Record::Chmod*>(ctx) ;
		(*cm)(r,res) ;
		delete cm ;
	}
	return res ;
}

// chroot
[[maybe_unused]] static void _entry_chroot( void*& /*ctx*/ , Record& r , pid_t pid , uint64_t args[6] , Comment c ) {
	try {
		Record::Chroot( r , _path<false>(pid,args+0) , c ) ;
	} catch (::string const&) {}
}

// creat
[[maybe_unused]] static void _entry_creat( void*& ctx , Record& r , pid_t pid , uint64_t args[6] , Comment c ) {
	try {
		ctx = new Record::Open{ r , _path<false>(pid,args+0) , O_WRONLY|O_CREAT|O_TRUNC , c } ;
	} catch (::string const&) {}
}
// use _exit_open as exit proc

// execve
// must be called before actual syscall execution as after execution, info is no more available
template<bool At,int FlagArg> [[maybe_unused]] static void _entry_execve( void*& /*ctx*/ , Record& r , pid_t pid , uint64_t args[6] , Comment c ) {
	try {
		Record::Exec<true/*Send*/,false/*ChkSimple*/>( r , _path<At>(pid,args+0,true/*simple_ok*/) , _flag<FlagArg>(args,AT_SYMLINK_NOFOLLOW) , c ) ;
	} catch (::string const&) {}
}

// getdents
[[maybe_unused]] static void _entry_getdents( void*& ctx , Record& r , pid_t , uint64_t args[6] , Comment c ) {
	ctx = new Record::ReadDir{ r , Fd(args[0]) , c } ;
}
[[maybe_unused]] static int64_t/*res*/ _exit_getdents( void* ctx , Record& r , pid_t , int64_t res ) {
	if (ctx) {
		Record::ReadDir* rd = static_cast<Record::ReadDir*>(ctx) ;
		(*rd)( r , res ) ;
		delete rd ;
	}
	return res ;
}

// hard link
template<bool At,int FlagArg> [[maybe_unused]] static void _entry_lnk( void*& ctx , Record& r , pid_t pid , uint64_t args[6] , Comment c ) {
	try {
		ctx = new Record::Lnk{ r , _path<At>(pid,args+0,true/*simple_ok*/) , _path<At>(pid,args+1+At) , _flag<FlagArg>(args,AT_SYMLINK_NOFOLLOW) , c } ;
	} catch (::string const&) {}
}
[[maybe_unused]] static int64_t/*res*/ _exit_lnk( void* ctx , Record& r , pid_t , int64_t res ) {
	if (ctx) {
		Record::Lnk* l = static_cast<Record::Lnk*>(ctx) ;
		(*l)(r,res) ;
		delete l ;
	}
	return res ;
}

// mkdir
template<bool At> [[maybe_unused]] static void _entry_mkdir( void*& /*ctx*/ , Record& r , pid_t pid , uint64_t args[6] , Comment c ) {
	try {
		Record::Mkdir( r , _path<At>(pid,args+0) , c ) ;
	} catch (::string const&) {}
}

// mount
[[maybe_unused]] static void _entry_mount( void*& /*ctx*/ , Record& r , pid_t pid , uint64_t args[6] , Comment c ) {
	try {
		if (args[3]&MS_BIND) Record::Mount( r , _path<false>(pid,args+0,true/*simple_ok*/) , _path<false>(pid,args+1) , c ) ;
	} catch (::string const&) {}
}

// open
template<bool At> [[maybe_unused]] static void _entry_open( void*& ctx , Record& r , pid_t pid , uint64_t args[6] , Comment c ) {
	try {
		ctx = new Record::Open{ r , _path<At>(pid,args+0) , int(args[1+At])/*flags*/ , c } ;
	} catch (::string const&) {}
}
[[maybe_unused]] static int64_t/*res*/ _exit_open( void* ctx , Record& r , pid_t , int64_t res ) {
	if (ctx) {
		Record::Open* o = static_cast<Record::Open*>(ctx) ;
		(*o)( r , res ) ;
		delete o ;
	}
	return res ;
}

// read_lnk
using ReadLinkBuf = ::pair<Record::Readlink,uint64_t/*buf*/> ;
template<bool At> [[maybe_unused]] static void _entry_read_lnk( void*& ctx , Record& r , pid_t pid , uint64_t args[6] , Comment c ) {
	try {
		uint64_t orig_buf = args[At+1]                                        ;
		size_t   sz       = args[At+2]                                        ;
		char*    buf      = pid ? nullptr : reinterpret_cast<char*>(orig_buf) ;
		ctx = new ReadLinkBuf( Record::Readlink{ r , _path<At>(pid,args+0) , buf , sz , c } , orig_buf ) ;
	} catch (::string const&) {}
}
[[maybe_unused]] static int64_t/*res*/ _exit_read_lnk( void* ctx , Record& r , pid_t pid , int64_t res ) {
	if (ctx) {
		ReadLinkBuf* rlb = static_cast<ReadLinkBuf*>(ctx) ;
		res = (rlb->first)(r,res) ; SWEAR( res<=ssize_t(rlb->first.sz) , res,rlb->first.sz ) ;
		if ( pid && rlb->first.magic ) {                                                       // access to backdoor was emulated, we must transport result to actual user space
			if (res>=0)
				try {
					_poke( pid , rlb->second , rlb->first.buf , res ) ;
				} catch (::string const&) {
					Fd::Stderr.write(cat("poke error when processing ",rlb->first.backdoor_descr,'\n')) ;
					errno = EFAULT                 ;
					res   = -+BackdoorErr::PokeErr ;                                           // distinguish between backdoor error and absence of support
				}
			delete[] rlb->first.buf ;                                                          // buf has been allocated when processing magic
		}
		delete rlb ;
	}
	return res ;
}

// rename
template<bool At,int FlagArg> [[maybe_unused]] static void _entry_rename( void*& ctx , Record& r , pid_t pid , uint64_t args[6] , Comment c ) {
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
		ctx = new Record::Rename{ r , _path<At>(pid,args+0) , _path<At>(pid,args+1+At) , exchange , no_replace , c } ;
	} catch (::string const&) {}
}
[[maybe_unused]] static int64_t/*res*/ _exit_rename( void* ctx , Record& r , pid_t , int64_t res ) {
	if (ctx) {
		Record::Rename* rn = static_cast<Record::Rename*>(ctx) ;
		(*rn)(r,res) ;
		delete rn ;
	}
	return res ;
}

// symlink
template<bool At> [[maybe_unused]] static void _entry_symlink( void*& ctx , Record& r , pid_t pid , uint64_t args[6] , Comment c ) {
	try {
		ctx = new Record::Symlink{ r , _path<At>(pid,args+1) , c } ;
	} catch (::string const&) {}
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
template<bool At,int FlagArg> [[maybe_unused]] static void _entry_unlink( void*& ctx , Record& r , pid_t pid , uint64_t args[6] , Comment c ) {
	try {
		bool rmdir = _flag<FlagArg>(args,AT_REMOVEDIR) ;
		if (rmdir)           Record::Unlnk( r , _path<At>(pid,args+0) , rmdir , c ) ; // rmdir calls us without exit, and we must not set ctx in that case
		else       ctx = new Record::Unlnk{ r , _path<At>(pid,args+0) , rmdir , c } ;
	} catch (::string const&) {}
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
template<bool At,int FlagArg> [[maybe_unused]] static void _do_stat( Record& r , pid_t pid , uint64_t args[6] , Accesses a , Comment c ) {
	try {
		Record::Stat( r , _path<At>(pid,args+0) , _flag<FlagArg>(args,AT_SYMLINK_NOFOLLOW) , a , c ) ;
	} catch (::string const&) {}
}
template<bool At,int FlagArg> [[maybe_unused]] static void _entry_access( void*& /*ctx*/ , Record& r , pid_t pid , uint64_t args[6] , Comment c ) {
	Accesses a ; if (args[At+1]&X_OK) a |= Access::Reg ;
	_do_stat<At,FlagArg>(r,pid,args,a,c) ;
}
template<bool At,int FlagArg> [[maybe_unused]] static void _entry_open_tree( void*& /*ctx*/ , Record& r , pid_t pid , uint64_t args[6] , Comment c ) {
	_do_stat<At,FlagArg>(r,pid,args,Accesses(),c) ;
}
template<bool At,int FlagArg> [[maybe_unused]] static void _entry_stat( void*& /*ctx*/ , Record& r , pid_t pid , uint64_t args[6] , Comment c ) {
	_do_stat<At,FlagArg>(r,pid,args,~Accesses(),c) ;
}

[[maybe_unused]] static void _entry_statx( void*& /*ctx*/ , Record& r , pid_t pid , uint64_t args[6] , Comment c ) {
	#if defined(STATX_TYPE) && defined(STATX_SIZE) && defined(STATX_BLOCKS) && defined(STATX_MODE)
		uint     msk = args[3] ;
		Accesses a   ;
		if      (msk&(STATX_TYPE|STATX_SIZE|STATX_BLOCKS)) a = ~Accesses() ; // user can distinguish all content
		else if (msk& STATX_MODE                         ) a = Access::Reg ; // user can distinguish executable files, which is part of crc for regular files
	#else
		Accesses a = ~Accesses() ;                                           // if access macros are not defined, be pessimistic
	#endif
	_do_stat<true,2>(r,pid,args,a,c) ;
}

static constexpr SyscallDescr::Tab _build_syscall_descr_tab() {
	SyscallDescr::Tab s_tab = {} ;
	#define FILL_ENTRY( n , ... ) {                                         \
		constexpr long i = MACRO_VAL(n,-1L) ;                               \
		if constexpr (i>=0) {                                               \
			static_assert(i<SyscallDescr::NSyscalls,"increase NSyscalls") ; \
			s_tab[i] = SyscallDescr __VA_ARGS__ ;                           \
		}                                                                   \
	}
	// entries marked filter (i.e. field is !=0) means that processing can be skipped if corresponding arg is a file name known to require no processing
	//                                    entry           <At   ,FlagArg   > , exit           filter comment
	FILL_ENTRY( SYS_access            , { _entry_access   <false,FlagNever > , nullptr        , 1  , Comment::access            } ) ;
	FILL_ENTRY( SYS_faccessat         , { _entry_access   <true ,3         > , nullptr        , 2  , Comment::faccessat         } ) ;
	FILL_ENTRY( SYS_faccessat2        , { _entry_access   <true ,3         > , nullptr        , 2  , Comment::faccessat2        } ) ;
	FILL_ENTRY( SYS_chdir             , { _entry_chdir    <false           > , _exit_chdir    , 0  , Comment::chdir             } ) ;
	FILL_ENTRY( SYS_fchdir            , { _entry_chdir    <true            > , _exit_chdir    , 0  , Comment::fchdir            } ) ;
	FILL_ENTRY( SYS_chmod             , { _entry_chmod    <false,FlagNever > , _exit_chmod    , 1  , Comment::chmod             } ) ;
	FILL_ENTRY( SYS_fchmodat          , { _entry_chmod    <true ,3         > , _exit_chmod    , 2  , Comment::fchmodat          } ) ;
	FILL_ENTRY( SYS_chroot            , { _entry_chroot                      , nullptr        , 0  , Comment::chroot            } ) ;
	FILL_ENTRY( SYS_creat             , { _entry_creat                       , _exit_open     , 1  , Comment::creat             } ) ;
	FILL_ENTRY( SYS_execve            , { _entry_execve   <false,FlagNever > , nullptr        , 0  , Comment::execve            } ) ;
	FILL_ENTRY( SYS_execveat          , { _entry_execve   <true ,4         > , nullptr        , 0  , Comment::execveat          } ) ;
	FILL_ENTRY( SYS_getdents          , { _entry_getdents                    , _exit_getdents , 0  , Comment::getdents          } ) ;
	FILL_ENTRY( SYS_getdents64        , { _entry_getdents                    , _exit_getdents , 0  , Comment::getdents64        } ) ;
	FILL_ENTRY( SYS_link              , { _entry_lnk      <false,FlagNever > , _exit_lnk      , 2  , Comment::link              } ) ;
	FILL_ENTRY( SYS_linkat            , { _entry_lnk      <true ,4         > , _exit_lnk      , 4  , Comment::linkat            } ) ;
	FILL_ENTRY( SYS_mkdir             , { _entry_mkdir    <false           > , nullptr        , 1  , Comment::mkdir             } ) ;
	FILL_ENTRY( SYS_mkdirat           , { _entry_mkdir    <true            > , nullptr        , 2  , Comment::mkdirat           } ) ;
	FILL_ENTRY( SYS_mount             , { _entry_mount                       , nullptr        , 0  , Comment::mount             } ) ;
	FILL_ENTRY( SYS_name_to_handle_at , { _entry_open     <true            > , _exit_open     , 2  , Comment::name_to_handle_at } ) ;
	FILL_ENTRY( SYS_open              , { _entry_open     <false           > , _exit_open     , 1  , Comment::open              } ) ;
	FILL_ENTRY( SYS_openat            , { _entry_open     <true            > , _exit_open     , 2  , Comment::openat            } ) ;
	FILL_ENTRY( SYS_openat2           , { _entry_open     <true            > , _exit_open     , 2  , Comment::openat2           } ) ;
	FILL_ENTRY( SYS_open_tree         , { _entry_open_tree<true ,2         > , nullptr        , 2  , Comment::open_tree         } ) ;
	FILL_ENTRY( SYS_readlink          , { _entry_read_lnk <false           > , _exit_read_lnk , 1  , Comment::readlink          } ) ;
	FILL_ENTRY( SYS_readdir           , { _entry_getdents                    , _exit_getdents , 0  , Comment::readdir           } ) ;
	FILL_ENTRY( SYS_readlinkat        , { _entry_read_lnk <true            > , _exit_read_lnk , 2  , Comment::readlinkat        } ) ;
	FILL_ENTRY( SYS_rename            , { _entry_rename   <false,FlagNever > , _exit_rename   , 2  , Comment::rename            } ) ;
	FILL_ENTRY( SYS_renameat          , { _entry_rename   <true ,FlagNever > , _exit_rename   , 4  , Comment::renameat          } ) ;
	FILL_ENTRY( SYS_renameat2         , { _entry_rename   <true ,4         > , _exit_rename   , 4  , Comment::renameat2         } ) ;
	FILL_ENTRY( SYS_rmdir             , { _entry_unlink   <false,FlagAlways> , nullptr        , 1  , Comment::rmdir             } ) ;
	FILL_ENTRY( SYS_stat              , { _entry_stat     <false,FlagNever > , nullptr        , 1  , Comment::stat              } ) ;
	FILL_ENTRY( SYS_stat64            , { _entry_stat     <false,FlagNever > , nullptr        , 1  , Comment::stat64            } ) ;
	FILL_ENTRY( SYS_fstatat64         , { _entry_stat     <true ,3         > , nullptr        , 2  , Comment::fstatat64         } ) ;
	FILL_ENTRY( SYS_lstat             , { _entry_stat     <false,FlagAlways> , nullptr        , 1  , Comment::lstat             } ) ;
	FILL_ENTRY( SYS_lstat64           , { _entry_stat     <false,FlagAlways> , nullptr        , 1  , Comment::lstat64           } ) ;
	FILL_ENTRY( SYS_statx             , { _entry_statx                       , nullptr        , 2  , Comment::statx             } ) ;
	FILL_ENTRY( SYS_newfstatat        , { _entry_stat     <true ,3         > , nullptr        , 2  , Comment::newfstatat        } ) ;
	FILL_ENTRY( SYS_oldstat           , { _entry_stat     <false,FlagNever > , nullptr        , 1  , Comment::oldstat           } ) ;
	FILL_ENTRY( SYS_oldlstat          , { _entry_stat     <false,FlagAlways> , nullptr        , 1  , Comment::oldlstat          } ) ;
	FILL_ENTRY( SYS_symlink           , { _entry_symlink  <false           > , _exit_sym_lnk  , 2  , Comment::symlink           } ) ;
	FILL_ENTRY( SYS_symlinkat         , { _entry_symlink  <true            > , _exit_sym_lnk  , 3  , Comment::symlinkat         } ) ;
	FILL_ENTRY( SYS_unlink            , { _entry_unlink   <false,FlagNever > , _exit_unlnk    , 1  , Comment::unlink            } ) ;
	FILL_ENTRY( SYS_unlinkat          , { _entry_unlink   <true ,2         > , _exit_unlnk    , 2  , Comment::unlinkat          } ) ;
	#undef FILL_ENTRY
	#undef FILL_ENTRY_STR
	return s_tab ;
}

constexpr SyscallDescr::Tab _syscall_descr_tab = _build_syscall_descr_tab() ;
SyscallDescr::Tab const& SyscallDescr::s_tab = _syscall_descr_tab ;
