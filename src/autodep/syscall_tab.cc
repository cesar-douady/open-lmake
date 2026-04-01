// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <linux/seccomp.h>
#include <syscall.h>       // for SYS_* macros
#ifdef SYS_openat2
	#include <linux/openat2.h>
#endif

#include "world_32.h"

#include "non_portable.hh"

#include "record.hh"

#include "syscall_tab.hh"

[[maybe_unused]] static void _peek( Fd proc_mem , char* dst , uint64_t src , size_t sz ) {
	if (!proc_mem) { ::memcpy( dst , reinterpret_cast<const char*>(src) , sz ) ; return ; }
	while (sz) {
		ssize_t cnt = ::pread( proc_mem , dst , sz , src ) ; if (cnt<=0) throw cat("cannot peek at address 0x",to_hex(src)) ;
		src += cnt ;
		dst += cnt ;
		sz  -= cnt ;
	}
}

[[maybe_unused]] static void _poke( Fd proc_mem , uint64_t dst , const char* src , size_t sz ) {
	if (!proc_mem) { ::memcpy( reinterpret_cast<char*>(dst) , src , sz ) ; return ; }
	while (sz) {
		ssize_t cnt = ::pwrite( proc_mem , src , sz , dst ) ; if (cnt<=0) throw cat("cannot poke at address 0x",to_hex(dst)) ;
		src += cnt ;
		dst += cnt ;
		sz  -= cnt ;
	}
}

// return null terminated string pointed by src in process space
[[maybe_unused]] static ::string _get_str( Fd proc_mem , uint64_t src ) {
	if (!proc_mem) return {reinterpret_cast<const char*>(src)} ;
	::string res                      ;
	char     buf[::min(PAGE_SZ,1024)] ; // filenames longer than 1024 are really exceptional, no need to anticipate more than that
	for (;;) {
		size_t  sz  = ::min( sizeof(buf) , size_t(PAGE_SZ-src%PAGE_SZ) ) ;
		ssize_t cnt = ::pread( proc_mem , buf , sz , src )               ; if (cnt<=0) throw cat("cannot peek name from address 0x",to_hex(src)) ;
		size_t  l   = ::strnlen( buf , sz )                              ;
		res.append( buf , l ) ;
		if (l<sz) break ;
		src += l ;
	}
	return res ;
}

template<bool At,bool KeepSimple=false> [[maybe_unused]] static Record::Path _path( Fd proc_mem , uint64_t const* args ) {
	::string arg = _get_str(proc_mem,args[At]) ;
	if ( !KeepSimple && Record::s_is_simple(arg) ) throw ""s                    ;
	if ( At                                      ) return { Fd(args[0]) , arg } ;
	else                                           return {               arg } ;
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

template<class T> ::pair<int64_t/*rc*/,int/*errno*/> _do_exit( void* ctx_ , ::optional<int64_t> rc
,	::function<int64_t(T&              )> emulate_proc
,	::function<void   (T&,int64_t/*rc*/)> record_proc =[](T&,int64_t){}
) {
	T& ctx = *static_cast<T*>(ctx_) ;
	if (!rc) {
		errno = 0                 ;
		rc    = emulate_proc(ctx) ;
	}
	int e = errno ;
	record_proc(ctx,*rc) ;
	delete &ctx ;
	return {*rc,e} ;
}

// chdir
template<bool At> [[maybe_unused]] static ::pair<void* /*ctx*/,bool/*refresh*/> _entry_chdir( Record& r , Fd proc_mem , uint64_t args[6] , bool /*emulate*/ , Comment c ) {
	try {
		if (At) { Record::Chdir cd { r , Fd(args[0])                                   , c } ; cd(r) ; }
		else    { Record::Chdir cd { r , _path<At,true/*KeepSimple*/>(proc_mem,args+0) , c } ; cd(r) ; }
	} catch (::string const&) {}
	return {} ;
}

// chmod
struct ChmodHelper {
	Record::Chmod chmod ;
	mode_t        mod   ;
} ;
template<bool At,int FlagArg> [[maybe_unused]] static ::pair<void* /*ctx*/,bool/*refresh*/> _entry_chmod( Record& r , Fd proc_mem , uint64_t args[6] , bool /*emulate*/ , Comment c ) {
	try {
		ChmodHelper& cm = *new ChmodHelper{ .chmod={r,_path<At>(proc_mem,args+0),bool(args[At+1]&S_IXUSR),_flag<FlagArg>(args,AT_SYMLINK_NOFOLLOW),c} , .mod=mode_t(args[At+1]) } ;
		if (cm.chmod.confirm_id) return { &cm , false/*refresh*/ } ;
		else                     delete &cm ;                        // nothing to do on exit
	} catch (::string const&) {}
	return {} ;
}
[[maybe_unused]] static ::pair<int64_t/*rc*/,int/*errno*/> _exit_chmod( void* ctx , Record& r , Fd , ::optional<int64_t> rc ) {
	return _do_exit<ChmodHelper>( ctx , rc
	,	[ ](ChmodHelper& cm           )->int64_t { return ::chmod( cm.chmod.real_write().c_str() , cm.mod ) ; }
	,	[&](ChmodHelper& cm,int64_t rc)          { cm.chmod( r , rc ) ;                                       }
	) ;
}

// chroot
[[maybe_unused]] static ::pair<void* /*ctx*/,bool/*refresh*/> _entry_chroot( Record& r , Fd proc_mem , uint64_t args[6] , bool emulate , Comment c ) {
	try { //!                                                           At
		if (emulate) { Record::Chroot  cr {                     r,_path<false>(proc_mem,args+0),c} ; cr(r) ; return {                    } ; } // cannot emulate chroot, record access in all cases
		else         { Record::Chroot& cr = *new Record::Chroot{r,_path<false>(proc_mem,args+0),c} ;         return {&cr,false/*refresh*/} ; }
	} catch (::string const&) {}
	return {} ;
}
[[maybe_unused]] static ::pair<int64_t/*rc*/,int/*errno*/> _exit_chroot( void* ctx , Record& r , Fd , ::optional<int64_t> rc ) {
	return _do_exit<Record::Chroot>( ctx , rc
	,	[ ](Record::Chroot&              )->int64_t { FAIL() ;   }                                                                             // cannot emulate chroot
	,	[&](Record::Chroot& cr,int64_t rc)          { cr(r,rc) ; }
	) ;
}

// creat
struct CreatHelper {
	Record::Open open ;
	mode_t       mod  ;
} ;
[[maybe_unused]] static ::pair<void* /*ctx*/,bool/*refresh*/> _entry_creat( Record& r , Fd proc_mem , uint64_t args[6] , bool /*emulate*/ , Comment c ) {
	try {
		CreatHelper& cr = *new CreatHelper{ .open={r,_path<false/*At*/>(proc_mem,args+0),O_WRONLY|O_CREAT|O_TRUNC,c} , .mod=mode_t(args[1]) }  ;
		if (cr.open.confirm_id) return { &cr , false/*refresh*/ } ;
		else                    delete &cr ;                        // nothing to do on exit
	} catch (::string const&) {}
	return {} ;
}
[[maybe_unused]] static ::pair<int64_t/*rc*/,int/*errno*/> _exit_creat( void* ctx , Record& r , Fd , ::optional<int64_t> rc ) {
	return _do_exit<CreatHelper>( ctx , rc
	,	[ ](CreatHelper& c           )->int64_t { return ::creat( c.open.real_write().c_str() , c.mod ) ; }
	,	[&](CreatHelper& c,int64_t rc)          { c.open( r , rc ) ;                                      }
	) ;
}

// execve
// must be called before actual syscall execution as after execution, info is no more available
template<bool At,int FlagArg> [[maybe_unused]] static ::pair<void* /*ctx*/,bool/*refresh*/> _entry_execve( Record& r , Fd proc_mem , uint64_t args[6] , bool /*emulate*/ , Comment c ) {
	try {
		Record::Exec<true/*Send*/,false/*SkipSimple*/>( r , _path<At,true/*KeepSimple*/>(proc_mem,args+0) , _flag<FlagArg>(args,AT_SYMLINK_NOFOLLOW) , c ) ;
		return { nullptr/*ctx*/ , true/*refresh*/ } ; // process mem and word size change, tell tracer
	} catch (::string const&) {}
	return {} ;
}

// getdents
[[maybe_unused]] static ::pair<void* /*ctx*/,bool/*refresh*/> _entry_getdents( Record& r , Fd , uint64_t args[6] , bool emulate , Comment c ) {
	if (emulate) { Record::ReadDir  rd {                      r,Fd(args[0]),c} ; rd(r) ; return {                    } ; }                      // cannot emulate readdir, record access in all cases
	else         { Record::ReadDir& rd = *new Record::ReadDir{r,Fd(args[0]),c} ;         return {&rd,false/*refresh*/} ; }
}
[[maybe_unused]] static ::pair<int64_t/*rc*/,int/*errno*/> _exit_getdents( void* ctx , Record& r , Fd , ::optional<int64_t> rc ) {
	return _do_exit<Record::ReadDir>( ctx , rc
	,	[ ](Record::ReadDir&              )->int64_t { FAIL() ;   }                                                                             // cannot emulate getdents
	,	[&](Record::ReadDir& rd,int64_t rc)          { rd(r,rc) ; }
	) ;
}

// hard link
template<bool At,int FlagArg> [[maybe_unused]] static ::pair<void* /*ctx*/,bool/*refresh*/> _entry_lnk( Record& r , Fd proc_mem , uint64_t args[6] , bool /*emulate*/ , Comment c ) {
	try {
		Record::Lnk& l = *new Record::Lnk{ r , _path<At,true/*KeepSimple*/>(proc_mem,args+0) , _path<At>(proc_mem,args+1+At) , _flag<FlagArg>(args,AT_SYMLINK_NOFOLLOW) , c } ;
		if (l.dst.confirm_id) return { &l , false/*refresh*/ } ;
		else                  delete &l ;                        // nothing to do on exit
	} catch (::string const&) {}
	return {} ;
}
[[maybe_unused]] static ::pair<int64_t/*rc*/,int/*errno*/> _exit_lnk( void* ctx , Record& r , Fd , ::optional<int64_t> rc ) {
	return _do_exit<Record::Lnk>( ctx , rc
	,	[ ](Record::Lnk& l           )->int64_t { return ::link( l.src.real.c_str() , l.dst.real_write().c_str() ) ; }
	,	[&](Record::Lnk& l,int64_t rc)          { l( r , rc ) ;                                                      }
	) ;
}

// mkdir
template<bool At> [[maybe_unused]] static ::pair<void* /*ctx*/,bool/*refresh*/> _entry_mkdir( Record& r , Fd proc_mem , uint64_t args[6] , bool /*emulate*/ , Comment c ) {
	try {
		Record::Mkdir( r , _path<At>(proc_mem,args+0) , c ) ;
	} catch (::string const&) {}
	return {} ;
}

// mount
[[maybe_unused]] static ::pair<void* /*ctx*/,bool/*refresh*/> _entry_mount( Record& r , Fd proc_mem , uint64_t args[6] , bool emulate , Comment c ) {
	try { //!                                                        At
		if (emulate) { Record::Mount  m {                    r,_path<false>(proc_mem,args+1),c} ; m(r) ; return {                   } ; } // cannot emulate mount, record access in all cases
		else         { Record::Mount& m = *new Record::Mount{r,_path<false>(proc_mem,args+1),c} ;        return {&m,false/*refresh*/} ; }
	} catch (::string const&) {}
	return {} ;
}
[[maybe_unused]] static ::pair<int64_t/*rc*/,int/*errno*/> _exit_mount( void* ctx , Record& r , Fd , ::optional<int64_t> rc ) {
	return _do_exit<Record::Mount>( ctx , rc
	,	[ ](Record::Mount&             )->int64_t { FAIL() ;  }                                                                           // cannot emulate mount
	,	[&](Record::Mount& m,int64_t rc)          { m(r,rc) ; }
	) ;
}

// name_to_handle_at (open_by_handle_at is priviledged, no need to handle it)
[[maybe_unused]] static ::pair<void* /*ctx*/,bool/*refresh*/> _entry_name_to_handle_at( Record& r , Fd proc_mem , uint64_t args[6] , bool /*emulate*/ , Comment c ) {
	try {
		Record::Solve( r , _path<true/*At*/>(proc_mem,args+0) , !(args[4]&AT_SYMLINK_FOLLOW) , false/*read*/ , false/*create*/ , c ) ;
	} catch (::string const&) {}
	return {} ;
}

// open
struct OpenHelper {
	Record::Open open  ;
	int          flags ;
	mode_t       mod   ;
} ;
template<bool At> [[maybe_unused]] static ::pair<void* /*ctx*/,bool/*refresh*/> _entry_open( Record& r , Fd proc_mem , uint64_t args[6] , bool /*emulate*/ , Comment c ) {
	try {
		OpenHelper& o = *new OpenHelper{ .open={r,_path<At>(proc_mem,args+0),int(args[At+1])/*flags*/,c} , .flags=int(args[At+1]) , .mod=mode_t(args[At+2]) } ;
		if (o.open.confirm_id) return { &o , false/*refresh*/ } ;
		else                   delete &o ;                                                                      // nothing to do on exit
	} catch (::string const&) {}
	return {} ;
}
[[maybe_unused]] static ::pair<int64_t/*rc*/,int/*errno*/> _exit_open( void* ctx , Record& r , Fd , ::optional<int64_t> rc ) {
	return _do_exit<OpenHelper>( ctx , rc
	,	[ ](OpenHelper& o           )->int64_t { return ::open( o.open.real_write().c_str() , o.flags , o.mod ) ; }
	,	[&](OpenHelper& o,int64_t rc)          { o.open( r , rc ) ;                                               }
	) ;
}
#ifdef SYS_openat2
	struct Openat2Helper {
		Record::Open      open ;
		struct ::open_how how  = {} ;
	} ;
	[[maybe_unused]] static ::pair<void* /*ctx*/,bool/*refresh*/> _entry_open2( Record& r , Fd proc_mem , uint64_t args[6] , bool /*emulate*/ , Comment c ) {
		struct ::open_how how ;
		try                     { _peek( proc_mem , reinterpret_cast<char*>(&how) , args[2] , sizeof(how) ) ; }
		catch (::string const&) { return {} ;                                                                 }
		throw_if( how.flags&RESOLVE_BENEATH , "openat2 flag RESOLV_BENEATH not yet implemented" ) ;             // XXX! : implement
		try {
			Openat2Helper& o2 = *new Openat2Helper{ .open={r,_path<true/*At*/>(proc_mem,args+0),int(how.flags),c} , .how=how } ;
			if (o2.open.confirm_id) return { &o2 , false/*refresh*/ } ;
			else                    delete &o2 ;                                                                // nothing to do on exit
		} catch (::string const&) {}
		return {} ;
	}
	[[maybe_unused]] static ::pair<int64_t/*rc*/,int/*errno*/> _exit_open2( void* ctx , Record& r , Fd , ::optional<int64_t> rc ) {
		return _do_exit<Openat2Helper>( ctx , rc
		,	[ ](Openat2Helper& o2           )->int64_t { return ::syscall( SYS_openat2 , Fd::Cwd , o2.open.real_write().c_str() , &o2.how , sizeof(o2.how) ) ; }
		,	[&](Openat2Helper& o2,int64_t rc)          { o2.open( r , rc ) ;                                                                                   }
		) ;
	}
#endif

// read_lnk
struct ReadlinkHelper {
	Record::Readlink read_lnk ;
	uint64_t         buf      ;
} ;
template<bool At> [[maybe_unused]] static ::pair<void* /*ctx*/,bool/*refresh*/> _entry_read_lnk( Record& r , Fd proc_mem , uint64_t args[6] , bool /*emulate*/ , Comment c ) {
	try {
		char*           buf = +proc_mem ? nullptr : reinterpret_cast<char*>(args[At+1])                                                  ;
		ReadlinkHelper& rl  = *new ReadlinkHelper{ .read_lnk={r,_path<At>(proc_mem,args+0),buf,size_t(args[At+2]),c} , .buf=args[At+1] } ;
		if (rl.read_lnk.magic) return { &rl , false/*refresh*/ } ;
		else                   delete &rl ;
	} catch (::string const&) {}
	return {} ;
}
[[maybe_unused]] static ::pair<int64_t/*rc*/,int/*errno*/> _exit_read_lnk( void* ctx , Record& r , Fd proc_mem , ::optional<int64_t> rc ) {
	if (!rc) rc = 0 ;
	return _do_exit<ReadlinkHelper>( ctx , {}/*rc*/
	,	[&](ReadlinkHelper& rl)->int64_t {
			SWEAR( rl.read_lnk.magic ) ;                                                             // else we should not be here
			int64_t cnt = rl.read_lnk(r,*rc) ;
			if (cnt>=0) {
				errno = 0 ;
				SWEAR_PROD( cnt<=ssize_t(rl.read_lnk.sz) , cnt,rl.read_lnk.sz ) ;
				if (+proc_mem)                                                                       // access to backdoor was emulated, we must transport result to actual user space
					try                     { _poke( proc_mem , rl.buf , rl.read_lnk.buf , cnt ) ; }
					catch (::string const&) { errno = ECOMM ; cnt = -1 ;                           } // distinguish between backdoor error and absence of support
				if (rl.read_lnk.buf) delete[] rl.read_lnk.buf ;                                      // buf has been allocated when processing magic
			}
			return cnt ;
		}
	) ;
}

// rename
struct RenameHelper {
	 Record::Rename rename ;
	 uint           flags  ;
} ;
template<bool At,int FlagArg> [[maybe_unused]] static ::pair<void* /*ctx*/,bool/*refresh*/> _entry_rename( Record& r , Fd proc_mem , uint64_t args[6] , bool /*emulate*/ , Comment c ) {
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
		RenameHelper& rn = *new RenameHelper{ .rename={r,_path<At>(proc_mem,args+0),_path<At>(proc_mem,args+1+At),exchange,no_replace,c} , .flags=FlagArg==FlagNever?0:uint(args[FlagArg]) } ;
		if (rn.rename.confirm_id) return { &rn , false/*refresh*/ } ;
		else                      delete &rn ;
	} catch (::string const&) {}
	return {} ;
}
[[maybe_unused]] static ::pair<int64_t/*rc*/,int/*errno*/> _exit_rename( void* ctx , Record& r , Fd , ::optional<int64_t> rc ) {
	return _do_exit<RenameHelper>( ctx , rc
	,	[ ](RenameHelper& rn)->int64_t {
		#if HAS_RENAMEAT2                                                                                                                                     // prefer official libc if available
			return ::renameat2(                   Fd::Cwd , rn.rename.src.real_write().c_str() , Fd::Cwd , rn.rename.dst.real_write().c_str() , rn.flags )  ;
		#elif defined(SYS_renameat2)                                                                                                                          // implement syscall if it exists
			return int(::syscall( SYS_renameat2 , Fd::Cwd , rn.rename.src.real_write().c_str() , Fd::Cwd , rn.rename.dst.real_write().c_str() , rn.flags )) ;
		#else                                                                                                                                                 // no call to renameat2
			return ::rename(                                rn.rename.src.real_write().c_str() ,           rn.rename.dst.real_write().c_str()            )  ;
		#endif
		}
	,	[&](RenameHelper& rn,int64_t rc) { rn.rename( r , rc ) ; }
	) ;
}

// symlink
struct SymlinkHelper {
	Record::Symlink lnk    ;
	uint64_t        target ;
} ;
template<bool At> [[maybe_unused]] static ::pair<void* /*ctx*/,bool/*refresh*/> _entry_symlink( Record& r , Fd proc_mem , uint64_t args[6] , bool /*emulate*/ , Comment c ) {
	try {
		SymlinkHelper& sl = *new SymlinkHelper{ .lnk={r,_path<At>(proc_mem,args+1),c} , .target=args[0] } ;
		if (sl.lnk.confirm_id) return { &sl , false/*/refresh*/ } ;
		else                   delete &sl ;                         // nothing to do on exit
	} catch (::string const&) {}
	return {} ;
}
[[maybe_unused]] static ::pair<int64_t/*rc*/,int/*errno*/> _exit_sym_lnk( void* ctx , Record& r , Fd proc_mem , ::optional<int64_t> rc ) {
	return _do_exit<SymlinkHelper>( ctx , rc
	,	[&](SymlinkHelper& sl           )->int64_t { return ::symlink( _get_str(proc_mem,sl.target).c_str() , sl.lnk.real_write().c_str() ) ; }
	,	[&](SymlinkHelper& sl,int64_t rc)          { sl.lnk( r , rc) ;                                                                        }
	) ;
}

// unlink
template<bool At,int FlagArg> [[maybe_unused]] static ::pair<void* /*ctx*/,bool/*refresh*/> _entry_unlink( Record& r , Fd proc_mem , uint64_t args[6] , bool /*emulate*/ , Comment c ) {
	try {
		bool rmdir = _flag<FlagArg>(args,AT_REMOVEDIR) ;
		if (rmdir)           Record::Unlnk( r , _path<At>(proc_mem,args+0) , rmdir , c ) ; // rmdir calls us without exit, and we must not set ctx in that case
		else {
			Record::Unlnk& u = *new Record::Unlnk{ r , _path<At>(proc_mem,args+0) , rmdir , c } ;
			if (u.confirm_id) return { &u , false/*refresh*/ } ;
			else              delete &u ;                                                  // nothing to do on exit
		}
	} catch (::string const&) {}
	return {} ;
}
[[maybe_unused]] static ::pair<int64_t/*rc*/,int/*errno*/> _exit_unlnk( void* ctx , Record& r , Fd , ::optional<int64_t> rc ) {
	return _do_exit<Record::Unlnk>( ctx , rc
	,	[ ](Record::Unlnk& u           )->int64_t { return ::unlink(u.real_write().c_str()) ; }
	,	[&](Record::Unlnk& u,int64_t rc)          { u( r , rc ) ;                             }
	) ;
}

// access
template<bool At,int FlagArg> [[maybe_unused]] static ::pair<void* /*ctx*/,bool/*refresh*/> _do_stat( Record& r , Fd proc_mem , uint64_t args[6] , Accesses a , Comment c ) {
	try {
		Record::Stat( r , _path<At>(proc_mem,args+0) , _flag<FlagArg>(args,AT_SYMLINK_NOFOLLOW) , a , c ) ;
	} catch (::string const&) {}
	return {} ;
}
template<bool At,int FlagArg> [[maybe_unused]] static ::pair<void* /*ctx*/,bool/*refresh*/> _entry_access( Record& r , Fd proc_mem , uint64_t args[6] , bool /*emulate*/ , Comment c ) {
	Accesses a ; if (args[At+1]&X_OK) a |= Access::Reg ;
	return _do_stat<At,FlagArg>(r,proc_mem,args,a,c) ;
}
template<bool At,int FlagArg> [[maybe_unused]] static ::pair<void* /*ctx*/,bool/*refresh*/> _entry_open_tree( Record& r , Fd proc_mem , uint64_t args[6] , bool /*emulate*/ , Comment c ) {
	return _do_stat<At,FlagArg>(r,proc_mem,args,Accesses(),c) ;
}
template<bool At,int FlagArg> [[maybe_unused]] static ::pair<void* /*ctx*/,bool/*refresh*/> _entry_stat( Record& r , Fd proc_mem , uint64_t args[6] , bool /*emulate*/ , Comment c ) {
	return _do_stat<At,FlagArg>(r,proc_mem,args,FullAccesses,c) ;
}

[[maybe_unused]] static ::pair<void* /*ctx*/,bool/*refresh*/> _entry_statx( Record& r , Fd proc_mem , uint64_t args[6] , bool /*emulate*/ , Comment c ) {
	#if defined(STATX_TYPE) && defined(STATX_SIZE) && defined(STATX_BLOCKS) && defined(STATX_MODE)
		uint     msk = args[3] ;
		Accesses a   ;
		if      (msk&(STATX_TYPE|STATX_SIZE|STATX_BLOCKS)) a = FullAccesses ; // user can distinguish all content
		else if (msk& STATX_MODE                         ) a = Access::Reg  ; // user can distinguish executable files, which is part of crc for regular files
	#else
		Accesses a = FullAccesses ;                                           // if access macros are not defined, be pessimistic
	#endif
	return _do_stat<true,2>(r,proc_mem,args,a,c) ;
}

template<bool Is32=false> static constexpr SyscallDescr::Tab _mk_syscall_descr_tab() {
	SyscallDescr::Tab tab = {} ;
	#if HAS_32
		#define SC_NUM(sc) Is32 ? MACRO_VAL(SYS32_##sc,-1L) : MACRO_VAL(SYS_##sc,-1L)
	#else
		#define SC_NUM(sc)                                    MACRO_VAL(SYS_##sc,-1L)
	#endif
	#define FILL_ENTRY( sc , ... ) {                                                         \
		constexpr long I = SC_NUM(sc) ;                                                      \
		if constexpr (I>=0) {                                                                \
			static_assert( I<SyscallDescr::NSyscalls   , "increase NSyscalls"            ) ; \
			static_assert( +(SyscallDescr __VA_ARGS__) , "entry appears as non-existent" ) ; \
			tab[I] = SyscallDescr __VA_ARGS__ ;                                              \
		}                                                                                    \
	}
	// entries marked filter (i.e. field is !=0) means that processing can be skipped if corresponding arg is a filename known to require no processing
	//                                entry                   <At   ,FlagArg   > , exit           filter return_fd  comment
	FILL_ENTRY( access            , { _entry_access           <false,FlagNever > , nullptr        ,  0  , false   , Comment::access            } ) ;
	FILL_ENTRY( faccessat         , { _entry_access           <true ,3         > , nullptr        ,  1  , false   , Comment::faccessat         } ) ;
	FILL_ENTRY( faccessat2        , { _entry_access           <true ,3         > , nullptr        ,  1  , false   , Comment::faccessat2        } ) ;
	FILL_ENTRY( chdir             , { _entry_chdir            <false           > , nullptr        , -1  , false   , Comment::chdir             } ) ;
	FILL_ENTRY( fchdir            , { _entry_chdir            <true            > , nullptr        , -1  , false   , Comment::fchdir            } ) ;
	FILL_ENTRY( chmod             , { _entry_chmod            <false,FlagNever > , _exit_chmod    ,  0  , false   , Comment::chmod             } ) ;
	FILL_ENTRY( fchmodat          , { _entry_chmod            <true ,3         > , _exit_chmod    ,  1  , false   , Comment::fchmodat          } ) ;
	FILL_ENTRY( chroot            , { _entry_chroot                              , _exit_chroot   , -1  , false   , Comment::chroot            } ) ;
	FILL_ENTRY( creat             , { _entry_creat                               , _exit_creat    ,  0  , true    , Comment::creat             } ) ;
	FILL_ENTRY( execve            , { _entry_execve           <false,FlagNever > , nullptr        , -1  , false   , Comment::execve            } ) ;
	FILL_ENTRY( execveat          , { _entry_execve           <true ,4         > , nullptr        , -1  , false   , Comment::execveat          } ) ;
	FILL_ENTRY( getdents          , { _entry_getdents                            , _exit_getdents , -1  , false   , Comment::getdents          } ) ;
	FILL_ENTRY( getdents64        , { _entry_getdents                            , _exit_getdents , -1  , false   , Comment::getdents64        } ) ;
	FILL_ENTRY( link              , { _entry_lnk              <false,FlagNever > , _exit_lnk      ,  1  , false   , Comment::link              } ) ;
	FILL_ENTRY( linkat            , { _entry_lnk              <true ,4         > , _exit_lnk      ,  3  , false   , Comment::linkat            } ) ;
	FILL_ENTRY( mkdir             , { _entry_mkdir            <false           > , nullptr        ,  0  , false   , Comment::mkdir             } ) ;
	FILL_ENTRY( mkdirat           , { _entry_mkdir            <true            > , nullptr        ,  1  , false   , Comment::mkdirat           } ) ;
	FILL_ENTRY( mount             , { _entry_mount                               , _exit_mount    , -1  , false   , Comment::mount             } ) ;
	FILL_ENTRY( name_to_handle_at , { _entry_name_to_handle_at                   , nullptr        ,  1  , false   , Comment::name_to_handle_at } ) ;
	FILL_ENTRY( open              , { _entry_open             <false           > , _exit_open     ,  0  , true    , Comment::open              } ) ;
	FILL_ENTRY( openat            , { _entry_open             <true            > , _exit_open     ,  1  , true    , Comment::openat            } ) ;
	FILL_ENTRY( open_tree         , { _entry_open_tree        <true ,2         > , nullptr        ,  1  , false   , Comment::open_tree         } ) ;
	FILL_ENTRY( readdir           , { _entry_getdents                            , _exit_getdents , -1  , false   , Comment::readdir           } ) ;
	FILL_ENTRY( readlink          , { _entry_read_lnk         <false           > , _exit_read_lnk ,  0  , false   , Comment::readlink          } ) ;
	FILL_ENTRY( readlinkat        , { _entry_read_lnk         <true            > , _exit_read_lnk ,  1  , false   , Comment::readlinkat        } ) ;
	FILL_ENTRY( rename            , { _entry_rename           <false,FlagNever > , _exit_rename   ,  1  , false   , Comment::rename            } ) ;
	FILL_ENTRY( renameat          , { _entry_rename           <true ,FlagNever > , _exit_rename   ,  3  , false   , Comment::renameat          } ) ;
	FILL_ENTRY( renameat2         , { _entry_rename           <true ,4         > , _exit_rename   ,  3  , false   , Comment::renameat2         } ) ;
	FILL_ENTRY( rmdir             , { _entry_unlink           <false,FlagAlways> , nullptr        ,  0  , false   , Comment::rmdir             } ) ;
	FILL_ENTRY( stat              , { _entry_stat             <false,FlagNever > , nullptr        ,  0  , false   , Comment::stat              } ) ;
	FILL_ENTRY( stat64            , { _entry_stat             <false,FlagNever > , nullptr        ,  0  , false   , Comment::stat64            } ) ;
	FILL_ENTRY( fstatat64         , { _entry_stat             <true ,3         > , nullptr        ,  1  , false   , Comment::fstatat64         } ) ;
	FILL_ENTRY( lstat             , { _entry_stat             <false,FlagAlways> , nullptr        ,  0  , false   , Comment::lstat             } ) ;
	FILL_ENTRY( lstat64           , { _entry_stat             <false,FlagAlways> , nullptr        ,  0  , false   , Comment::lstat64           } ) ;
	FILL_ENTRY( statx             , { _entry_statx                               , nullptr        ,  1  , false   , Comment::statx             } ) ;
	FILL_ENTRY( newfstatat        , { _entry_stat             <true ,3         > , nullptr        ,  1  , false   , Comment::newfstatat        } ) ;
	FILL_ENTRY( oldstat           , { _entry_stat             <false,FlagNever > , nullptr        ,  0  , false   , Comment::oldstat           } ) ;
	FILL_ENTRY( oldlstat          , { _entry_stat             <false,FlagAlways> , nullptr        ,  0  , false   , Comment::oldlstat          } ) ;
	FILL_ENTRY( symlink           , { _entry_symlink          <false           > , _exit_sym_lnk  ,  1  , false   , Comment::symlink           } ) ;
	FILL_ENTRY( symlinkat         , { _entry_symlink          <true            > , _exit_sym_lnk  ,  2  , false   , Comment::symlinkat         } ) ;
	FILL_ENTRY( unlink            , { _entry_unlink           <false,FlagNever > , _exit_unlnk    ,  0  , false   , Comment::unlink            } ) ;
	FILL_ENTRY( unlinkat          , { _entry_unlink           <true ,2         > , _exit_unlnk    ,  1  , false   , Comment::unlinkat          } ) ;
	#ifdef SYS_openat2
		FILL_ENTRY( openat2 , { _entry_open2 , _exit_open2 ,  1/*filter*/ , true/*return_fd*/ , Comment::openat2 } ) ;
	#endif
	#undef FILL_ENTRY
	return tab ;
}

//                                                                                          Is32
/**/       constexpr SyscallDescr::Tab        SyscallDescrTab       = _mk_syscall_descr_tab<false>() ;
IF_HAS_32( constexpr SyscallDescr::Tab        SyscallDescrTab32     = _mk_syscall_descr_tab<true >() ; )
/**/                 SyscallDescr::Tab const& SyscallDescr::s_tab   = SyscallDescrTab                ;
IF_HAS_32(           SyscallDescr::Tab const& SyscallDescr::s_tab32 = SyscallDescrTab32              ; )

// generate BPF filter that does a dicothomy search rather than linear for improved perf
// generate BPF filter at compile time for improved perf

using BpfInstr = struct ::sock_filter ;

template<uint8_t N> static constexpr uint8_t NBpfInstrs    = 1 + NBpfInstrs<N/2> + NBpfInstrs<N-N/2> ; // dichotomy : intial test + branches
template<         >        constexpr uint8_t NBpfInstrs<1> = 1                                       ; // linear search
template<         >        constexpr uint8_t NBpfInstrs<2> = 2                                       ; // .
template<         >        constexpr uint8_t NBpfInstrs<3> = 3                                       ; // .

template<uint8_t N> static constexpr ::array<BpfInstr,NBpfInstrs<N>> _mk_bpf_filter_tree( uint idx , uint8_t n_after_allow , uint8_t n_after_trace ) {
	::array<BpfInstr,NBpfInstrs<N>> res ;
	if constexpr (N<=3) {
		static_assert(N==res.size()) ;
		SWEAR( N-1+n_after_trace<=Max<uint8_t> , N,n_after_trace ) ;
		for( uint8_t i : iota(N-1) ) res[i  ] = { .code=BPF_JMP|BPF_JEQ|BPF_K , .jt=uint8_t(N-1-i+n_after_trace) , .jf=0             , .k=idx+i   } ;
		/**/                         res[N-1] = { .code=BPF_JMP|BPF_JEQ|BPF_K , .jt=              n_after_trace  , .jf=n_after_allow , .k=idx+N-1 } ;
	} else {
		constexpr uint8_t N2  = N/2   ;
		constexpr uint8_t N21 = N-N/2 ;
		SWEAR( NBpfInstrs<N2><=Max<uint8_t> , N ) ;
		res[0] = { .code=BPF_JMP|BPF_JGE|BPF_K , .jt=NBpfInstrs<N2> , .jf=0 , .k=idx+N2 } ;
		::copy( _mk_bpf_filter_tree<N2 >(idx   ,n_after_allow+NBpfInstrs<N21>,n_after_trace+NBpfInstrs<N21>) , res.begin()+1                ) ;
		::copy( _mk_bpf_filter_tree<N21>(idx+N2,n_after_allow                ,n_after_trace                ) , res.begin()+1+NBpfInstrs<N2> ) ;
	}
	return res ;
}

/**/       static constexpr uint NBpfSyscalls   = []() { uint n = 0 ; for( SyscallDescr const& e : SyscallDescrTab   ) if (+e) n++ ; return n ; }() ;
IF_HAS_32( static constexpr uint NBpfSyscalls32 = []() { uint n = 0 ; for( SyscallDescr const& e : SyscallDescrTab32 ) if (+e) n++ ; return n ; }() ; )

constexpr uint8_t BpfFilterSz =
	IF_HAS_32( +2                            ) // load+check arch
	/**/       +1+NBpfInstrs<NBpfSyscalls  >   // load nr + filter
	IF_HAS_32( +1+NBpfInstrs<NBpfSyscalls32> ) // load nr + filter 32-bit
	/**/       +2                              // return
;
template<uint32_t SeccompRetCatch> static constexpr ::array<BpfInstr,BpfFilterSz> mk_bpf_filter() {
	::array<BpfInstr,BpfFilterSz> res ;
	uint                          i   = 0 ;
	// load+check arch
	#if HAS_32
		res[i++] = { .code=BPF_LD |BPF_W|BPF_ABS , .jt=0 , .jf=0                                   , .k=offsetof(struct ::seccomp_data,arch) } ;
		res[i++] = { .code=BPF_JMP|BPF_JEQ|BPF_K , .jt=0 , .jf=uint8_t(1+NBpfInstrs<NBpfSyscalls>) , .k=NonPortable::Arch                    } ;
		uint8_t N32 = 1+NBpfInstrs<NBpfSyscalls32> ;
	#else
		uint8_t N32 = 0                            ;
	#endif
	//
	// load nr + filter
	uint8_t start = i ;
	res[i++] = { .code=BPF_LD |BPF_W|BPF_ABS , .jt=0 , .jf=0 , .k=offsetof(struct ::seccomp_data,nr) } ;
	::copy( _mk_bpf_filter_tree<NBpfSyscalls>(0,N32,N32+1) , res.begin()+i ) ; i += NBpfInstrs<NBpfSyscalls> ;
	//
	// load nr + filter 32-bit
	#if HAS_32
		uint8_t start32 = i ;
		res[i++] = { .code=BPF_LD |BPF_W|BPF_ABS , .jt=0 , .jf=0 , .k=offsetof(struct ::seccomp_data,nr) } ;
		::copy( _mk_bpf_filter_tree<NBpfSyscalls32>(0,0,1) , res.begin()+i ) ; i += NBpfInstrs<NBpfSyscalls32> ;
	#endif
	//
	// return
	res[i++] = { .code=BPF_RET|BPF_K , .jt=0 , .jf=0 , .k=SECCOMP_RET_ALLOW } ;
	res[i++] = { .code=BPF_RET|BPF_K , .jt=0 , .jf=0 , .k=SeccompRetCatch   } ;
	//
	SWEAR( i==BpfFilterSz , i,BpfFilterSz ) ;
	//
	/**/       ::array<uint,NBpfSyscalls  > syscalls   ;
	IF_HAS_32( ::array<uint,NBpfSyscalls32> syscalls32 ; )
	/**/       i = 0 ; for( uint sc : iota(SyscallDescrTab  .size()) ) if (+SyscallDescrTab  [sc]) syscalls  [i++] = sc ; SWEAR( i==NBpfSyscalls   , i,NBpfSyscalls   ) ;
	IF_HAS_32( i = 0 ; for( uint sc : iota(SyscallDescrTab32.size()) ) if (+SyscallDescrTab32[sc]) syscalls32[i++] = sc ; SWEAR( i==NBpfSyscalls32 , i,NBpfSyscalls32 ) ; )
	/**/       ::sort(syscalls  ) ;
	IF_HAS_32( ::sort(syscalls32) ; )
	//
	for( BpfInstr& instr : res ) {
		if (&instr<&res[start]) continue ;
		switch (instr.code) {
			case BPF_JMP|BPF_JEQ|BPF_K :
			case BPF_JMP|BPF_JGE|BPF_K :
			case BPF_JMP|BPF_JGT|BPF_K :
				#if HAS_32
					instr.k = &instr<&res[start32] ? syscalls[instr.k] : syscalls32[instr.k] ;
				#else
					instr.k =                        syscalls[instr.k]                       ;
				#endif
			break ;
		DN}
	}
	//
	return res ;
}
template<uint32_t SeccompRetCatch> static constexpr ::array<BpfInstr,BpfFilterSz> BpfFilter = mk_bpf_filter<SeccompRetCatch>() ;

template<uint32_t SeccompRetCatch> static constexpr SyscallDescr::BpfProg SyscallDescrBpfProg = {
	.len    = ushort(BpfFilterSz)
,	.filter = const_cast<BpfInstr*>(&BpfFilter<SeccompRetCatch>[0])
} ;

/**/                    SyscallDescr::BpfProg const& SyscallDescr::s_bpf_prog_ptrace  = SyscallDescrBpfProg<SECCOMP_RET_TRACE     > ;
IF_CAN_AUTODEP_SECCOMP( SyscallDescr::BpfProg const& SyscallDescr::s_bpf_prog_seccomp = SyscallDescrBpfProg<SECCOMP_RET_USER_NOTIF> ; )
