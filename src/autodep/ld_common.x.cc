// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <errno.h>
#include <sched.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mount.h>
#include <sys/time.h>
#include <unistd.h>
#include <utime.h>

#include "disk.hh"

#define NE noexcept

#include "gather.hh"
#include "record.hh"
#include "syscall_tab.hh"

// easier to use #if instead of #ifdef
#ifndef LD_AUDIT
	#define LD_AUDIT 0
#endif
#ifndef LD_PRELOAD
	#define LD_PRELOAD 0
#endif
#ifndef LD_PRELOAD_JEMALLOC
	#define LD_PRELOAD_JEMALLOC 0
#endif
#ifndef IN_SERVER
	#define IN_SERVER 0
#endif

#ifndef O_TMPFILE
	#define O_TMPFILE 0 // no check for O_TMPFILE if it does not exist0
#endif

#define NEED_ELF (!LD_AUDIT) // else elf dependencies are captured by auditing code or unavailable

#if NEED_ELF
	#include <dlfcn.h> // dlopen
	#include "elf.hh"
#endif

// compiled with flag -fvisibility=hidden : this is good for perf and with LD_PRELOAD, we do not polute application namespace

using namespace Disk ;

#if LD_AUDIT
	struct SaveErrno {
		void restore_errno() {} // our errno is not the same as user errno, so nothing to do
	} ;
#else
	struct SaveErrno {
		// cxtors & casts
		SaveErrno() { errno_ = errno  ; }
		// services
		void restore_errno() { errno  = errno_ ; }
		// data
		int errno_ ;
	} ;
#endif

extern "C" {
	// the following libcalls are defined in libc not always in #include's, so they may be called by application code
	// they may not be defined on all systems, but it does not hurt to redeclare them if they are already declared, so filter may be loose
	extern int     __clone2        ( int (*fn)(void*) , void *stack_base , size_t stack_size , int flags , void *arg , ...    ) ;
	extern int     __close         (         int fd                                                                           )    ;
	extern int     __dup2          (         int oldfd , int newfd                                                            ) NE ;
	extern pid_t   __fork          (                                                                                          ) NE ;
	extern pid_t   __libc_fork     (                                                                                          ) NE ;
	extern int     __open          (                     const char* pth , int flgs , ...                                     )    ;
	extern int     __open_nocancel (                     const char* pth , int flgs , ...                                     )    ;
	extern int     __open_2        (                     const char* pth , int flgs                                           )    ;
	extern int     __openat_2      (         int dfd   , const char* pth , int flgs                                           )    ;
	extern ssize_t __readlink_chk  (                     const char* pth , char*   , size_t l , size_t bsz                    ) NE ;
	extern ssize_t __readlinkat_chk(         int dfd   , const char* pth , char* b , size_t l , size_t bsz                    ) NE ;
	extern char*   __realpath_chk  (                     const char* pth , char* rpth , size_t rlen                           ) NE ;
	extern int     __xstat         ( int v ,             const char* pth , struct stat* buf                                   ) NE ;
	extern int     __lxstat        ( int v ,             const char* pth , struct stat* buf                                   ) NE ;
	extern int     __fxstatat      ( int v , int dfd   , const char* pth , struct stat* buf , int flgs                        ) NE ;
	extern void    closefrom       (         int  fd1                                                                         ) NE ;
	extern int     execveat        (         int dirfd , const char* pth , char* const argv[] , char *const envp[] , int flgs ) NE ;
	extern int     faccessat2      (         int dirfd , const char* pth , int mod , int flgs                                 ) NE ;
	extern int     renameat2       (         int odfd  , const char* op  , int ndfd , const char* np , uint flgs              ) NE ;
	extern int     statx           (         int dirfd , const char* pth , int flgs , uint msk , struct statx* buf            ) NE ;
	//
	extern int __open64         (                   const char* pth , int flgs , ...                )    ;
	extern int __open64_nocancel(                   const char* pth , int flgs , ...                )    ;
	extern int __open64_2       (                   const char* pth , int flgs                      )    ;
	extern int __openat64_2     (         int dfd , const char* pth , int flgs                      )    ;
	extern int __xstat64        ( int v ,           const char* pth , struct stat64* buf            ) NE ;
	extern int __lxstat64       ( int v ,           const char* pth , struct stat64* buf            ) NE ;
	extern int __fxstatat64     ( int v , int dfd , const char* pth , struct stat64* buf , int flgs ) NE ;
	//
	#if HAS_CLOSE_RANGE
		extern int close_range( uint fd1 , uint fd2 , int flgs ) NE ;
	#endif
}

static              Mutex<MutexLvl::Autodep2> _g_mutex ;         // ensure exclusivity between threads
static thread_local bool                      _t_loop  = false ; // prevent recursion within a thread

// User program may have global variables whose cxtor/dxtor do accesses.
// In that case, they may come before our own Audit is constructed if declared global (in the case of LD_PRELOAD).
// To face this order problem, we declare our Audit as a static within a funciton which will be constructed upon first call.
// As all statics with cxtor/dxtor, we define it through new so as to avoid destruction during finalization.
#if !IN_SERVER
	static                                // in server, we want to have direct access to recorder (no risk of name pollution as we masterize the code)
#endif
Record& auditor() {
	static Record* s_res = nullptr ;
	if (!s_res) s_res = new Record{New} ; // dont initialize directly as C++ guard for static variables may do some syscalls
	return *s_res ;
}

template<class Action,int NP=1> struct AuditAction : SaveErrno,Action {
	// cxtors & casts
	// errno must be protected from our auditing actions in cxtor and operator()
	// more specifically, errno must be the original one before the actual call to libc
	// and must be the one after the actual call to libc when auditing code finally leave
	// SaveErrno contains save_errno in its cxtor
	// so here, errno must be restored at the end of cxtor
	template<class... A> AuditAction(                                    A&&... args) requires(NP==0) : Action{auditor(),                      ::forward<A>(args)... } { restore_errno() ; }
	template<class... A> AuditAction(char*          p  ,                 A&&... args) requires(NP==1) : Action{auditor(),Record::WPath(p)     ,::forward<A>(args)... } { restore_errno() ; }
	template<class... A> AuditAction(Record::Path&& p  ,                 A&&... args) requires(NP==1) : Action{auditor(),::move(p )           ,::forward<A>(args)... } { restore_errno() ; }
	template<class... A> AuditAction(Record::Path&& p1,Record::Path&& p2,A&&... args) requires(NP==2) : Action{auditor(),::move(p1),::move(p2),::forward<A>(args)... } { restore_errno() ; }
	// services
	template<class T> T operator()(T res) { return Action::operator()(auditor(),res) ; }
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
using Readlink = AuditAction<Record::Readlink      > ;
using Rename   = AuditAction<Record::Rename  ,2    > ;
using Solve    = AuditAction<Record::Solve         > ;
using Stat     = AuditAction<Record::Stat          > ;
using Symlink  = AuditAction<Record::Symlink       > ;
using Unlnk    = AuditAction<Record::Unlnk         > ;
using WSolve   = AuditAction<Record::WSolve        > ;

#if NEED_ELF

	//
	// Dlopen
	//

	struct _Dlopen : Record::ReadCS {
		using Base = Record::ReadCS ;
		// cxtors & casts
		_Dlopen() = default ;
		_Dlopen( Record& r , const char* file , Comment c ) : Base{search_elf(r,file,c)} {}
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
	_Exec( Record& r , Record::Path&& path , bool no_follow , const char* const envp[] , Comment c ) : Base{r,::move(path),no_follow,c} {
		#if NEED_ELF
			static constexpr char   Llpe[] = "LD_LIBRARY_PATH=" ;
			static constexpr size_t LlpeSz = sizeof(Llpe)-1     ;                          // -1 to account of terminating null
			//
			const char* const* llp ;
			for( llp=envp ; *llp ; llp++ ) if (strncmp( *llp , Llpe , LlpeSz )==0) break ;
			if (*llp) elf_deps( r , self , *llp+LlpeSz , c+1/*Dep*/ ) ;                // pass value after the LD_LIBRARY_PATH= prefix
			else      elf_deps( r , self , nullptr     , c+1/*Dep*/ ) ;                // /!\ dont add LlpeSz to nullptr
		#else
			(void)envp ;
		#endif
	}
} ;
using Exec = AuditAction<_Exec> ;

struct _Execp : _Exec {
	using Base = _Exec ;
	// search executable file in PATH
	_Execp() = default ;
	_Execp( Record& r , const char* file , bool /*no_follow*/ , const char* const envp[] , Comment c ) {
		if (!file) return ;
		//
		if (::strchr(file,'/')) {                                                             // if file contains a /, no search is performed
			static_cast<Base&>(self) = Base(r,file,false/*no_follow*/,envp,c) ;
			return ;
		}
		//
		::string p = get_env("PATH") ;
		if (!p) {                                                                             // gather standard path if path not provided
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
			Record::Read(r,full_file,false/*no_follow*/,true/*keep_real*/,c) ;
			if (is_exe(full_file,false/*no_follow*/)) {
				static_cast<Base&>(self) = Base(r,full_file,false/*no_follow*/,envp,c) ;
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
		for( const char* m=mode ; *m && *m!=',' ; m++ )    // after a ',', there is a css=xxx which we do not care about
			switch (*m) {
				case 'r' : r = true ; break ;
				case 'w' : w = true ; break ;
				case 'a' : a = true ; break ;
				case '+' : p = true ; break ;
			DN}
		if (a+r+w!=1) return O_DIRECTORY ;                 // error case, no access
		int flags = p ? O_RDWR : r ? O_RDONLY : O_WRONLY ;
		if (!r) flags |= O_CREAT  ;
		if (w ) flags |= O_TRUNC  ;
		if (a ) flags |= O_APPEND ;
		return flags ;
	}
	Fopen( Record::Path&& pth , const char* mode , Comment c ) : Base{ ::move(pth) , mk_flags(mode) , c } {}
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
	Mkstemp( char* t , int sl , Comment c ) : Base{ t , true/*no_follow*/ , false/*read*/ , true/*create*/ , c } , tmpl{t} , sfx_len{sl} , comment{c} {}
	Mkstemp( char* t ,          Comment c ) : Mkstemp(t,0,c) {}
	int operator()(int fd) {
		// in case of success, tmpl is modified to contain the file that was actually opened, and it was called with file instead of tmpl
		if (file!=tmpl) ::memcpy( tmpl+::strlen(tmpl)-sfx_len-6 , file+::strlen(file)-sfx_len-6 , 6 ) ;
		if (fd>=0     ) Record::Open(auditor(),file,O_CREAT|O_WRONLY|O_TRUNC|O_NOFOLLOW,comment)(auditor(),fd) ;
		return Base::operator()(fd) ;
	}
	// data
	char*    tmpl    = nullptr/*garbage*/ ;
	int      sfx_len = 0      /*garbage*/ ;
	Comment  comment ;
} ;

//
// Audited
//

#if LD_PRELOAD
	// for ld_preload, we want to hide libc functions so as to substitute the auditing functions to the regular functions
	#pragma GCC visibility push(default) // force visibility of functions defined hereinafter, until the corresponding pop
	extern "C"
#endif
#if LD_AUDIT
	// for ld_audit, we want to use private functions so auditing code can freely call the libc without having to deal with errno
	namespace Audited
#endif
{
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wignored-attributes"

	#define ASLNF(flags) bool((flags)&AT_SYMLINK_NOFOLLOW)
	#define EXE(  mode ) bool((mode )&S_IXUSR            )

	struct SaveTloop {
		SaveTloop () { SWEAR(!_t_loop) ; _t_loop = true  ; }
		~SaveTloop() {                   _t_loop = false ; }
	} ;

	// cwd is implicitly accessed by mostly all libcalls, so we have to ensure mutual exclusion as cwd could change between actual access and path resolution in audit
	// hence we should use a shared lock when reading and an exclusive lock when chdir
	// however, we have to ensure exclusivity for lnk cache, so we end up to exclusive access anyway, so simpler to lock exclusively here
	// no malloc must be performed before cond is checked to allow jemalloc accesses to be filtered, hence auditor() (which allocates a Record) is done after
	// use short macros as lines are very long in defining audited calls to libc
	// protect against recusive calls
	// args must be in () e.g. HDR1(unlink,path,(path))
	#define ORIG(libcall) static decltype(::libcall)* orig = reinterpret_cast<decltype(::libcall)*>(get_orig(#libcall)) ;
	#define HDR(libcall,cond,args) \
		ORIG(libcall) ;                                                                      \
		if ( UNLIKELY(_t_loop) || !LIKELY(started()) || LIKELY(cond) ) return (*orig) args ; \
		SaveTloop sav_t_loop ;                                                               \
		Lock      lock       { _g_mutex }
	// do a first check to see if it is obvious that nothing needs to be done
	#define HDR0(libcall,            args) HDR( libcall , false                                                    , args )
	#define HDR1(libcall,path,       args) HDR( libcall , Record::s_is_simple(path )                               , args )
	#define HDR2(libcall,path1,path2,args) HDR( libcall , Record::s_is_simple(path1) && Record::s_is_simple(path2) , args )
	// macro for libcall that are forbidden in server when recording deps
	#if IN_SERVER
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
	int chdir (CC* p ) NE { HDR0(chdir ,(p )) ; NO_SERVER(chdir ) ; Chdir r{p     ,Comment::Cchdir } ; return r(orig(p )) ; }
	int fchdir(int fd) NE { HDR0(fchdir,(fd)) ; NO_SERVER(fchdir) ; Chdir r{Fd(fd),Comment::Cfchdir} ; return r(orig(fd)) ; }

	// chmod
	// although file is not modified, resulting file after chmod depends on its previous content, much like a copy

	//                                                                                          exe   no_follow
	int chmod   (      CC* p,mode_t m      ) NE { HDR1(chmod   ,p,(  p,m  )) ; Chmod r{   p ,EXE(m),false   ,Comment::Cchmod   } ; return r(orig(  p,m  )) ; }
	int fchmodat(int d,CC* p,mode_t m,int f) NE { HDR1(fchmodat,p,(d,p,m,f)) ; Chmod r{{d,p},EXE(m),ASLNF(f),Comment::Cfchmodat} ; return r(orig(d,p,m,f)) ; }

	// clone
	// cf fork about why this wrapper is necessary
	static int (*_clone_fn)(void*) ;       // variable to hold actual function to call
	static int _call_clone_fn(void* arg) {
		SWEAR(!_t_loop) ;
		_g_mutex.unlock(MutexLvl::None) ;  // contrarily to fork, clone does not proceed but calls a function and the lock must be released in both parent and child (we are the only thread here)
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
		if ( _t_loop || !started() || flags&CLONE_VM ) return (*orig)(fn,stack,flags,arg,parent_tid,tls,child_tid) ; // if flags contains CLONE_VM, lock is not duplicated : nothing to do
		NO_SERVER(clone) ;
		Lock lock{_g_mutex} ;                                                                                               // no need to set _t_loop as clone calls no other piggy-backed function
		_clone_fn = fn ;                                                                                                    // _g_mutex is held, so there is no risk of clash
		return (*orig)(_call_clone_fn,stack,flags,arg,parent_tid,tls,child_tid) ;
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
		if ( _t_loop || !started() || flags&CLONE_VM ) return (*orig)(fn,stack,stack_size,flags,arg,parent_tid,tls,child_tid) ; // cf clone
		Lock lock{_g_mutex} ;                                                                                                          // cf clone
		//
		NO_SERVER(__clone2) ;
		_clone_fn = fn ;                                                                                                               // cf clone
		return (*orig)(_call_clone_fn,stack,stack_size,flags,arg,parent_tid,tls,child_tid) ;
		}

	#if !IN_SERVER
		// close
		// close must be tracked as we must call hide
		// in case close is called with one our our fd's, we must hide somewhere else (unless in server)
		// note that although hide calls no syscall, auditor() can and we must manage errno
		int  close  (int fd) { HDR0(close  ,(fd)) ; Hide r{fd} ; return r(orig(fd)) ; }
		int  __close(int fd) { HDR0(__close,(fd)) ; Hide r{fd} ; return r(orig(fd)) ; }
		#if HAS_CLOSE_RANGE
			int  close_range(uint fd1,uint fd2,int f) NE { HDR0(close_range,(fd1,fd2,f)) ; Hide r{fd1,fd2,f} ; return r(orig(fd1,fd2,f)) ; }
		#endif
	#endif

	#if NEED_ELF
		// dlopen
		void* dlopen (          CC* p,int f) NE { HDR(dlopen ,!p||!*p,(   p,f)) ; Dlopen r{p,Comment::Cdlopen } ; return r(orig(   p,f)) ; } // we do not support tmp mapping for indirect ...
		void* dlmopen(Lmid_t lm,CC* p,int f) NE { HDR(dlmopen,!p||!*p,(lm,p,f)) ; Dlopen r{p,Comment::Cdlmopen} ; return r(orig(lm,p,f)) ; } // ... deps, so we can pass pth to orig
	#endif

	#if !IN_SERVER
		// dup2
		// in case dup2/3 is called with one our fd's, we must hide somewhere else (unless in server)
		int dup2  (int ofd,int nfd      ) NE { HDR0(dup2  ,(ofd,nfd  )) ; Hide r{nfd} ; return r(orig(ofd,nfd  )) ; }
		int dup3  (int ofd,int nfd,int f) NE { HDR0(dup3  ,(ofd,nfd,f)) ; Hide r{nfd} ; return r(orig(ofd,nfd,f)) ; }
		int __dup2(int ofd,int nfd      ) NE { HDR0(__dup2,(ofd,nfd  )) ; Hide r{nfd} ; return r(orig(ofd,nfd  )) ; }
	#endif

	#if NEED_ELF
		// env
		// only there to capture LD_LIBRARY_PATH before it is modified as man dlopen says it must be captured at program start, but we have no entry at program start
		// ld_audit does not need it and anyway captures LD_LIBRARY_PATH at startup
		int setenv  (const char *name , const char *value , int overwrite) { ORIG(setenv  ) ; get_ld_library_path() ; return (*orig)(name,value,overwrite) ; }
		int unsetenv(const char *name                                    ) { ORIG(unsetenv) ; get_ld_library_path() ; return (*orig)(name                ) ; }
		int putenv  (char *string                                        ) { ORIG(putenv  ) ; get_ld_library_path() ; return (*orig)(string              ) ; }
	#endif

	// execv
	// /!\ : exec* can be called from within a vfork
	// So we must ensure that the child fully clean locks and other protections before actually calling exec as we cannot clean up after the call (it usually does not return)
	// and its memory is shared with parent in that case.
	// In counterpart, exec* calls do not themselves call other libc functions, so we need no protection while they run.
	// execv*p cannot be simple as we do not know which file will be accessed
	#define HDR_EXEC(Exec,libcall,no_follow,path,envp) \
		ORIG(libcall) ;                                             \
		SWEAR(!_t_loop) ;                                           \
		if (started()) {                                            \
			NO_SERVER(libcall) ;                                    \
			SaveTloop sav_t_loop ;                                  \
			Lock      lock       { _g_mutex } ;                     \
			Exec( path , no_follow , envp , Comment::C##libcall ) ; \
		}
	//                                                                                                    no_follow
	int execv   (      CC* p,char* const argv[]                            ) NE { HDR_EXEC(Exec ,execv   ,false      ,               p ,environ) ; return orig(  p,argv          ) ; }
	int execve  (      CC* p,char* const argv[],char* const envp[]         ) NE { HDR_EXEC(Exec ,execve  ,false      ,               p ,envp   ) ; return orig(  p,argv,envp     ) ; }
	int execvp  (      CC* p,char* const argv[]                            ) NE { HDR_EXEC(Execp,execvp  ,false      ,               p ,environ) ; return orig(  p,argv          ) ; }
	int execvpe (      CC* p,char* const argv[],char* const envp[]         ) NE { HDR_EXEC(Execp,execvpe ,false      ,               p ,envp   ) ; return orig(  p,argv,envp     ) ; }
	int execveat(int d,CC* p,char* const argv[],char *const envp[],int flgs) NE { HDR_EXEC(Exec ,execveat,ASLNF(flgs),Record::Path(d,p),envp   ) ; return orig(d,p,argv,envp,flgs) ; }
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

	// fopen
	FILE* fopen    (CC* p,CC* m         ) { HDR1(fopen    ,p,(p,m   )) ; Fopen r{p,m,Comment::Cfopen    } ; return r(orig(p,m   )) ; }
	FILE* freopen  (CC* p,CC* m,FILE* fp) { HDR1(freopen  ,p,(p,m,fp)) ; Fopen r{p,m,Comment::Cfreopen  } ; return r(orig(p,m,fp)) ; }
	FILE* fopen64  (CC* p,CC* m         ) { HDR1(fopen64  ,p,(p,m   )) ; Fopen r{p,m,Comment::Cfopen64  } ; return r(orig(p,m   )) ; }
	FILE* freopen64(CC* p,CC* m,FILE* fp) { HDR1(freopen64,p,(p,m,fp)) ; Fopen r{p,m,Comment::Cfreopen64} ; return r(orig(p,m,fp)) ; }

	// fork
	// not recursively called by auditing code
	// /!\ lock is not strictly necessary, but we must beware of interaction between lock & fork : locks are duplicated
	//     if another thread has the lock while we fork => child will dead lock as it has the lock but not the thread
	//     a simple way to stay coherent is to take the lock before fork and to release it after both in parent & child
	// vfork does not duplicate its memory and need no special treatment (as with clone with the CLONE_VM flag)
	pid_t fork       (       ) NE { HDR0(fork       ,(   )) ; NO_SERVER(fork       ) ; return orig  (   ) ; }
	pid_t __fork     (       ) NE { HDR0(__fork     ,(   )) ; NO_SERVER(__fork     ) ; return orig  (   ) ; }
	pid_t __libc_fork(       ) NE { HDR0(__libc_fork,(   )) ; NO_SERVER(__libc_fork) ; return orig  (   ) ; }
	int   system     (CC* cmd)    { HDR0(system     ,(cmd)) ; NO_SERVER(system     ) ; return orig  (cmd) ; } // actually does a fork

	// link                                                                                                         no_follow
	int link  (       CC* op,       CC* np      ) NE { HDR2(link  ,op,np,(   op,   np  )) ; Lnk r{    op ,    np ,false   ,Comment::Clink  } ; return r(orig(   op,   np  )) ; }
	int linkat(int od,CC* op,int nd,CC* np,int f) NE { HDR2(linkat,op,np,(od,op,nd,np,f)) ; Lnk r{{od,op},{nd,np},ASLNF(f),Comment::Clinkat} ; return r(orig(od,op,nd,np,f)) ; }

	// mkdir
	int mkdir  (      CC* p,mode_t m) NE { HDR1(mkdir  ,p,(  p,m)) ; Mkdir r{   p ,Comment::Cmkdirat} ; return r(orig(  p,m)) ; }
	int mkdirat(int d,CC* p,mode_t m) NE { HDR1(mkdirat,p,(d,p,m)) ; Mkdir r{{d,p},Comment::Cmkdir  } ; return r(orig(d,p,m)) ; }

	// mkstemp
	int mkstemp    (char* t             ) { HDR0(mkstemp    ,(t     )) ; Mkstemp r{t,   Comment::Cmkstemp    } ; return r(orig(t     )) ; }
	int mkostemp   (char* t,int f       ) { HDR0(mkostemp   ,(t,f   )) ; Mkstemp r{t,   Comment::Cmkostemp   } ; return r(orig(t,f   )) ; }
	int mkstemps   (char* t,      int sl) { HDR0(mkstemps   ,(t,  sl)) ; Mkstemp r{t,sl,Comment::Cmkstemps   } ; return r(orig(t,  sl)) ; }
	int mkostemps  (char* t,int f,int sl) { HDR0(mkostemps  ,(t,f,sl)) ; Mkstemp r{t,sl,Comment::Cmkostemps  } ; return r(orig(t,f,sl)) ; }
	int mkstemp64  (char* t             ) { HDR0(mkstemp64  ,(t     )) ; Mkstemp r{t,   Comment::Cmkstemp64  } ; return r(orig(t     )) ; }
	int mkostemp64 (char* t,int f       ) { HDR0(mkostemp64 ,(t,f   )) ; Mkstemp r{t,   Comment::Cmkostemp64 } ; return r(orig(t,f   )) ; }
	int mkstemps64 (char* t,      int sl) { HDR0(mkstemps64 ,(t,  sl)) ; Mkstemp r{t,sl,Comment::Cmkstemps64 } ; return r(orig(t,  sl)) ; }
	int mkostemps64(char* t,int f,int sl) { HDR0(mkostemps64,(t,f,sl)) ; Mkstemp r{t,sl,Comment::Cmkostemps64} ; return r(orig(t,f,sl)) ; }

	// mount
	int mount(CC* sp,CC* tp,CC* fst,ulong f,const void* d) {
		HDR( mount , !(f&MS_BIND) || (Record::s_is_simple(sp)&&Record::s_is_simple(tp)) , (sp,tp,fst,f,d) ) ;
		Mount r{sp,tp,Comment::Cmount} ;
		return r(orig(sp,tp,fst,f,d)) ;
		}

	// name_to_handle_at
	int name_to_handle_at( int d , CC* p , struct ::file_handle *h , int *mount_id , int f ) NE {
		HDR1( name_to_handle_at , p , (d,p,h,mount_id,f) ) ;
		Open r{{d,p},f,Comment::Cname_to_handle_at} ;
		return r(orig(d,p,h,mount_id,f)) ;
	}

	// open
	static_assert( ::is_unsigned_v<mode_t> && sizeof(mode_t)<=sizeof(uint) ) ;
	#define MOD mode_t m = 0 ; if ( f & (O_CREAT|O_TMPFILE) ) { va_list lst ; va_start(lst,f) ; m = mode_t(va_arg(lst,uint)) ; va_end(lst) ; }
	//
	int open             (      CC* p,int f,...) { MOD ; HDR1(open             ,p,(  p,f,m)) ; Open r{   p ,f                         ,Comment::Copen             } ; return r(orig(  p,f,m)) ; }
	int __open           (      CC* p,int f,...) { MOD ; HDR1(__open           ,p,(  p,f,m)) ; Open r{   p ,f                         ,Comment::C__open           } ; return r(orig(  p,f,m)) ; }
	int __open_nocancel  (      CC* p,int f,...) { MOD ; HDR1(__open_nocancel  ,p,(  p,f,m)) ; Open r{   p ,f                         ,Comment::C__open_nocancel  } ; return r(orig(  p,f,m)) ; }
	int __open_2         (      CC* p,int f    ) {       HDR1(__open_2         ,p,(  p,f  )) ; Open r{   p ,f                         ,Comment::C__open_2         } ; return r(orig(  p,f  )) ; }
	int openat           (int d,CC* p,int f,...) { MOD ; HDR1(openat           ,p,(d,p,f,m)) ; Open r{{d,p},f                         ,Comment::Copenat           } ; return r(orig(d,p,f,m)) ; }
	int __openat_2       (int d,CC* p,int f    ) {       HDR1(__openat_2       ,p,(d,p,f  )) ; Open r{{d,p},f                         ,Comment::C__openat_2       } ; return r(orig(d,p,f  )) ; }
	int creat            (      CC* p,mode_t m ) {       HDR1(creat            ,p,(  p,  m)) ; Open r{   p ,(O_CREAT|O_WRONLY|O_TRUNC),Comment::Ccreat            } ; return r(orig(  p,  m)) ; }
	int open64           (      CC* p,int f,...) { MOD ; HDR1(open64           ,p,(  p,f,m)) ; Open r{   p ,f                         ,Comment::Copen64           } ; return r(orig(  p,f,m)) ; }
	int __open64         (      CC* p,int f,...) { MOD ; HDR1(__open64         ,p,(  p,f,m)) ; Open r{   p ,f                         ,Comment::C__open64         } ; return r(orig(  p,f,m)) ; }
	int __open64_nocancel(      CC* p,int f,...) { MOD ; HDR1(__open64_nocancel,p,(  p,f,m)) ; Open r{   p ,f                         ,Comment::C__open64_nocancel} ; return r(orig(  p,f,m)) ; }
	int __open64_2       (      CC* p,int f    ) {       HDR1(__open64_2       ,p,(  p,f  )) ; Open r{   p ,f                         ,Comment::C__open64_2       } ; return r(orig(  p,f  )) ; }
	int openat64         (int d,CC* p,int f,...) { MOD ; HDR1(openat64         ,p,(d,p,f,m)) ; Open r{{d,p},f                         ,Comment::Copenat64         } ; return r(orig(d,p,f,m)) ; }
	int __openat64_2     (int d,CC* p,int f    ) {       HDR1(__openat64_2     ,p,(d,p,f  )) ; Open r{{d,p},f                         ,Comment::C__openat64_2     } ; return r(orig(d,p,f  )) ; }
	int creat64          (      CC* p,mode_t m ) {       HDR1(creat64          ,p,(  p,  m)) ; Open r{   p ,(O_CREAT|O_WRONLY|O_TRUNC),Comment::Ccreat64          } ; return r(orig(  p,  m)) ; }
	#undef MOD
	DIR* opendir(CC* p) { HDR1(opendir,p,(p)) ; Solve r{p,true/*no_follow*/,false/*read*/,false/*create*/,Comment::Copendir  } ; return r(orig(p)) ; }

	// readlink
	#define RL Readlink
	#if LD_PRELOAD_JEMALLOC
		// jemalloc does a readlink of its config file (/etc/jemalloc.conf) during its init phase
		// under some circumstances (not really understood), dlsym, which is necessary to find the original readlink function calls malloc
		// this creates a loop, leading to a deadlock in jemalloc as it takes a mutex during its init phase
		// this is a horible hack to avoid calling dlsym : readlink is redirected to __readlink_chk (which is, thus, left unprotected)
		// once init phase is passed, we proceed normally
		ssize_t readlink(CC* p,char* b,size_t sz) NE {
			if (!started()) return __readlink_chk(p,b,sz,sz) ;
			HDR1(readlink,p,(p,b,sz)) ; RL r{p ,b,sz,Comment::Creadlink} ; return r(orig(p,b,sz)) ;
		}
	#else
		ssize_t readlink      (CC* p,char* b,size_t sz           ) NE { HDR1(readlink      ,p,(p,b,sz    )) ; RL r{p ,b,sz,Comment::Creadlink       } ; return r(orig(p,b,sz    )) ; }
		ssize_t __readlink_chk(CC* p,char* b,size_t sz,size_t bsz) NE { HDR1(__readlink_chk,p,(p,b,sz,bsz)) ; RL r{p ,b,sz,Comment::C__readlink__chk} ; return r(orig(p,b,sz,bsz)) ; }
	#endif
	ssize_t readlinkat      (int d,CC* p,char* b,size_t sz           ) NE { HDR1(readlinkat      ,p,(d,p,b,sz    )) ; RL r{{d,p},b,sz,Comment::Creadlinkat      } ; return r(orig(d,p,b,sz    )) ; }
	ssize_t __readlinkat_chk(int d,CC* p,char* b,size_t sz,size_t bsz) NE { HDR1(__readlinkat_chk,p,(d,p,b,sz,bsz)) ; RL r{{d,p},b,sz,Comment::C__readlinkat_chk} ; return r(orig(d,p,b,sz,bsz)) ; }
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
	#endif //!                                                                                                             exchange no_replace
	int rename   (       CC* op,       CC* np       ) NE { HDR2(rename   ,op,np,(   op,   np  )) ; Rename r{    op ,    np ,false  ,false    ,Comment::Crename   } ; return r(orig(   op,   np  )) ; }
	int renameat (int od,CC* op,int nd,CC* np       ) NE { HDR2(renameat ,op,np,(od,op,nd,np  )) ; Rename r{{od,op},{nd,np},false  ,false    ,Comment::Crenameat } ; return r(orig(od,op,nd,np  )) ; }
	int renameat2(int od,CC* op,int nd,CC* np,uint f) NE { HDR2(renameat2,op,np,(od,op,nd,np,f)) ; Rename r{{od,op},{nd,np},REXC(f),RNR(f)   ,Comment::Crenameat2} ; return r(orig(od,op,nd,np,f)) ; }
	#undef RNR
	#undef REXC

	// rmdir
	int rmdir(CC* p) NE { HDR1(rmdir,p,(p)) ; Unlnk r{p,true/*rmdir*/,Comment::Crmdir} ; return r(orig(p)) ; }

	// symlink
	int symlink  (CC* t,      CC* p) NE { HDR1(symlink  ,p,(t,  p)) ; Symlink r{   p ,Comment::Csymlink  } ; return r(orig(t,  p)) ; }
	int symlinkat(CC* t,int d,CC* p) NE { HDR1(symlinkat,p,(t,d,p)) ; Symlink r{{d,p},Comment::Csymlinkat} ; return r(orig(t,d,p)) ; }

	// truncate
	int truncate  (CC* p,off_t   l) NE { HDR1(truncate  ,p,(p,l)) ; Open r{p,l?O_RDWR:O_WRONLY,Comment::Ctruncate  } ; return r(orig(p,l)) ; }
	int truncate64(CC* p,off64_t l) NE { HDR1(truncate64,p,(p,l)) ; Open r{p,l?O_RDWR:O_WRONLY,Comment::Ctruncate64} ; return r(orig(p,l)) ; }

	// unlink
	int unlink  (      CC* p      ) NE { HDR1(unlink  ,p,(  p  )) ; Unlnk r{   p ,false/*rmdir*/      ,Comment::Cunlink  } ; return r(orig(  p  )) ; }
	int unlinkat(int d,CC* p,int f) NE { HDR1(unlinkat,p,(d,p,f)) ; Unlnk r{{d,p},bool(f&AT_REMOVEDIR),Comment::Cunlinkat} ; return r(orig(d,p,f)) ; }

	// utime                                                                                                 no_follow read  create
	int utime    (      CC* p,const struct utimbuf* t         ) { HDR1(utime    ,p,(  p,t  )) ; Solve r{   p ,false   ,false,false,Comment::Cutime    } ; return r(orig(  p,t  )) ; }
	int utimes   (      CC* p,const struct timeval  t[2]      ) { HDR1(utimes   ,p,(  p,t  )) ; Solve r{   p ,false   ,false,false,Comment::Cutimes   } ; return r(orig(  p,t  )) ; }
	int futimesat(int d,CC* p,const struct timeval  t[2]      ) { HDR1(futimesat,p,(d,p,t  )) ; Solve r{{d,p},false   ,false,false,Comment::Cfutimesat} ; return r(orig(d,p,t  )) ; }
	int lutimes  (      CC* p,const struct timeval  t[2]      ) { HDR1(lutimes  ,p,(  p,t  )) ; Solve r{   p ,true    ,false,false,Comment::Clutimes  } ; return r(orig(  p,t  )) ; }
	int utimensat(int d,CC* p,const struct timespec t[2],int f) { HDR1(utimensat,p,(d,p,t,f)) ; Solve r{{d,p},ASLNF(f),false,false,Comment::Cutimensat} ; return r(orig(d,p,t,f)) ; }

	// mere path accesses (neeed to solve path, but no actual access to file data)
	#define ACCESSES(msk) ( (msk)&X_OK ? Accesses(Access::Reg) : Accesses(Access::Stat) )
	//                                                                                    no_follow accesses
	int access   (      CC* p,int m      ) NE { HDR1(access   ,p,(  p,m  )) ; Stat r{   p ,false   ,ACCESSES(m),Comment::Caccess   } ; return r(orig(  p,m  )) ; }
	int faccessat(int d,CC* p,int m,int f) NE { HDR1(faccessat,p,(d,p,m,f)) ; Stat r{{d,p},ASLNF(f),ACCESSES(m),Comment::Cfaccessat} ; return r(orig(d,p,m,f)) ; }
	#undef ACCESSES
	// stat* accesses provide the size field, which make the user sensitive to file content
	//                                                                                                             no_follow accesses
	int __xstat     (int v,      CC* p,struct stat  * b      ) NE { HDR1(__xstat     ,p,(v,  p,b  )) ; Stat r{   p ,false   ,~Accesses(),Comment::C__xstat     } ; return r(orig(v,  p,b  )) ; }
	int __lxstat    (int v,      CC* p,struct stat  * b      ) NE { HDR1(__lxstat    ,p,(v,  p,b  )) ; Stat r{   p ,true    ,~Accesses(),Comment::C__lxstat    } ; return r(orig(v,  p,b  )) ; }
	int __fxstatat  (int v,int d,CC* p,struct stat  * b,int f) NE { HDR1(__fxstatat  ,p,(v,d,p,b,f)) ; Stat r{{d,p},ASLNF(f),~Accesses(),Comment::C__fxstatat  } ; return r(orig(v,d,p,b,f)) ; }
	int stat        (            CC* p,struct stat  * b      ) NE { HDR1(stat        ,p,(    p,b  )) ; Stat r{   p ,false   ,~Accesses(),Comment::Cstat        } ; return r(orig(    p,b  )) ; }
	int lstat       (            CC* p,struct stat  * b      ) NE { HDR1(lstat       ,p,(    p,b  )) ; Stat r{   p ,true    ,~Accesses(),Comment::Clstat       } ; return r(orig(    p,b  )) ; }
	int fstatat     (      int d,CC* p,struct stat  * b,int f) NE { HDR1(fstatat     ,p,(  d,p,b,f)) ; Stat r{{d,p},ASLNF(f),~Accesses(),Comment::Cfstatat     } ; return r(orig(  d,p,b,f)) ; }
	int __xstat64   (int v,      CC* p,struct stat64* b      ) NE { HDR1(__xstat64   ,p,(v,  p,b  )) ; Stat r{   p ,false   ,~Accesses(),Comment::C__xstat64   } ; return r(orig(v,  p,b  )) ; }
	int __lxstat64  (int v,      CC* p,struct stat64* b      ) NE { HDR1(__lxstat64  ,p,(v,  p,b  )) ; Stat r{   p ,true    ,~Accesses(),Comment::C__lxstat64  } ; return r(orig(v,  p,b  )) ; }
	int __fxstatat64(int v,int d,CC* p,struct stat64* b,int f) NE { HDR1(__fxstatat64,p,(v,d,p,b,f)) ; Stat r{{d,p},ASLNF(f),~Accesses(),Comment::C__fxstatat64} ; return r(orig(v,d,p,b,f)) ; }
	int stat64      (            CC* p,struct stat64* b      ) NE { HDR1(stat64      ,p,(    p,b  )) ; Stat r{   p ,false   ,~Accesses(),Comment::Cstat64      } ; return r(orig(    p,b  )) ; }
	int lstat64     (            CC* p,struct stat64* b      ) NE { HDR1(lstat64     ,p,(    p,b  )) ; Stat r{   p ,true    ,~Accesses(),Comment::Clstat64     } ; return r(orig(    p,b  )) ; }
	int fstatat64   (      int d,CC* p,struct stat64* b,int f) NE { HDR1(fstatat64   ,p,(  d,p,b,f)) ; Stat r{{d,p},ASLNF(f),~Accesses(),Comment::Cfstatat64   } ; return r(orig(  d,p,b,f)) ; }
	//
	int statx(int d,CC* p,int f,uint msk,struct statx* b) NE {                   // statx must exist even if statx is not supported by the system as it appears in ENUMERATE_LIBCALLS
		HDR1(statx,p,(d,p,f,msk,b)) ;
		#if defined(STATX_TYPE) && defined(STATX_SIZE) && defined(STATX_BLOCKS) && defined(STATX_MODE)
			Accesses a ;
			if      (msk&(STATX_TYPE|STATX_SIZE|STATX_BLOCKS)) a = ~Accesses() ; // user can distinguish all content
			else if (msk& STATX_MODE                         ) a = Access::Reg ; // user can distinguish executable files, which is part of crc for regular files
		#else
			Accesses a = ~Accesses() ;                                           // if access macros are not defined, be pessimistic
		#endif
		Stat r{{d,p},true/*no_follow*/,a,Comment::Cstatx} ;
		return r(orig(d,p,f,msk,b)) ;
	}

	// realpath                                                                                                    no_follow accesses
	char* realpath              (CC* p,char* rp          ) NE { HDR1(realpath              ,p,(p,rp   )) ; Stat r{p,false   ,Accesses(),Comment::Crealpath              } ; return r(orig(p,rp   )) ; }
	char* __realpath_chk        (CC* p,char* rp,size_t rl) NE { HDR1(__realpath_chk        ,p,(p,rp,rl)) ; Stat r{p,false   ,Accesses(),Comment::C__realpath_chk        } ; return r(orig(p,rp,rl)) ; }
	char* canonicalize_file_name(CC* p                   ) NE { HDR1(canonicalize_file_name,p,(p      )) ; Stat r{p,false   ,Accesses(),Comment::Ccanonicalize_file_name} ; return r(orig(p      )) ; }

	// scandir
	using NmLst = struct dirent***                                     ;
	using Fltr  = int (*)(const struct dirent*                       ) ;
	using Cmp   = int (*)(const struct dirent**,const struct dirent**) ;
	//                                                                                               no_follow read  create
	int scandir  (      CC* p,NmLst nl,Fltr f,Cmp c) { HDR1(scandir  ,p,(  p,nl,f,c)) ; Solve r{   p ,true    ,false,false,Comment::Cscandir  } ; return r(orig(  p,nl,f,c)) ; }
	int scandirat(int d,CC* p,NmLst nl,Fltr f,Cmp c) { HDR1(scandirat,p,(d,p,nl,f,c)) ; Solve r{{d,p},true    ,false,false,Comment::Cscandirat} ; return r(orig(d,p,nl,f,c)) ; }
	//
	using NmLst64 = struct dirent64***                                       ;
	using Fltr64  = int (*)(const struct dirent64*                         ) ;
	using Cmp64   = int (*)(const struct dirent64**,const struct dirent64**) ;
	//                                                                                                         no_follow read  create
	int scandir64  (      CC* p,NmLst64 nl,Fltr64 f,Cmp64 c) { HDR1(scandir64  ,p,(  p,nl,f,c)) ; Solve r{   p ,true    ,false,false,Comment::Cscandir64  } ; return r(orig(  p,nl,f,c)) ; }
	int scandirat64(int d,CC* p,NmLst64 nl,Fltr64 f,Cmp64 c) { HDR1(scandirat64,p,(d,p,nl,f,c)) ; Solve r{{d,p},true    ,false,false,Comment::Cscandirat64} ; return r(orig(d,p,nl,f,c)) ; }

	#undef CC

	// syscall
	// /!\ we must be very careful to avoid dead-lock :
	// - mutex calls futex management, which sometimes call syscall
	// - so filter on s_tab must be done before locking (in HDR)
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
		SyscallDescr::Tab const& tab   = SyscallDescr::s_tab                                       ;
		SyscallDescr      const& descr = n>=0&&n<SyscallDescr::NSyscalls ? tab[n] : NoSyscallDescr ; // protect against arbitrary invalid syscall numbers
		HDR(
			syscall
		,	!descr || (descr.filter&&Record::s_is_simple(reinterpret_cast<const char*>(args[descr.filter-1])))
		,	(n,args[0],args[1],args[2],args[3],args[4],args[5])
		) ;
		void*     descr_ctx = nullptr ;
		SaveErrno audit_ctx ;                                                                              // save user errno when required
		//               vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		if (descr.entry) descr.entry( descr_ctx , auditor() , 0/*pid*/ , args , descr.comment ) ;
		//               ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		audit_ctx.restore_errno() ;
		//         vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		long res = orig(n,args[0],args[1],args[2],args[3],args[4],args[5]) ;
		//         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		//                     vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		if (descr.exit) return descr.exit( descr_ctx , auditor() , 0/*pid*/ , res ) ;
		else            return res                                                  ;
		//                     ^^^
	}

	#undef NO_SERVER
	#undef HDR2
	#undef HDR1
	#undef HDR0
	#undef HDR

	#undef ORIG

	#undef EXE
	#undef ASLNF

	#pragma GCC diagnostic pop
}
#if LD_PRELOAD
	#pragma GCC visibility pop
#endif
