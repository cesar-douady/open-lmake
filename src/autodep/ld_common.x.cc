// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <dlfcn.h>     // dlopen, dlinfo
#include <errno.h>
#include <sched.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <syscall.h>   // for SYS_* macros
#include <sys/mount.h>
#include <sys/time.h>
#include <unistd.h>
#include <utime.h>

#include "disk.hh"

#include "gather.hh"
#include "record.hh"
#include "syscall_tab.hh"

// compiled with flag -fvisibility=hidden : this is good for perf and with LD_PRELOAD, we do not polute application namespace

using namespace Disk ;

extern "C" {
	// the following functions are defined in libc not in #include's, so they may be called by application code
	extern int     __close          ( int fd                                                        )          ;
	extern int     __dup2           ( int oldfd , int newfd                                         ) noexcept ;
	extern pid_t   __fork           (                                                               ) noexcept ;
	extern pid_t   __libc_fork      (                                                               ) noexcept ;
	extern int     __open           (             const char* pth , int flgs , ...                  )          ;
	extern int     __open_nocancel  (             const char* pth , int flgs , ...                  )          ;
	extern int     __open_2         (             const char* pth , int flgs                        )          ;
	extern int     __open64         (             const char* pth , int flgs , ...                  )          ;
	extern int     __open64_nocancel(             const char* pth , int flgs , ...                  )          ;
	extern int     __open64_2       (             const char* pth , int flgs                        )          ;
	extern int     __openat_2       ( int dfd   , const char* pth , int flgs                        )          ;
	extern int     __openat64_2     ( int dfd   , const char* pth , int flgs                        )          ;
	extern ssize_t __readlink_chk   (             const char* pth , char*   , size_t l , size_t bsz ) noexcept ;
	extern ssize_t __readlinkat_chk ( int dfd   , const char* pth , char* b , size_t l , size_t bsz ) noexcept ;
	extern char*   __realpath_chk   (             const char* pth , char* rpth , size_t rlen        ) noexcept ;
	//
	extern int __xstat     ( int v ,           const char* pth , struct stat  * buf            ) noexcept ;
	extern int __xstat64   ( int v ,           const char* pth , struct stat64* buf            ) noexcept ;
	extern int __lxstat    ( int v ,           const char* pth , struct stat  * buf            ) noexcept ;
	extern int __lxstat64  ( int v ,           const char* pth , struct stat64* buf            ) noexcept ;
	extern int __fxstatat  ( int v , int dfd , const char* pth , struct stat  * buf , int flgs ) noexcept ;
	extern int __fxstatat64( int v , int dfd , const char* pth , struct stat64* buf , int flgs ) noexcept ;
	// following libcalls may not be defined on all systems (but it does not hurt to redeclare them if they are already declared)
	int         __clone2   ( int (*fn)(void *) , void *stack_base , size_t stack_size , int flags , void *arg , ... )          ;
	extern int  close_range( uint fd1 , uint fd2 , int flgs                                                         ) noexcept ;
	extern void closefrom  ( int  fd1                                                                               ) noexcept ;
	extern int  execveat   ( int dirfd , const char* pth , char* const argv[] , char *const envp[] , int flgs       ) noexcept ;
	extern int  faccessat2 ( int dirfd , const char* pth , int mod , int flgs                                       ) noexcept ;
	extern int  renameat2  ( int odfd  , const char* op  , int ndfd , const char* np , uint flgs                    ) noexcept ;
	extern int  statx      ( int dirfd , const char* pth , int flgs , uint msk , struct statx* buf                  ) noexcept ;
	#if MAP_VFORK
		extern pid_t __vfork() noexcept ;
	#endif
}

static              Mutex<MutexLvl::Autodep2> _g_mutex ;         // ensure exclusivity between threads
static thread_local bool                      _t_loop  = false ; // prevent recursion within a thread

// User program may have global variables whose cxtor/dxtor do accesses.
// In that case, they may come before our own Audit is constructed if declared global (in the case of LD_PRELOAD).
// To face this order problem, we declare our Audit as a static within a funciton which will be constructed upon first call.
// As all statics with cxtor/dxtor, we define it through new so as to avoid destruction during finalization.
#ifndef IN_SERVER
	static                                // in server, we want to have direct access to recorder (no risk of name pollution as we masterize the code)
#endif
Record& auditor() {
	static Record* s_res = nullptr ;
	if (!s_res) s_res = new Record{New} ; // dont initialize directly as C++ guard for static variables may do some syscalls
	return *s_res ;
}

template<class Action,int NP=1> struct AuditAction : Ctx,Action {
	// cxtors & casts
	// errno must be protected from our auditing actions in cxtor and operator()
	// more specifically, errno must be the original one before the actual call to libc
	// and must be the one after the actual call to libc when auditing code finally leave
	// Ctx contains save_errno in its cxtor and restore_errno in its dxtor
	// so here, errno must be restored at the end of cxtor and saved at the beginning of operator()
	template<class... A> AuditAction(                                    A&&... args) requires(NP==0) : Action{auditor(),                      ::forward<A>(args)... } { restore_errno() ; }
	template<class... A> AuditAction(char*          p  ,                 A&&... args) requires(NP==1) : Action{auditor(),Record::WPath(p)     ,::forward<A>(args)... } { restore_errno() ; }
	template<class... A> AuditAction(Record::Path&& p  ,                 A&&... args) requires(NP==1) : Action{auditor(),::move(p )           ,::forward<A>(args)... } { restore_errno() ; }
	template<class... A> AuditAction(Record::Path&& p1,Record::Path&& p2,A&&... args) requires(NP==2) : Action{auditor(),::move(p1),::move(p2),::forward<A>(args)... } { restore_errno() ; }
	// services
	template<class T> T operator()(T res) { save_errno() ; return Action::operator()(auditor(),res) ; }
} ;
//                                           n paths
using Chdir    = AuditAction<Record::Chdir         > ;
using Chmod    = AuditAction<Record::Chmod         > ;
using Hide     = AuditAction<Record::Hide    ,0    > ;
using Mkdir    = AuditAction<Record::Mkdir         > ;
using Lnk      = AuditAction<Record::Lnk     ,2    > ;
using Mount    = AuditAction<Record::Mount   ,2    > ;
using Open     = AuditAction<Record::Open          > ;
using Read     = AuditAction<Record::Read          > ;
using ReadCS   = AuditAction<Record::ReadCS        > ;
using Readlink = AuditAction<Record::Readlink      > ;
using Rename   = AuditAction<Record::Rename  ,2    > ;
using Solve    = AuditAction<Record::Solve         > ;
using SolveCS  = AuditAction<Record::SolveCS       > ;
using Stat     = AuditAction<Record::Stat          > ;
using Symlink  = AuditAction<Record::Symlink       > ;
using Unlnk    = AuditAction<Record::Unlnk         > ;
using WSolve   = AuditAction<Record::WSolve        > ;

#ifdef LD_PRELOAD

	//
	// Dlopen
	//

	struct _Dlopen : Record::ReadCS {
		using Base = Record::ReadCS ;
		// cxtors & casts
		_Dlopen() = default ;
		_Dlopen( Record& r , const char* file , ::string&& comment ) : Base{search_elf(r,file,::move(comment))} {}
		// services
	} ;
	using Dlopen = AuditAction<_Dlopen,0/*NP*/> ;

#endif

//
// Exec
//

struct _Exec : Record::Exec {
	using Base = Record::Exec ;
	//
	_Exec() = default ;
	_Exec( Record& r , Record::Path&& path , bool no_follow , const char* const envp[] , ::string&& comment ) : Base{r,::move(path),no_follow,::copy(comment)} {
		static constexpr char   Llpe[] = "LD_LIBRARY_PATH=" ;
		static constexpr size_t LlpeSz = sizeof(Llpe)-1     ;                          // -1 to account of terminating null
		//
		const char* const* llp = nullptr/*garbage*/ ;
		for( llp=envp ; *llp ; llp++ ) if (strncmp( *llp , Llpe , LlpeSz )==0) break ;
		if (*llp) elf_deps( r , *this , *llp+LlpeSz , comment+".dep" ) ;               // pass value after the LD_LIBRARY_PATH= prefix
		else      elf_deps( r , *this , nullptr     , comment+".dep" ) ;               // /!\ dont add LlpeSz to nullptr
	}
} ;
using Exec = AuditAction<_Exec> ;

struct _Execp : _Exec {
	using Base = _Exec ;
	// search executable file in PATH
	_Execp() = default ;
	_Execp( Record& r , const char* file , bool /*no_follow*/ , const char* const envp[] , ::string&& comment ) {
		if (!file) return ;
		//
		if (::strchr(file,'/')) {                                                              // if file contains a /, no search is performed
			static_cast<Base&>(*this) = Base(r,file,false/*no_follow*/,envp,::move(comment)) ;
			return ;
		}
		//
		::string p = get_env("PATH") ;
		if (!p) {                                                                              // gather standard path if path not provided
			size_t n = ::confstr(_CS_PATH,nullptr,0) ;
			p.resize(n) ;
			::confstr(_CS_PATH,p.data(),n) ;
			SWEAR(p.back()==0) ;
			p.pop_back() ;
		}
		//
		for( size_t pos=0 ;;) {
			size_t   end       = p.find(':',pos)                         ;
			size_t   len       = (end==Npos?p.size():end)-pos            ;
			::string full_file = len ? p.substr(pos,len)+'/'+file : file ;
			Record::Read(r,full_file,false/*no_follow*/,true/*keep_real*/,::copy(comment)) ;
			if (is_exe(full_file,false/*no_follow*/)) {
				static_cast<Base&>(*this) = Base(r,full_file,false/*no_follow*/,envp,::move(comment)) ;
				return ;
			}
			if (end==Npos) return ;
			pos = end+1 ;
		}
	}
} ;
using Execp = AuditAction<_Execp,0/*NP*/> ;

//
// Fopen
//

struct Fopen : AuditAction<Record::Open> {
	using Base = AuditAction<Record::Open> ;
	static int mk_flags(const char* mode) {
		bool r = false ;
		bool w = false ;
		bool a = false ;
		bool p = false ;
		for( const char* m=mode ; *m && *m!=',' ; m++ )                                                // after a ',', there is a css=xxx which we do not care about
			switch (*m) {
				case 'r' : r = true ; break ;
				case 'w' : w = true ; break ;
				case 'a' : a = true ; break ;
				case '+' : p = true ; break ;
				case 'c' :            return O_PATH ;                                                  // gnu extension, no access
				default : ;
			}
		if (a+r+w!=1) return O_PATH ;                                                                  // error case   , no access
		int flags = p ? O_RDWR : r ? O_RDONLY : O_WRONLY ;
		if (!r) flags |= O_CREAT  ;
		if (w ) flags |= O_TRUNC  ;
		if (a ) flags |= O_APPEND ;
		return flags ;
	}
	Fopen( Record::Path&& pth , const char* mode , ::string const& comment ) : Base{ ::move(pth) , mk_flags(mode) , comment+'.'+mode } {}
	FILE* operator()(FILE* fp) {
		Base::operator()(fp?::fileno(fp):-1) ;
		return fp ;
	}
} ;

//
// Mkstemp
//

struct Mkstemp : WSolve {
	using Base = AuditAction<Record::WSolve> ;
	Mkstemp( char* t , int sl , ::string&& comment_ ) : Base{ t , true/*no_follow*/ , false/*read*/ , true/*create*/ , comment_ } , tmpl{t} , sfx_len{sl} , comment{::move(comment_)} {}
	Mkstemp( char* t ,          ::string&& comment_ ) : Mkstemp(t,0,::move(comment_)) {}
	int operator()(int fd) {
		// in case of success, tmpl is modified to contain the file that was actually opened, and it was called with file instead of tmpl
		if (file!=tmpl) ::memcpy( tmpl+strlen(tmpl)-sfx_len-6 , file+strlen(file)-sfx_len-6 , 6 ) ;
		if (fd>=0     ) Record::Open(auditor(),file,O_CREAT|O_WRONLY|O_TRUNC|O_NOFOLLOW,::move(comment))(auditor(),fd) ;
		return Base::operator()(fd) ;
	}
	// data
	char*    tmpl    = nullptr/*garbage*/ ;
	int      sfx_len = 0      /*garbage*/ ;
	::string comment ;
} ;

//
// Audited
//

#ifdef LD_PRELOAD
	// for ld_preload, we want to hide libc functions so as to substitute the auditing functions to the regular functions
	#pragma GCC visibility push(default) // force visibility of functions defined hereinafter, until the corresponding pop
	extern "C"
#endif
#ifdef LD_AUDIT
	// for ld_audit, we want to use private functions so auditing code can freely call the libc without having to deal with errno
	namespace Audited
#endif
{
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wignored-attributes"

	#define NE           noexcept
	#define ASLNF(flags) bool((flags)&AT_SYMLINK_NOFOLLOW)
	#define EXE(  mode ) bool((mode )&S_IXUSR            )

	// cwd is implicitly accessed by mostly all libcalls, so we have to ensure mutual exclusion as cwd could change between actual access and path resolution in audit
	// hence we should use a shared lock when reading and an exclusive lock when chdir
	// however, we have to ensure exclusivity for lnk cache, so we end up to exclusive access anyway, so simpler to lock exclusively here
	// no malloc must be performed before cond is checked to allow jemalloc accesses to be filtered, hence auditor() (which allocates a Record) is done after
	// use short macros as lines are very long in defining audited calls to libc
	// protect against recusive calls
	// args must be in () e.g. HEADER1(unlink,path,(path))
	#define ORIG(libcall) \
		using Libcall = decltype(::libcall)* ;                                                            \
		static ::atomic<Libcall> atomic_orig = nullptr ;                                                  \
		if (!atomic_orig) {                                                                               \
			Libcall prev = nullptr ;                                                                      \
			atomic_orig.compare_exchange_strong( prev , reinterpret_cast<Libcall>(get_orig(#libcall)) ) ; \
		}
	#define HEADER(libcall,is_stat,cond,args) \
		ORIG(libcall) ;                                                                                \
		if ( _t_loop || !started()                                      ) return (*atomic_orig) args ; \
		Save sav{_t_loop,true} ;                                                                       \
		if ( cond                                                       ) return (*atomic_orig) args ; \
		Lock lock { _g_mutex } ;                                                                       \
		if ( is_stat && (auditor(),Record::s_autodep_env().ignore_stat) ) return (*atomic_orig) args ; \
		Libcall orig = atomic_orig
	// do a first check to see if it is obvious that nothing needs to be done
	#define HEADER0(libcall,is_stat,            args) HEADER( libcall , is_stat , false                                                    , args )
	#define HEADER1(libcall,is_stat,path,       args) HEADER( libcall , is_stat , Record::s_is_simple(path )                               , args )
	#define HEADER2(libcall,is_stat,path1,path2,args) HEADER( libcall , is_stat , Record::s_is_simple(path1) && Record::s_is_simple(path2) , args )
	// macro for libcall that are forbidden in server when recording deps
	#ifdef IN_SERVER
		#define NO_SERVER(libcall) \
			*Record::s_deps_err += #libcall " is forbidden in server\n" ; \
			errno = ENOSYS ;                                              \
			return -1
	#else
		#define NO_SERVER(libcall)
	#endif

	#define CC const char

	// chdir
	// chdir must be tracked as we must tell Record of the new cwd
	// /!\ chdir manipulates cwd, which mandates an exclusive lock
	//                                    is_stat
	int chdir (CC* p ) NE { HEADER0(chdir ,false,(p )) ; NO_SERVER(chdir ) ; Chdir r{p     ,"chdir" } ; return r(orig(p )) ; }
	int fchdir(int fd) NE { HEADER0(fchdir,false,(fd)) ; NO_SERVER(fchdir) ; Chdir r{Fd(fd),"fchdir"} ; return r(orig(fd)) ; }

	// chmod
	// although file is not modified, resulting file after chmod depends on its previous content, much like a copy

	//                                                            is_stat                             exe   no_follow
	int chmod   (      CC* p,mode_t m      ) NE { HEADER1(chmod   ,false,p,(  p,m  )) ; Chmod r{   p ,EXE(m),false   ,"chmod"   } ; return r(orig(  p,m  )) ; }
	int fchmodat(int d,CC* p,mode_t m,int f) NE { HEADER1(fchmodat,false,p,(d,p,m,f)) ; Chmod r{{d,p},EXE(m),ASLNF(f),"fchmodat"} ; return r(orig(d,p,m,f)) ; }

	// clone
	// cf fork about why this wrapper is necessary
	static int (*_clone_fn)(void*) ;       // variable to hold actual function to call
	static int _call_clone_fn(void* arg) {
		_t_loop = false ;
		_g_mutex.unlock(MutexLvl::None) ;  // contrarily to fork, clone does not proceed but calls a function and we must release the lock both in parent and child (and we are the only thread here)
		return _clone_fn(arg) ;
	}
	int clone( int (*fn)(void*) , void *stack , int flags , void *arg , ... ) NE {
		va_list args ;
		va_start(args,arg) ;
		pid_t* parent_tid = va_arg(args,pid_t*) ;
		void * tls        = va_arg(args,void *) ;
		pid_t* child_tid  = va_arg(args,pid_t*) ;
		va_end(args) ;
		//
		ORIG(clone) ;
		if ( _t_loop || !started() || flags&CLONE_VM ) return (*atomic_orig)(fn,stack,flags,arg,parent_tid,tls,child_tid) ; // if flags contains CLONE_VM, lock is not duplicated : nothing to do
		NO_SERVER(clone) ;
		Lock lock{_g_mutex} ;                                                                                               // no need to set _t_loop as clone calls no other piggy-backed function
		_clone_fn = fn ;                                                                                                    // _g_mutex is held, so there is no risk of clash
		return (*atomic_orig)(_call_clone_fn,stack,flags,arg,parent_tid,tls,child_tid) ;
	}
	int __clone2( int (*fn)(void*) , void *stack , size_t stack_size , int flags , void *arg , ... ) {
		va_list args ;
		va_start(args,arg) ;
		pid_t* parent_tid = va_arg(args,pid_t*) ;
		void * tls        = va_arg(args,void *) ;
		pid_t* child_tid  = va_arg(args,pid_t*) ;
		va_end(args) ;
		//
		ORIG(__clone2) ;
		if ( _t_loop || !started() || flags&CLONE_VM ) return (*atomic_orig)(fn,stack,stack_size,flags,arg,parent_tid,tls,child_tid) ; // cf clone
		Lock lock{_g_mutex} ;                                                                                                          // cf clone
		//
		NO_SERVER(__clone2) ;
		_clone_fn = fn ;                                                                                                               // cf clone
		return (*atomic_orig)(_call_clone_fn,stack,stack_size,flags,arg,parent_tid,tls,child_tid) ;
	}

	#ifndef IN_SERVER
		// close
		// close must be tracked as we must call hide
		// in case close is called with one our our fd's, we must hide somewhere else (unless in server)
		// note that although hide calls no syscall, auditor() can and we must manage errno
		//                                     is_stat
		int  close  (int fd ) { HEADER0(close  ,false,(fd)) ; Hide r{fd} ; return r(orig(fd)) ; }
		int  __close(int fd ) { HEADER0(__close,false,(fd)) ; Hide r{fd} ; return r(orig(fd)) ; }
		#if HAS_CLOSE_RANGE
			int  close_range(uint fd1,uint fd2,int f) NE { HEADER0(close_range,false/*is_stat*/,(fd1,fd2,f)) ; Hide r{fd1,fd2,f} ; return r(orig(fd1,fd2,f)) ; }
		#endif
	#endif

	#ifdef LD_PRELOAD
		// dlopen
		// not necessary with ld_audit as auditing mechanism provides a reliable way of finding indirect deps
		//                                                      is_stat
		void* dlopen (          CC* p,int f) NE { HEADER(dlopen ,false,!p||!*p,(   p,f)) ; Dlopen r{p,"dlopen" } ; return r(orig(   p,f)) ; } // we do not support tmp mapping for indirect ...
		void* dlmopen(Lmid_t lm,CC* p,int f) NE { HEADER(dlmopen,false,!p||!*p,(lm,p,f)) ; Dlopen r{p,"dlmopen"} ; return r(orig(lm,p,f)) ; } // ... deps, so we can pass pth to orig
	#endif

	#ifndef IN_SERVER
		// dup2
		// in case dup2/3 is called with one our fd's, we must hide somewhere else (unless in server)
		//                                                   is_stat
		int dup2  (int ofd,int nfd      ) NE { HEADER0(dup2  ,false,(ofd,nfd  )) ; Hide r{nfd} ; return r(orig(ofd,nfd  )) ; }
		int dup3  (int ofd,int nfd,int f) NE { HEADER0(dup3  ,false,(ofd,nfd,f)) ; Hide r{nfd} ; return r(orig(ofd,nfd,f)) ; }
		int __dup2(int ofd,int nfd      ) NE { HEADER0(__dup2,false,(ofd,nfd  )) ; Hide r{nfd} ; return r(orig(ofd,nfd  )) ; }
	#endif

	#ifdef LD_PRELOAD
		// env
		// only there to capture LD_LIBRARY_PATH before it is modified as man dlopen says it must be captured at program start, but we have no entry at program start
		// ld_audit does not need it and anyway captures LD_LIBRARY_PATH at startup
		int setenv  (const char *name , const char *value , int overwrite) { ORIG(setenv  ) ; get_ld_library_path() ; return (*atomic_orig)(name,value,overwrite) ; }
		int unsetenv(const char *name                                    ) { ORIG(unsetenv) ; get_ld_library_path() ; return (*atomic_orig)(name                ) ; }
		int putenv  (char *string                                        ) { ORIG(putenv  ) ; get_ld_library_path() ; return (*atomic_orig)(string              ) ; }
	#endif

	// execv
	// /!\ : exec* can be called from within a vfork
	// So we must ensure that the child fully clean locks and other protections before actually calling exec as we cannot clean up after the call (it usually does not return)
	// and its memory is shared with parent in that case.
	// In counterpart, exec* calls do not themselves call other libc functions, so we need no protection while they run.
	// execv*p cannot be simple as we do not know which file will be accessed
	#define HEADER_EXEC(Exec,libcall,no_follow,path,envp) \
		ORIG(libcall) ;                                  \
		Libcall orig = atomic_orig ;                     \
		SWEAR(!_t_loop) ;                                \
		if (started()) {                                 \
			NO_SERVER(libcall) ;                         \
			Save sav  { _t_loop , true } ;               \
			Lock lock { _g_mutex       } ;               \
			Exec( path , no_follow , envp , #libcall ) ; \
		}
	//                                                                                                                 no_follow
	int execv   (         CC* p , char* const argv[]                                 ) NE { HEADER_EXEC(Exec ,execv   ,false      ,               p ,environ) ; return orig(  p,argv          ) ; }
	int execve  (         CC* p , char* const argv[] , char* const envp[]            ) NE { HEADER_EXEC(Exec ,execve  ,false      ,               p ,envp   ) ; return orig(  p,argv,envp     ) ; }
	int execvp  (         CC* p , char* const argv[]                                 ) NE { HEADER_EXEC(Execp,execvp  ,false      ,               p ,environ) ; return orig(  p,argv          ) ; }
	int execvpe (         CC* p , char* const argv[] , char* const envp[]            ) NE { HEADER_EXEC(Execp,execvpe ,false      ,               p ,envp   ) ; return orig(  p,argv,envp     ) ; }
	int execveat( int d , CC* p , char* const argv[] , char *const envp[] , int flgs ) NE { HEADER_EXEC(Exec ,execveat,ASLNF(flgs),Record::Path(d,p),envp   ) ; return orig(d,p,argv,envp,flgs) ; }
	// execl
	#define MK_ARGS(end_action,value) \
		char*   cur         = const_cast<char*>(arg)            ;            \
		va_list arg_cnt_lst ; va_start(arg_cnt_lst,arg        ) ;            \
		va_list args_lst    ; va_copy (args_lst   ,arg_cnt_lst) ;            \
		int     arg_cnt     ;                                                \
		for( arg_cnt=0 ; cur ; arg_cnt++ ) cur = va_arg(arg_cnt_lst,char*) ; \
		char** args = new char*[arg_cnt+1] ;                                 \
		args[0] = const_cast<char*>(arg) ;                                   \
		for( int i=1 ; i<=arg_cnt ; i++ ) args[i] = va_arg(args_lst,char*) ; \
		end_action          ;                                                \
		va_end(arg_cnt_lst) ;                                                \
		va_end(args_lst   ) ;                                                \
		int rc = value      ;                                                \
		delete[] args ;                                                      \
		return rc
	int execl (CC* p,CC* arg,...) NE { MK_ARGS(                                             , execv (p,args     ) ) ; }
	int execle(CC* p,CC* arg,...) NE { MK_ARGS( char* const* envp = va_arg(args_lst,char**) , execve(p,args,envp) ) ; }
	int execlp(CC* p,CC* arg,...) NE { MK_ARGS(                                             , execvp(p,args     ) ) ; }
	#undef MK_ARGS

	// fopen                                                 is_stat
	FILE* fopen    (CC* p,CC* m         ) { HEADER1(fopen    ,false,p,(p,m   )) ; Fopen r{p,m,"fopen"    } ; return r(orig(p,m   )) ; }
	FILE* fopen64  (CC* p,CC* m         ) { HEADER1(fopen64  ,false,p,(p,m   )) ; Fopen r{p,m,"fopen64"  } ; return r(orig(p,m   )) ; }
	FILE* freopen  (CC* p,CC* m,FILE* fp) { HEADER1(freopen  ,false,p,(p,m,fp)) ; Fopen r{p,m,"freopen"  } ; return r(orig(p,m,fp)) ; }
	FILE* freopen64(CC* p,CC* m,FILE* fp) { HEADER1(freopen64,false,p,(p,m,fp)) ; Fopen r{p,m,"freopen64"} ; return r(orig(p,m,fp)) ; }

	// fork
	// not recursively called by auditing code
	// /!\ lock is not strictly necessary, but we must beware of interaction between lock & fork : locks are duplicated
	//     if another thread has the lock while we fork => child will dead lock as it has the lock but not the thread
	//     a simple way to stay coherent is to take the lock before fork and to release it after both in parent & child
	// vfork does not duplicate its memory and need no special treatment (as with clone with the CLONE_VM flag)
	//                                                 is_stat
	pid_t fork       (       ) NE { HEADER0(fork       ,false,(   )) ; NO_SERVER(fork       ) ; return orig  (   ) ; }
	pid_t __fork     (       ) NE { HEADER0(__fork     ,false,(   )) ; NO_SERVER(__fork     ) ; return orig  (   ) ; }
	pid_t __libc_fork(       ) NE { HEADER0(__libc_fork,false,(   )) ; NO_SERVER(__libc_fork) ; return orig  (   ) ; }
	int   system     (CC* cmd)    { HEADER0(system     ,false,(cmd)) ; NO_SERVER(system     ) ; return orig  (cmd) ; } // actually does a fork
	#if MAP_VFORK
		pid_t vfork  () NE { return fork  () ; } // for posix systems, instrumenting exec* after vfork is forbidden, in exchange vfork is a subset of fork ...
		pid_t __vfork() NE { return __fork() ; } // ... for linux, instrumenting exec* after vfork is ok but vfork is more precisely specified and cannot be mapped to fork
	#endif

	// link                                                          is_stat                                              no_follow
	int link  (       CC* op,       CC* np      ) NE { HEADER2(link  ,false,op,np,(   op,   np  )) ; Lnk r{    op ,    np ,false   ,"link"  } ; return r(orig(   op,   np  )) ; }
	int linkat(int od,CC* op,int nd,CC* np,int f) NE { HEADER2(linkat,false,op,np,(od,op,nd,np,f)) ; Lnk r{{od,op},{nd,np},ASLNF(f),"linkat"} ; return r(orig(od,op,nd,np,f)) ; }

	// mkdir                                              is_stat
	int mkdir  (      CC* p,mode_t m) NE { HEADER1(mkdir  ,false,p,(  p,m)) ; Mkdir r{   p ,"mkdirat"} ; return r(orig(  p,m)) ; }
	int mkdirat(int d,CC* p,mode_t m) NE { HEADER1(mkdirat,false,p,(d,p,m)) ; Mkdir r{{d,p},"mkdir"  } ; return r(orig(d,p,m)) ; }

	// mkstemp                                                 is_stat
	int mkstemp    (char* t             ) { HEADER0(mkstemp    ,false,(t     )) ; Mkstemp r{t,   "mkstemp"    } ; return r(orig(t     )) ; }
	int mkostemp   (char* t,int f       ) { HEADER0(mkostemp   ,false,(t,f   )) ; Mkstemp r{t,   "mkostemp"   } ; return r(orig(t,f   )) ; }
	int mkstemps   (char* t,      int sl) { HEADER0(mkstemps   ,false,(t,  sl)) ; Mkstemp r{t,sl,"mkstemps"   } ; return r(orig(t,  sl)) ; }
	int mkostemps  (char* t,int f,int sl) { HEADER0(mkostemps  ,false,(t,f,sl)) ; Mkstemp r{t,sl,"mkostemps"  } ; return r(orig(t,f,sl)) ; }
	int mkstemp64  (char* t             ) { HEADER0(mkstemp64  ,false,(t     )) ; Mkstemp r{t,   "mkstemp64"  } ; return r(orig(t     )) ; }
	int mkostemp64 (char* t,int f       ) { HEADER0(mkostemp64 ,false,(t,f   )) ; Mkstemp r{t,   "mkostemp64" } ; return r(orig(t,f   )) ; }
	int mkstemps64 (char* t,      int sl) { HEADER0(mkstemps64 ,false,(t,  sl)) ; Mkstemp r{t,sl,"mkstemps64" } ; return r(orig(t,  sl)) ; }
	int mkostemps64(char* t,int f,int sl) { HEADER0(mkostemps64,false,(t,f,sl)) ; Mkstemp r{t,sl,"mkostemps64"} ; return r(orig(t,f,sl)) ; }

	// mount
	int mount(CC* sp,CC* tp,CC* fst,ulong f,const void* d) {
		HEADER( mount , false/*is_stat*/ , !(f&MS_BIND) || (Record::s_is_simple(sp)&&Record::s_is_simple(tp)) , (sp,tp,fst,f,d) ) ;
		Mount r{sp,tp,"mount"} ;
		return r(orig(sp,tp,fst,f,d)) ;
	}

	// name_to_handle_at
	int name_to_handle_at( int d , CC* p , struct ::file_handle *h , int *mount_id , int f ) NE {
		HEADER1( name_to_handle_at , false/*is_stat*/ , p , (d,p,h,mount_id,f) ) ;
		Open r{{d,p},f,"name_to_handle_at"} ;
		return r(orig(d,p,h,mount_id,f)) ;
	}

	// open
	#define MOD mode_t m = 0 ; if ( f & (O_CREAT|O_TMPFILE) ) { va_list lst ; va_start(lst,f) ; m = va_arg(lst,mode_t) ; va_end(lst) ; }
	//                                                                            is_stat
	int open             (      CC* p,int f,...) { MOD ; HEADER1(open             ,false,p,(  p,f,m)) ; Open r{   p ,f                         ,"open"             } ; return r(orig(  p,f,m)) ; }
	int __open           (      CC* p,int f,...) { MOD ; HEADER1(__open           ,false,p,(  p,f,m)) ; Open r{   p ,f                         ,"__open"           } ; return r(orig(  p,f,m)) ; }
	int __open_nocancel  (      CC* p,int f,...) { MOD ; HEADER1(__open_nocancel  ,false,p,(  p,f,m)) ; Open r{   p ,f                         ,"__open_nocancel"  } ; return r(orig(  p,f,m)) ; }
	int __open_2         (      CC* p,int f    ) {       HEADER1(__open_2         ,false,p,(  p,f  )) ; Open r{   p ,f                         ,"__open_2"         } ; return r(orig(  p,f  )) ; }
	int open64           (      CC* p,int f,...) { MOD ; HEADER1(open64           ,false,p,(  p,f,m)) ; Open r{   p ,f                         ,"open64"           } ; return r(orig(  p,f,m)) ; }
	int __open64         (      CC* p,int f,...) { MOD ; HEADER1(__open64         ,false,p,(  p,f,m)) ; Open r{   p ,f                         ,"__open64"         } ; return r(orig(  p,f,m)) ; }
	int __open64_nocancel(      CC* p,int f,...) { MOD ; HEADER1(__open64_nocancel,false,p,(  p,f,m)) ; Open r{   p ,f                         ,"__open64_nocancel"} ; return r(orig(  p,f,m)) ; }
	int __open64_2       (      CC* p,int f    ) {       HEADER1(__open64_2       ,false,p,(  p,f  )) ; Open r{   p ,f                         ,"__open64_2"       } ; return r(orig(  p,f  )) ; }
	int openat           (int d,CC* p,int f,...) { MOD ; HEADER1(openat           ,false,p,(d,p,f,m)) ; Open r{{d,p},f                         ,"openat"           } ; return r(orig(d,p,f,m)) ; }
	int __openat_2       (int d,CC* p,int f    ) {       HEADER1(__openat_2       ,false,p,(d,p,f  )) ; Open r{{d,p},f                         ,"__openat_2"       } ; return r(orig(d,p,f  )) ; }
	int openat64         (int d,CC* p,int f,...) { MOD ; HEADER1(openat64         ,false,p,(d,p,f,m)) ; Open r{{d,p},f                         ,"openat64"         } ; return r(orig(d,p,f,m)) ; }
	int __openat64_2     (int d,CC* p,int f    ) {       HEADER1(__openat64_2     ,false,p,(d,p,f  )) ; Open r{{d,p},f                         ,"__openat64_2"     } ; return r(orig(d,p,f  )) ; }
	int creat            (      CC* p,mode_t m ) {       HEADER1(creat            ,false,p,(  p,  m)) ; Open r{   p ,(O_CREAT|O_WRONLY|O_TRUNC),"creat"            } ; return r(orig(  p,  m)) ; }
	int creat64          (      CC* p,mode_t m ) {       HEADER1(creat64          ,false,p,(  p,  m)) ; Open r{   p ,(O_CREAT|O_WRONLY|O_TRUNC),"creat64"          } ; return r(orig(  p,  m)) ; }
	#undef MOD
	DIR* opendir(CC* p) { HEADER1(opendir,false/*is_stat*/,p,(p)) ; Solve r{p,true/*no_follow*/,false/*read*/,false/*create*/,"opendir"  } ; return r(orig(p)) ; }

	// readlink
	#define RL Readlink
	#ifdef LD_PRELOAD_JEMALLOC
		// jemalloc does a readlink of its config file (/etc/jemalloc.conf) during its init phase
		// under some circumstances (not really understood), dlsym, which is necessary to find the original readlink function calls malloc
		// this creates a loop, leading to a deadlock in jemalloc as it takes a mutex during its init phase
		// this is a horible hack to avoid calling dlsym : readlink is redirected to __readlink_chk (which is, thus, left unprotected)
		// once init phase is passed, we proceed normally
		ssize_t readlink(CC* p,char* b,size_t sz) NE {
			if (!started()) return __readlink_chk(p,b,sz,sz) ;
			HEADER1(readlink,false/*is_stat*/,p,(p,b,sz)) ; RL r{p ,b,sz,"readlink"} ; return r(orig(p,b,sz)) ;
		}
	#else //!                                                                                 is_stat
		ssize_t readlink      (CC* p,char* b,size_t sz           ) NE { HEADER1(readlink      ,false,p,(p,b,sz    )) ; RL r{p ,b,sz,"readlink"       } ; return r(orig(p,b,sz    )) ; }
		ssize_t __readlink_chk(CC* p,char* b,size_t sz,size_t bsz) NE { HEADER1(__readlink_chk,false,p,(p,b,sz,bsz)) ; RL r{p ,b,sz,"__readlink__chk"} ; return r(orig(p,b,sz,bsz)) ; }
	#endif //!                                                                                      is_stat
	ssize_t readlinkat      (int d,CC* p,char* b,size_t sz           ) NE { HEADER1(readlinkat      ,false,p,(d,p,b,sz    )) ; RL r{{d,p},b,sz,"readlinkat"      } ; return r(orig(d,p,b,sz    )) ; }
	ssize_t __readlinkat_chk(int d,CC* p,char* b,size_t sz,size_t bsz) NE { HEADER1(__readlinkat_chk,false,p,(d,p,b,sz,bsz)) ; RL r{{d,p},b,sz,"__readlinkat_chk"} ; return r(orig(d,p,b,sz,bsz)) ; }
	#undef RL

	// rename
	#ifdef RENAME_EXCHANGE
		#define REXC(flags) bool((flags)&RENAME_EXCHANGE)
	#else
		#define REXC(flags) false
	#endif
	#ifdef RENAME_NOREPLACE
		#define RNR(flags) bool((flags)&RENAME_NOREPLACE)
	#else
		#define RNR(flags) false
	#endif //!                                                              is_stat                                                 exchange no_replace
	int rename   (       CC* op,       CC* np       ) NE { HEADER2(rename   ,false,op,np,(   op,   np  )) ; Rename r{    op ,    np ,false  ,false    ,"rename"   } ; return r(orig(   op,   np  )) ; }
	int renameat (int od,CC* op,int nd,CC* np       ) NE { HEADER2(renameat ,false,op,np,(od,op,nd,np  )) ; Rename r{{od,op},{nd,np},false  ,false    ,"renameat" } ; return r(orig(od,op,nd,np  )) ; }
	int renameat2(int od,CC* op,int nd,CC* np,uint f) NE { HEADER2(renameat2,false,op,np,(od,op,nd,np,f)) ; Rename r{{od,op},{nd,np},REXC(f),RNR(f)   ,"renameat2"} ; return r(orig(od,op,nd,np,f)) ; }
	#undef RNR
	#undef REXC

	// rmdir
	int rmdir(CC* p) NE { HEADER1(rmdir,false,p,(p)) ; Unlnk r{p,true/*rmdir*/,"rmdir"} ; return r(orig(p)) ; }

	// symlink                                             is_stat
	int symlink  (CC* t,      CC* p) NE { HEADER1(symlink  ,false,p,(t,  p)) ; Symlink r{   p ,"symlink"  } ; return r(orig(t,  p)) ; }
	int symlinkat(CC* t,int d,CC* p) NE { HEADER1(symlinkat,false,p,(t,d,p)) ; Symlink r{{d,p},"symlinkat"} ; return r(orig(t,d,p)) ; }

	// truncate                                          is_stat
	int truncate  (CC* p,off_t   l) NE { HEADER1(truncate  ,false,p,(p,l)) ; Open r{p,l?O_RDWR:O_WRONLY,"truncate"  } ; return r(orig(p,l)) ; }
	int truncate64(CC* p,off64_t l) NE { HEADER1(truncate64,false,p,(p,l)) ; Open r{p,l?O_RDWR:O_WRONLY,"truncate64"} ; return r(orig(p,l)) ; }

	// unlink                                            is_stat
	int unlink  (      CC* p      ) NE { HEADER1(unlink  ,false,p,(  p  )) ; Unlnk r{   p ,false/*rmdir*/      ,"unlink"  } ; return r(orig(  p  )) ; }
	int unlinkat(int d,CC* p,int f) NE { HEADER1(unlinkat,false,p,(d,p,f)) ; Unlnk r{{d,p},bool(f&AT_REMOVEDIR),"unlinkat"} ; return r(orig(d,p,f)) ; }

	// utime                                                                       is_stat                            no_follow read  create
	int utime    (      CC* p,const struct utimbuf* t         ) { HEADER1(utime    ,false,p,(  p,t  )) ; Solve r{   p ,false   ,false,false,"utime"    } ; return r(orig(  p,t  )) ; }
	int utimes   (      CC* p,const struct timeval  t[2]      ) { HEADER1(utimes   ,false,p,(  p,t  )) ; Solve r{   p ,false   ,false,false,"utimes"   } ; return r(orig(  p,t  )) ; }
	int futimesat(int d,CC* p,const struct timeval  t[2]      ) { HEADER1(futimesat,false,p,(d,p,t  )) ; Solve r{{d,p},false   ,false,false,"futimesat"} ; return r(orig(d,p,t  )) ; }
	int lutimes  (      CC* p,const struct timeval  t[2]      ) { HEADER1(lutimes  ,false,p,(  p,t  )) ; Solve r{   p ,true    ,false,false,"lutimes"  } ; return r(orig(  p,t  )) ; }
	int utimensat(int d,CC* p,const struct timespec t[2],int f) { HEADER1(utimensat,false,p,(d,p,t,f)) ; Solve r{{d,p},ASLNF(f),false,false,"utimensat"} ; return r(orig(d,p,t,f)) ; }

	// mere path accesses (neeed to solve path, but no actual access to file data)
	#define ACCESSES(msk) ( (msk)&X_OK ? Accesses(Access::Reg) : Accesses() )
	//                                                            is_stat                           no_follow accesses
	int access   (      CC* p,int m      ) NE { HEADER1(access   ,true ,p,(  p,m  )) ; Stat r{   p ,false   ,ACCESSES(m),"access"   } ; return r(orig(  p,m  )) ; }
	int faccessat(int d,CC* p,int m,int f) NE { HEADER1(faccessat,true ,p,(d,p,m,f)) ; Stat r{{d,p},ASLNF(f),ACCESSES(m),"faccessat"} ; return r(orig(d,p,m,f)) ; }
	#undef ACCESSES
	// stat* accesses provide the size field, which make the user sensitive to file content
	//                                                                                  is_stat                             no_follow accesses
	int __xstat     (int v,      CC* p,struct stat  * b      ) NE { HEADER1(__xstat     ,true ,p,(v,  p,b  )) ; Stat r{   p ,false   ,~Accesses(),"__xstat"     } ; return r(orig(v,  p,b  )) ; }
	int __xstat64   (int v,      CC* p,struct stat64* b      ) NE { HEADER1(__xstat64   ,true ,p,(v,  p,b  )) ; Stat r{   p ,false   ,~Accesses(),"__xstat64"   } ; return r(orig(v,  p,b  )) ; }
	int __lxstat    (int v,      CC* p,struct stat  * b      ) NE { HEADER1(__lxstat    ,true ,p,(v,  p,b  )) ; Stat r{   p ,true    ,~Accesses(),"__lxstat"    } ; return r(orig(v,  p,b  )) ; }
	int __lxstat64  (int v,      CC* p,struct stat64* b      ) NE { HEADER1(__lxstat64  ,true ,p,(v,  p,b  )) ; Stat r{   p ,true    ,~Accesses(),"__lxstat64"  } ; return r(orig(v,  p,b  )) ; }
	int __fxstatat  (int v,int d,CC* p,struct stat  * b,int f) NE { HEADER1(__fxstatat  ,true ,p,(v,d,p,b,f)) ; Stat r{{d,p},ASLNF(f),~Accesses(),"__fxstatat"  } ; return r(orig(v,d,p,b,f)) ; }
	int __fxstatat64(int v,int d,CC* p,struct stat64* b,int f) NE { HEADER1(__fxstatat64,true ,p,(v,d,p,b,f)) ; Stat r{{d,p},ASLNF(f),~Accesses(),"__fxstatat64"} ; return r(orig(v,d,p,b,f)) ; }
	#if !NEED_STAT_WRAPPERS //!                                                 is_stat                           no_follow accesses
		int stat     (      CC* p,struct stat  * b      ) NE { HEADER1(stat     ,true ,p,(  p,b  )) ; Stat r{   p ,false   ,~Accesses(),"stat"     } ; return r(orig(  p,b  )) ; }
		int stat64   (      CC* p,struct stat64* b      ) NE { HEADER1(stat64   ,true ,p,(  p,b  )) ; Stat r{   p ,false   ,~Accesses(),"stat64"   } ; return r(orig(  p,b  )) ; }
		int lstat    (      CC* p,struct stat  * b      ) NE { HEADER1(lstat    ,true ,p,(  p,b  )) ; Stat r{   p ,true    ,~Accesses(),"lstat"    } ; return r(orig(  p,b  )) ; }
		int lstat64  (      CC* p,struct stat64* b      ) NE { HEADER1(lstat64  ,true ,p,(  p,b  )) ; Stat r{   p ,true    ,~Accesses(),"lstat64"  } ; return r(orig(  p,b  )) ; }
		int fstatat  (int d,CC* p,struct stat  * b,int f) NE { HEADER1(fstatat  ,true ,p,(d,p,b,f)) ; Stat r{{d,p},ASLNF(f),~Accesses(),"fstatat"  } ; return r(orig(d,p,b,f)) ; }
		int fstatat64(int d,CC* p,struct stat64* b,int f) NE { HEADER1(fstatat64,true ,p,(d,p,b,f)) ; Stat r{{d,p},ASLNF(f),~Accesses(),"fstatat64"} ; return r(orig(d,p,b,f)) ; }
	#endif
	int statx(int d,CC* p,int f,uint msk,struct statx* b) NE {                   // statx must exist even if statx is not supported by the system as it appears in ENUMERATE_LIBCALLS
		HEADER1(statx,true/*is_stat*/,p,(d,p,f,msk,b)) ;
		#if defined(STATX_TYPE) && defined(STATX_SIZE) && defined(STATX_BLOCKS) && defined(STATX_MODE)
			Accesses a ;
			if      (msk&(STATX_TYPE|STATX_SIZE|STATX_BLOCKS)) a = ~Accesses() ; // user can distinguish all content
			else if (msk& STATX_MODE                         ) a = Access::Reg ; // user can distinguish executable files, which is part of crc for regular files
		#else
			Accesses a = ~Accesses() ;                                           // if access macros are not defined, be pessimistic
		#endif
		Stat r{{d,p},true/*no_follow*/,a,"statx"} ;
		return r(orig(d,p,f,msk,b)) ;
	}

	// realpath                                                                               is_stat                       no_follow accesses
	char* realpath              (CC* p,char* rp          ) NE { HEADER1(realpath              ,false,p,(p,rp   )) ; Stat r{p,false   ,Accesses(),"realpath"              } ; return r(orig(p,rp   )) ; }
	char* __realpath_chk        (CC* p,char* rp,size_t rl) NE { HEADER1(__realpath_chk        ,false,p,(p,rp,rl)) ; Stat r{p,false   ,Accesses(),"__realpath_chk"        } ; return r(orig(p,rp,rl)) ; }
	char* canonicalize_file_name(CC* p                   ) NE { HEADER1(canonicalize_file_name,false,p,(p      )) ; Stat r{p,false   ,Accesses(),"canonicalize_file_name"} ; return r(orig(p      )) ; }

	// scandir
	using NmLst   = struct dirent  ***                                       ;
	using NmLst64 = struct dirent64***                                       ;
	using Fltr    = int (*)(const struct dirent  *                         ) ;
	using Fltr64  = int (*)(const struct dirent64*                         ) ;
	using Cmp     = int (*)(const struct dirent**  ,const struct dirent  **) ;
	using Cmp64   = int (*)(const struct dirent64**,const struct dirent64**) ;
	//                                                                            is_stat                               no_follow read create
	int scandir    (      CC* p,NmLst   nl,Fltr   f,Cmp   c) { HEADER1(scandir    ,false,p,(  p,nl,f,c)) ; Solve r{   p ,true    ,false,false,"scandir"    } ; return r(orig(  p,nl,f,c)) ; }
	int scandir64  (      CC* p,NmLst64 nl,Fltr64 f,Cmp64 c) { HEADER1(scandir64  ,false,p,(  p,nl,f,c)) ; Solve r{   p ,true    ,false,false,"scandir64"  } ; return r(orig(  p,nl,f,c)) ; }
	int scandirat  (int d,CC* p,NmLst   nl,Fltr   f,Cmp   c) { HEADER1(scandirat  ,false,p,(d,p,nl,f,c)) ; Solve r{{d,p},true    ,false,false,"scandirat"  } ; return r(orig(d,p,nl,f,c)) ; }
	int scandirat64(int d,CC* p,NmLst64 nl,Fltr64 f,Cmp64 c) { HEADER1(scandirat64,false,p,(d,p,nl,f,c)) ; Solve r{{d,p},true    ,false,false,"scandirat64"} ; return r(orig(d,p,nl,f,c)) ; }

	#undef CC

	// syscall
	// /!\ we must be very careful to avoid dead-lock :
	// - mutex calls futex management, which sometimes call syscall
	// - so filter on s_tab must be done before locking (in HEADER)
	// - this requires that s_tab does no memory allocation as memory allocation may call brk
	// - hence it is a ::array, not a ::umap (which would be simpler)
	long syscall( long n , ... ) {
		static constexpr SyscallDescr NoSyscallDescr ;
		uint64_t args[6] ;
		{	va_list lst ; va_start(lst,n) ;
			args[0] = va_arg(lst,uint64_t) ;
			args[1] = va_arg(lst,uint64_t) ;
			args[2] = va_arg(lst,uint64_t) ;
			args[3] = va_arg(lst,uint64_t) ;
			args[4] = va_arg(lst,uint64_t) ;
			args[5] = va_arg(lst,uint64_t) ;
			va_end(lst) ;
		}
		SyscallDescr::Tab const& tab   = SyscallDescr::s_tab()                                     ;
		SyscallDescr      const& descr = n>=0&&n<SyscallDescr::NSyscalls ? tab[n] : NoSyscallDescr ; // protect against arbitrary invalid syscall numbers
		HEADER(
			syscall
		,	false/*is_stat*/
		,	!descr || (descr.filter&&Record::s_is_simple(reinterpret_cast<const char*>(args[descr.filter-1])))
		,	(n,args[0],args[1],args[2],args[3],args[4],args[5])
		) ;
		void* descr_ctx = nullptr ;
		Ctx audit_ctx ;                                                                              // save user errno when required
		//               vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		if (descr.entry) descr.entry( descr_ctx , auditor() , 0/*pid*/ , args , descr.comment ) ;
		//               ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		audit_ctx.restore_errno() ;
		//         vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		long res = orig(n,args[0],args[1],args[2],args[3],args[4],args[5]) ;
		//         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		audit_ctx.save_errno() ;
		//                     vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		if (descr.exit) return descr.exit( descr_ctx , auditor() , 0/*pid*/ , res ) ;
		else            return res                                                  ;
		//                     ^^^
	}

	#undef NO_SERVER
	#undef HEADER2
	#undef HEADER1
	#undef HEADER0
	#undef HEADER

	#undef ORIG

	#undef EXE
	#undef ASLNF
	#undef NE

	#pragma GCC diagnostic pop
}
#ifdef LD_PRELOAD
	#pragma GCC visibility pop
#endif
