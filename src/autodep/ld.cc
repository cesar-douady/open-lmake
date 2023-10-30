// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <dirent.h>
#include <dlfcn.h>  // dlsym
#include <stdarg.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "disk.hh"
#include "lib.hh"
#include "record.hh"

#include "gather_deps.hh"

// compiled with flag -fvisibility=hidden : this is good for perf and with LD_PRELOAD, we do not polute application namespace

using namespace Disk ;

extern "C" {
	// the following functions are defined in libc not in #include's, so they may be called by application code
	extern int     __close          ( int fd                                                        )          ;
	extern int     __dup2           ( int oldfd , int newfd                                         ) noexcept ;
	extern pid_t   __fork           (                                                               ) noexcept ;
	extern pid_t   __libc_fork      (                                                               ) noexcept ;
	extern pid_t   __vfork          (                                                               ) noexcept ;
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
	// following syscalls may not be defined on all systems (but it does not hurt to redeclare them if they are already declared)
	extern int  close_range( uint fd1 , uint fd2 , int flgs                                                   ) noexcept ;
	extern void closefrom  ( int  fd1                                                                         ) noexcept ;
	extern int  execveat   ( int dirfd , const char* pth , char* const argv[] , char *const envp[] , int flgs ) noexcept ;
	extern int  faccessat2 ( int dirfd , const char* pth , int mod , int flgs                                 ) noexcept ;
	extern int  renameat2  ( int odfd  , const char* op  , int ndfd , const char* np , uint flgs              ) noexcept ;
	extern int  statx      ( int dirfd , const char* pth , int flgs , uint msk , struct statx* buf            ) noexcept ;
	#ifndef CLOSE_RANGE_CLOEXEC
		#define CLOSE_RANGE_CLOEXEC 0                                          // if not defined, close_range is not going to be used and we do not need this flag, just allow compilation
	#endif
}

// User program may have global variables whose cxtor/dxtor do accesses.
// In that case, they may come before our own Audit is constructed if declared global (in the case of LD_PRELOAD).
// To face this order problem, we declare our Audit as a static within a funciton which will be constructed upon first call.
// As all statics with cxtor/dxtor, we define it through new so as to avoid destruction during finalization.
static Record& auditer() {
	static Record* s_res = new Record ;
	return *s_res ;
}

template<class Action,bool NF=false,bool DP=false> struct AuditAction : Ctx,Action {
	using Path = Record::Path ;
	// cxtors & casts
	// errno must be protected from our auditing actions in cxtor and operator()
	// more specifically, errno must be the original one before the actual call to libc
	// and must be the one after the actual call to libc when auditing code finally leave
	// Ctx contains save_errno in its cxtor and restore_errno in its dxtor
	// so here, errno must be restored at the end of cxtor and saved at the beginning of operator()
	template<class... A> AuditAction (Path&& p ,          A&&... args) requires(!DP) : Action{auditer(),::move(p ),           ::forward<A>(args)... } { restore_errno() ; }
	template<class... A> AuditAction (Path&& p1,Path&& p2,A&&... args) requires( DP) : Action{auditer(),::move(p1),::move(p2),::forward<A>(args)... } { restore_errno() ; }
	// services
	template<class T> T operator()(            T res) requires(!NF) { save_errno() ; return Action::operator()(auditer(),       res              ) ; }
	template<class T> T operator()(            T res) requires( NF) { save_errno() ; return Action::operator()(auditer(),       res,get_no_file()) ; }
	template<class T> T operator()(bool has_fd,T res) requires(!NF) { save_errno() ; return Action::operator()(auditer(),has_fd,res              ) ; }
	template<class T> T operator()(bool has_fd,T res) requires( NF) { save_errno() ; return Action::operator()(auditer(),has_fd,res,get_no_file()) ; }
} ;
//                                          no_file,double_path
using ChDir   = AuditAction<Record::ChDir                      > ;
using Exec    = AuditAction<Record::Exec                       > ;
using Lnk     = AuditAction<Record::Lnk    ,true   ,true       > ;
using Open    = AuditAction<Record::Open   ,true               > ;
using Read    = AuditAction<Record::Read                       > ;
using ReadLnk = AuditAction<Record::ReadLnk                    > ;
using Rename  = AuditAction<Record::Rename ,true   ,true       > ;
using Search  = AuditAction<Record::Search                     > ;
using Solve   = AuditAction<Record::Solve                      > ;
using Stat    = AuditAction<Record::Stat   ,true               > ;
using SymLnk  = AuditAction<Record::SymLnk                     > ;
using Unlink  = AuditAction<Record::Unlink                     > ;

struct Fopen : AuditAction<Record::Open,true/*no_file*/> {
	using Base = AuditAction<Record::Open,true/*no_file*/> ;
	static int mk_flags(const char* mode) {
		bool a = false ;
		bool c = false ;
		bool p = false ;
		bool r = false ;
		bool w = false ;
		for( const char* m=mode ; *m && *m!=',' ; m++ )                        // after a ',', there is a css=xxx which we do not care about
			switch (*m) {
				case 'a' : a = true ; break ;
				case 'c' : c = true ; break ;
				case '+' : p = true ; break ;
				case 'r' : r = true ; break ;
				case 'w' : w = true ; break ;
				default : ;
			}
		if (a+r+w!=1) return O_PATH ;                                                         // error case   , no access
		if (c       ) return O_PATH ;                                                         // gnu extension, no access
		/**/          return ( p ? O_RDWR : r ? O_RDONLY : O_WRONLY ) | ( w ? O_TRUNC : 0 ) ; // normal posix
	}
	Fopen( const char* pth , const char* mode , ::string const& comment="fopen" ) : Base{ pth , mk_flags(mode) , to_string(comment,'.',mode) } {}
	FILE* operator()(FILE* fp) {
		Base::operator()( true/*has_fd*/ , fp?::fileno(fp):-1 ) ;
		return fp ;
	}
} ;

//
// SyscallDescr
//

static ::string get_str( pid_t , uint64_t val              ) { return {reinterpret_cast<const char*>(val)    } ; }
static ::string get_str( pid_t , uint64_t val , size_t len ) { return {reinterpret_cast<const char*>(val),len} ; }

template<class T> static T get( pid_t , uint64_t val ) {
	T res ;
	::memcpy( &res , reinterpret_cast<void const*>(val) , sizeof(T) ) ;
	return res ;
}

static void put_str( pid_t , uint64_t val , ::string const& str ) {
	::memcpy( reinterpret_cast<void*>(val) , str.data() , str.size() ) ;
}

#include "syscall.cc"

//
// Audited
//

#ifdef LD_PRELOAD
	// for this case, we want to hide libc functions so as to substitute the auditing functions to the regular functions
	#pragma GCC visibility push(default)                                       // force visibility of functions defined hereinafter, until the corresponding pop
	extern "C"
#endif
#ifdef LD_AUDIT
	// for this, we want to use private functions so auditing code can freely call the libc without having to deal with errno
	namespace Audited
#endif
{
	#define NE noexcept

	#define ORIG(syscall) static decltype(::syscall)* orig = reinterpret_cast<decltype(::syscall)*>(get_orig(#syscall))
	// cwd is implicitely accessed by mostly all syscalls, so we have to ensure mutual exclusion as cwd could change between actual access and path resolution in audit
	// hence we should use a shared lock when reading and an exclusive lock when chdir
	// however, we have to ensure exclusivity for lnk cache, so we end up to exclusive access anyway, so simpler to lock exclusively here
	// use short macros as lines are very long in defining audited calls to libc
	// protect against recusive calls
	// args must be in () e.g. HEADER1(unlink,path,(path))
	#define HEADER(syscall,args,cond) \
		ORIG(syscall) ;                                    \
		if ( Lock::t_busy() || (cond) ) return orig args ; \
		Lock lock
	// do a first check to see if it is obvious that nothing needs to be done
	#define HEADER0(syscall,            args) HEADER( syscall , args , false                                                    )
	#define HEADER1(syscall,path,       args) HEADER( syscall , args , Record::s_is_simple(path )                               )
	#define HEADER2(syscall,path1,path2,args) HEADER( syscall , args , Record::s_is_simple(path1) && Record::s_is_simple(path2) )

	#define CC   const char
	#define P(r) r.at,r.file
	#define A(r) r.at
	#define F(r)      r.file

	static constexpr int Cwd = Fd::Cwd ;

	// chdir
	// chdir cannot be simple as we must tell Record of the new cwd, which implies a modification
	int chdir (CC* pth) NE { HEADER1(chdir ,pth,(pth)) ; ChDir r{pth   } ; return r(orig(F(r))) ; } // /!\ chdir manipulates cwd, which mandates an exclusive lock
	int fchdir(int fd ) NE { HEADER0(fchdir,    (fd )) ; ChDir r{Fd(fd)} ; return r(orig(A(r))) ; } // .

	// close
	// close cannot be simple as we must call hide, which may make modifications
	// /!\ : close can be recursively called by auditing code
	// in case close is called with one our our fd's, we must hide somewhere else
	// Record::s_hide & s_hide_range are guaranteed syscall free, so no need to protect against errno
	int  close      (int  fd                   )    { HEADER0(close      ,(fd          )) ;                                  Record::s_hide      (fd     ) ; return orig(fd          ) ; }
	int  __close    (int  fd                   )    { HEADER0(__close    ,(fd          )) ;                                  Record::s_hide      (fd     ) ; return orig(fd          ) ; }
	int  close_range(uint fd1,uint fd2,int flgs) NE { HEADER0(close_range,(fd1,fd2,flgs)) ; if (!(flgs&CLOSE_RANGE_CLOEXEC)) Record::s_hide_range(fd1,fd2) ; return orig(fd1,fd2,flgs) ; }
	void closefrom  (int  fd1                  ) NE { HEADER0(closefrom  ,(fd1         )) ;                                  Record::s_hide_range(fd1    ) ; return orig(fd1         ) ; }

	// dlopen
	// dlopen cannot be simple as we do not know which file will be accessed
	// not recursively called by auditing code
	// for dlopen, we cannot transform access into real access as for other system calls as a lot of other directories may be searched (that we should do by the way)
	// When we do the correct interpretation, we can replace pth below by r.real to have a secure mecanism
	// XXX : do the full library search for dlopen/dlmopen (requires DT_RPATH & DT_RUNPATH interpretation)
	void* dlopen (             CC* pth , int fs ) NE { HEADER0(dlopen ,(   pth,fs)) ; Search r{pth,false/*exec*/,"LD_LIBRARY_PATH","dlopen" } ; return r(orig(   pth,fs)) ; }
	void* dlmopen( Lmid_t lm , CC* pth , int fs ) NE { HEADER0(dlmopen,(lm,pth,fs)) ; Search r{pth,false/*exec*/,"LD_LIBRARY_PATH","dlmopen"} ; return r(orig(lm,pth,fs)) ; }

	// dup2
	// /!\ : dup2/3 can be recursively called by auditing code
	// in case dup2/3 is called with one our fd's, we must hide somewhere else
	// Record::s_hide is guaranteed syscall free, so no need to protect against errno
	int dup2  ( int oldfd , int newfd            ) NE { HEADER0(dup2  ,(oldfd,newfd     )) ; Record::s_hide(newfd) ; return orig(oldfd,newfd     ) ; }
	int dup3  ( int oldfd , int newfd , int flgs ) NE { HEADER0(dup3  ,(oldfd,newfd,flgs)) ; Record::s_hide(newfd) ; return orig(oldfd,newfd,flgs) ; }
	int __dup2( int oldfd , int newfd            ) NE { HEADER0(__dup2,(oldfd,newfd     )) ; Record::s_hide(newfd) ; return orig(oldfd,newfd     ) ; }

	// execv
	// execv*p cannot be simple as we do not know which file will be accessed
	// exec does not support tmp mapping as this could require modifying file content along the interpreter path
	int execv  ( CC* pth , char* const argv[]                      ) NE { HEADER1(execv  ,pth,(pth,argv     )) ; Exec   r{pth,false/*no_follow*/ ,"execv"  } ; return r(orig(pth,argv     )) ; }
	int execve ( CC* pth , char* const argv[] , char* const envp[] ) NE { HEADER1(execve ,pth,(pth,argv,envp)) ; Exec   r{pth,false/*no_follow*/ ,"execve" } ; return r(orig(pth,argv,envp)) ; }
	int execvp ( CC* pth , char* const argv[]                      ) NE { HEADER0(execvp ,    (pth,argv     )) ; Search r{pth,true/*exec*/,"PATH","execvp" } ; return r(orig(pth,argv     )) ; }
	int execvpe( CC* pth , char* const argv[] , char* const envp[] ) NE { HEADER0(execvpe,    (pth,argv,envp)) ; Search r{pth,true/*exec*/,"PATH","execvpe"} ; return r(orig(pth,argv,envp)) ; }
	//
	int execveat( int dfd , CC* pth , char* const argv[] , char *const envp[] , int flgs ) NE {
		HEADER1(execveat,pth,(dfd,pth,argv,envp,flgs)) ;
		Exec r { {dfd,pth} , bool(flgs&AT_SYMLINK_NOFOLLOW) , "execveat" } ;
		return r(orig(dfd,pth,argv,envp,flgs)) ;
	}
	// execl
	#define MK_ARGS(end_action) \
		char*   cur         = const_cast<char*>(arg)            ;            \
		va_list arg_cnt_lst ; va_start(arg_cnt_lst,arg        ) ;            \
		va_list args_lst    ; va_copy (args_lst   ,arg_cnt_lst) ;            \
		int     arg_cnt     ;                                                \
		for( arg_cnt=0 ; cur ; arg_cnt++ ) cur = va_arg(arg_cnt_lst,char*) ; \
		char** args = new char*[arg_cnt+1] ;                                 \
		args[0] = const_cast<char*>(arg) ;                                   \
		for( int i=1 ; i<=arg_cnt ; i++ ) args[i] = va_arg(args_lst,char*) ; \
		end_action                                                           \
		va_end(arg_cnt_lst) ;                                                \
		va_end(args_lst   )
	int execl ( CC* pth , CC* arg , ... ) NE { MK_ARGS(                                               ) ; int rc = execv (pth,args     ) ; delete[] args ; return rc ; }
	int execle( CC* pth , CC* arg , ... ) NE { MK_ARGS( char* const* envp = va_arg(args_lst,char**) ; ) ; int rc = execve(pth,args,envp) ; delete[] args ; return rc ; }
	int execlp( CC* pth , CC* arg , ... ) NE { MK_ARGS(                                               ) ; int rc = execvp(pth,args     ) ; delete[] args ; return rc ; }
	#undef MK_ARGS

	// fopen
	FILE* fopen    ( CC* pth , CC* mode            ) { HEADER1(fopen    ,pth,(pth,mode   )) ; Fopen r{pth,mode,"fopen"    } ; return r(orig(F(r),mode   )) ; }
	FILE* fopen64  ( CC* pth , CC* mode            ) { HEADER1(fopen64  ,pth,(pth,mode   )) ; Fopen r{pth,mode,"fopen64"  } ; return r(orig(F(r),mode   )) ; }
	FILE* freopen  ( CC* pth , CC* mode , FILE* fp ) { HEADER1(freopen  ,pth,(pth,mode,fp)) ; Fopen r{pth,mode,"freopen"  } ; return r(orig(F(r),mode,fp)) ; }
	FILE* freopen64( CC* pth , CC* mode , FILE* fp ) { HEADER1(freopen64,pth,(pth,mode,fp)) ; Fopen r{pth,mode,"freopen64"} ; return r(orig(F(r),mode,fp)) ; }

	// fork
	// not recursively called by auditing code
	pid_t fork       () NE { HEADER0(fork       ,()) ; return orig()   ; }     // /!\ lock is not strictly necessary, but we must beware of interaction between lock & fork : locks are duplicated ...
	pid_t __fork     () NE { HEADER0(__fork     ,()) ; return orig()   ; }     //     imagine the lock is held by another thread while we fork => child will have dead lock                        ...
	pid_t __libc_fork() NE { HEADER0(__libc_fork,()) ; return orig()   ; }     //     a simple way to stay coherent is to take the lock before fork and to release it after both in parent & child
	pid_t vfork      () NE {                           return fork  () ; }     // mapped to fork as vfork prevents most actions before following exec and we need a clean semantic to instrument exec
	pid_t __vfork    () NE {                           return __fork() ; }     // .
	//
	int system(CC* cmd) { HEADER0(system,(cmd)) ; return orig(cmd) ; }         // cf fork for explanation as this syscall does fork

	// getcwd
	// cf man 3 getcwd (Linux)
	//                                                                                                                           buf_sz     sz  allocated
	char* getcwd              (char* buf,size_t sz) NE { HEADER0(getcwd              ,(buf,sz)) ; return fix_cwd( orig(buf,sz) , sz       , 0 , buf?No:sz?Maybe:Yes ).first ; }
	char* get_current_dir_name(                   ) NE { HEADER0(get_current_dir_name,(      )) ; return fix_cwd( orig(      ) , PATH_MAX , 0 , Yes                 ).first ; }
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
	char* getwd               (char* buf          ) NE { HEADER0(getwd               ,(buf   )) ; return fix_cwd( orig(buf   ) , PATH_MAX , 0                       ).first ; }
	#pragma GCC diagnostic pop

	// link
	int link  (       CC* op,       CC* np      ) NE { HEADER2(link  ,op,np,(   op,   np  )) ; Lnk r{    op ,    np   } ; return r(orig(F(r.src),F(r.dst)  )) ; }
	int linkat(int od,CC* op,int nd,CC* np,int f) NE { HEADER2(linkat,op,np,(od,op,nd,np,f)) ; Lnk r{{od,op},{nd,np},f} ; return r(orig(P(r.src),P(r.dst),f)) ; }

	#define O_CWT (O_CREAT|O_WRONLY|O_TRUNC)
	// in case of success, tmpl is modified to contain the file that was actually opened
	#define MKSTEMP(syscall,tmpl,sfx_len,args) \
		HEADER0(syscall,args) ;                                                                                \
		Solve r  { tmpl , true/*no_follow*/ } ;                                                                \
		int   fd = r(orig args)               ;                                                                \
		if (F(r)!=tmpl) ::memcpy( tmpl+strlen(tmpl)-sfx_len-6 , F(r)+strlen(F(r))-sfx_len-6 , 6 ) ;            \
		if (fd>=0     ) Record::Open(auditer(),F(r),O_CWT|O_NOFOLLOW,"mkstemp")(auditer(),true/*has_fd*/,fd) ; \
		return fd
	int mkostemp   ( char* tmpl , int flags               ) { MKSTEMP( mkostemp    , tmpl , 0       , (tmpl,flags        ) ) ; }
	int mkostemp64 ( char* tmpl , int flags               ) { MKSTEMP( mkostemp64  , tmpl , 0       , (tmpl,flags        ) ) ; }
	int mkostemps  ( char* tmpl , int flags , int sfx_len ) { MKSTEMP( mkostemps   , tmpl , sfx_len , (tmpl,flags,sfx_len) ) ; }
	int mkostemps64( char* tmpl , int flags , int sfx_len ) { MKSTEMP( mkostemps64 , tmpl , sfx_len , (tmpl,flags,sfx_len) ) ; }
	int mkstemp    ( char* tmpl                           ) { MKSTEMP( mkstemp     , tmpl , 0       , (tmpl              ) ) ; }
	int mkstemp64  ( char* tmpl                           ) { MKSTEMP( mkstemp64   , tmpl , 0       , (tmpl              ) ) ; }
	int mkstemps   ( char* tmpl ,             int sfx_len ) { MKSTEMP( mkstemps    , tmpl , sfx_len , (tmpl,      sfx_len) ) ; }
	int mkstemps64 ( char* tmpl ,             int sfx_len ) { MKSTEMP( mkstemps64  , tmpl , sfx_len , (tmpl,      sfx_len) ) ; }
	#undef MKSTEMP

	// open
	#define MOD mode_t m = 0 ; if ( f & (O_CREAT|O_TMPFILE) ) { va_list lst ; va_start(lst,f) ; m = va_arg(lst,mode_t) ; va_end(lst) ; }
	//                                                                                                                                                       has_fd
	int open             (         CC* p , int f , ... ) { MOD ; HEADER1(open             ,p,(  p,f,m)) ; Open r{   p ,f    ,"open"             } ; return r(true  ,orig(F(r),f,m)) ; }
	int __open           (         CC* p , int f , ... ) { MOD ; HEADER1(__open           ,p,(  p,f,m)) ; Open r{   p ,f    ,"__open"           } ; return r(true  ,orig(F(r),f,m)) ; }
	int __open_nocancel  (         CC* p , int f , ... ) { MOD ; HEADER1(__open_nocancel  ,p,(  p,f,m)) ; Open r{   p ,f    ,"__open_nocancel"  } ; return r(true  ,orig(F(r),f,m)) ; }
	int __open_2         (         CC* p , int f       ) {       HEADER1(__open_2         ,p,(  p,f  )) ; Open r{   p ,f    ,"__open_2"         } ; return r(true  ,orig(F(r),f  )) ; }
	int open64           (         CC* p , int f , ... ) { MOD ; HEADER1(open64           ,p,(  p,f,m)) ; Open r{   p ,f    ,"open64"           } ; return r(true  ,orig(F(r),f,m)) ; }
	int __open64         (         CC* p , int f , ... ) { MOD ; HEADER1(__open64         ,p,(  p,f,m)) ; Open r{   p ,f    ,"__open64"         } ; return r(true  ,orig(F(r),f,m)) ; }
	int __open64_nocancel(         CC* p , int f , ... ) { MOD ; HEADER1(__open64_nocancel,p,(  p,f,m)) ; Open r{   p ,f    ,"__open64_nocancel"} ; return r(true  ,orig(F(r),f,m)) ; }
	int __open64_2       (         CC* p , int f       ) {       HEADER1(__open64_2       ,p,(  p,f  )) ; Open r{   p ,f    ,"__open64_2"       } ; return r(true  ,orig(F(r),f  )) ; }
	int openat           ( int d , CC* p , int f , ... ) { MOD ; HEADER1(openat           ,p,(d,p,f,m)) ; Open r{{d,p},f    ,"openat"           } ; return r(true  ,orig(P(r),f,m)) ; }
	int __openat_2       ( int d , CC* p , int f       ) {       HEADER1(__openat_2       ,p,(d,p,f  )) ; Open r{{d,p},f    ,"__openat_2"       } ; return r(true  ,orig(P(r),f  )) ; }
	int openat64         ( int d , CC* p , int f , ... ) { MOD ; HEADER1(openat64         ,p,(d,p,f,m)) ; Open r{{d,p},f    ,"openat64"         } ; return r(true  ,orig(P(r),f,m)) ; }
	int __openat64_2     ( int d , CC* p , int f       ) {       HEADER1(__openat64_2     ,p,(d,p,f  )) ; Open r{{d,p},f    ,"__openat64_2"     } ; return r(true  ,orig(P(r),f  )) ; }
	int creat            (         CC* p , mode_t m    ) {       HEADER1(creat            ,p,(  p,  m)) ; Open r{   p ,O_CWT,"creat"            } ; return r(true  ,orig(F(r),  m)) ; }
	int creat64          (         CC* p , mode_t m    ) {       HEADER1(creat64          ,p,(  p,  m)) ; Open r{   p ,O_CWT,"creat64"          } ; return r(true  ,orig(F(r),  m)) ; }
	#undef MOD
	//
	int name_to_handle_at( int dirfd , CC* pth , struct ::file_handle *h , int *mount_id , int flgs ) NE {
		HEADER1(name_to_handle_at,pth,(dirfd,pth,h,mount_id,flgs)) ;
		Open r{{dirfd,pth},flgs,"name_to_handle_at"} ;
		return r(false/*has_fd*/,orig(P(r),h,mount_id,flgs)) ;
	}
	#undef O_CWT

	// readlink
	ssize_t readlink        (      CC* p,char* b,size_t sz           ) NE { HEADER1(readlink        ,p,(  p,b,sz    )) ; ReadLnk r{   p ,b,sz} ; return r(orig(F(r),b,sz    )) ; }
	ssize_t __readlink_chk  (      CC* p,char* b,size_t sz,size_t bsz) NE { HEADER1(__readlink_chk  ,p,(  p,b,sz,bsz)) ; ReadLnk r{   p ,b,sz} ; return r(orig(F(r),b,sz,bsz)) ; }
	ssize_t __readlinkat_chk(int d,CC* p,char* b,size_t sz,size_t bsz) NE { HEADER1(__readlinkat_chk,p,(d,p,b,sz,bsz)) ; ReadLnk r{{d,p},b,sz} ; return r(orig(P(r),b,sz,bsz)) ; }
	ssize_t readlinkat(int d,CC* p,char* b,size_t sz) NE {
		HEADER1(readlinkat,p,(d,p,b,sz)) ;
		if (d==Backdoor.fd) {                         return auditer().backdoor(p,b,sz) ; }
		else                { ReadLnk r{{d,p},b,sz} ; return r(orig(P(r),b,sz))         ; }
	}

	// rename
	int rename   (       CC* op,       CC* np       ) NE { HEADER2(rename   ,op,np,(   op,   np  )) ; Rename r{    op ,    np ,0u,"rename"   } ; return r(orig(F(r.src),F(r.dst)  )) ; }
	int renameat (int od,CC* op,int nd,CC* np       ) NE { HEADER2(renameat ,op,np,(od,op,nd,np  )) ; Rename r{{od,op},{nd,np},0u,"renameat" } ; return r(orig(P(r.src),P(r.dst)  )) ; }
	int renameat2(int od,CC* op,int nd,CC* np,uint f) NE { HEADER2(renameat2,op,np,(od,op,nd,np,f)) ; Rename r{{od,op},{nd,np},f ,"renameat2"} ; return r(orig(P(r.src),P(r.dst),f)) ; }

	// symlink
	int symlink  ( CC* target ,             CC* pth ) NE { HEADER1(symlink  ,pth,(target,      pth)) ; SymLnk r{       pth ,"symlink"  } ; return r(orig(target,F(r))) ; }
	int symlinkat( CC* target , int dirfd , CC* pth ) NE { HEADER1(symlinkat,pth,(target,dirfd,pth)) ; SymLnk r{{dirfd,pth},"symlinkat"} ; return r(orig(target,P(r))) ; }

	// truncate
	int truncate  (CC* pth,off_t len) NE { HEADER1(truncate  ,pth,(pth,len)) ; Open r{pth,len?O_RDWR:O_WRONLY,"truncate"  } ; return r(false/*has_fd*/,orig(F(r),len)) ; }
	int truncate64(CC* pth,off_t len) NE { HEADER1(truncate64,pth,(pth,len)) ; Open r{pth,len?O_RDWR:O_WRONLY,"truncate64"} ; return r(false/*has_fd*/,orig(F(r),len)) ; }

	// unlink
	int unlink  (             CC* pth             ) NE { HEADER1(unlink  ,pth,(      pth      )) ; Unlink r{       pth ,false/*remove_dir*/     ,"unlink"  } ; return r(orig(F(r)      )) ; }
	int unlinkat( int dirfd , CC* pth , int flags ) NE { HEADER1(unlinkat,pth,(dirfd,pth,flags)) ; Unlink r{{dirfd,pth},bool(flags&AT_REMOVEDIR),"unlinkat"} ; return r(orig(P(r),flags)) ; }

	// mere path accesses (neeed to solve path, but no actual access to file data)
	#define ASLM AT_SYMLINK_NOFOLLOW
	//                                                                                          no_follow
	int  access   (      CC* p,int m      ) NE { HEADER1(access   ,p,(  p,m  )) ; Stat  r{   p ,false       ,"access"   } ; return r(orig(F(r),m  )) ; }
	int  faccessat(int d,CC* p,int m,int f) NE { HEADER1(faccessat,p,(d,p,m,f)) ; Stat  r{{d,p},bool(f&ASLM),"faccessat"} ; return r(orig(P(r),m,f)) ; }
	DIR* opendir  (      CC* p            )    { HEADER1(opendir  ,p,(  p    )) ; Solve r{   p ,true                    } ; return r(orig(F(r)    )) ; }
	int  rmdir    (      CC* p            ) NE { HEADER1(rmdir    ,p,(  p    )) ; Solve r{   p ,true                    } ; return r(orig(F(r)    )) ; }
	//                                                                                                                 no_follow
	int __xstat     (int v,      CC* p,struct stat  * b      ) NE { HEADER1(__xstat     ,p,(v,  p,b  )) ; Stat r{   p ,false       ,"__xstat"     } ; return r(orig(v,F(r),b  )) ; }
	int __xstat64   (int v,      CC* p,struct stat64* b      ) NE { HEADER1(__xstat64   ,p,(v,  p,b  )) ; Stat r{   p ,false       ,"__xstat64"   } ; return r(orig(v,F(r),b  )) ; }
	int __lxstat    (int v,      CC* p,struct stat  * b      ) NE { HEADER1(__lxstat    ,p,(v,  p,b  )) ; Stat r{   p ,true        ,"__lxstat"    } ; return r(orig(v,F(r),b  )) ; }
	int __lxstat64  (int v,      CC* p,struct stat64* b      ) NE { HEADER1(__lxstat64  ,p,(v,  p,b  )) ; Stat r{   p ,true        ,"__lxstat64"  } ; return r(orig(v,F(r),b  )) ; }
	int __fxstatat  (int v,int d,CC* p,struct stat  * b,int f) NE { HEADER1(__fxstatat  ,p,(v,d,p,b,f)) ; Stat r{{d,p},bool(f&ASLM),"__fxstatat"  } ; return r(orig(v,P(r),b,f)) ; }
	int __fxstatat64(int v,int d,CC* p,struct stat64* b,int f) NE { HEADER1(__fxstatat64,p,(v,d,p,b,f)) ; Stat r{{d,p},bool(f&ASLM),"__fxstatat64"} ; return r(orig(v,P(r),b,f)) ; }
	#if !NEED_STAT_WRAPPERS
		//                                                                                                   no_follow
		int stat     (      CC* p,struct stat  * b      ) NE { HEADER1(stat     ,p,(  p,b  )) ; Stat r{   p ,false       ,"stat"     } ; return r(orig(F(r),b  )) ; }
		int stat64   (      CC* p,struct stat64* b      ) NE { HEADER1(stat64   ,p,(  p,b  )) ; Stat r{   p ,false       ,"stat64"   } ; return r(orig(F(r),b  )) ; }
		int lstat    (      CC* p,struct stat  * b      ) NE { HEADER1(lstat    ,p,(  p,b  )) ; Stat r{   p ,true        ,"lstat"    } ; return r(orig(F(r),b  )) ; }
		int lstat64  (      CC* p,struct stat64* b      ) NE { HEADER1(lstat64  ,p,(  p,b  )) ; Stat r{   p ,true        ,"lstat64"  } ; return r(orig(F(r),b  )) ; }
		int fstatat  (int d,CC* p,struct stat  * b,int f) NE { HEADER1(fstatat  ,p,(d,p,b,f)) ; Stat r{{d,p},bool(f&ASLM),"fstatat"  } ; return r(orig(P(r),b,f)) ; }
		int fstatat64(int d,CC* p,struct stat64* b,int f) NE { HEADER1(fstatat64,p,(d,p,b,f)) ; Stat r{{d,p},bool(f&ASLM),"fstatat64"} ; return r(orig(P(r),b,f)) ; }
	#endif
	int statx(int d,CC* p,int f,uint msk,struct statx* b) NE { HEADER1(statx,p,(d,p,f,msk,b)) ; Stat r{{d,p},true/*no_follow*/,"statx"} ; return r(orig(P(r),f,msk,b)) ; }

	// realpath
	//                                                                                                                 no_follow
	char* realpath              (CC* p,char* rp          ) NE { HEADER1(realpath              ,p,(p,rp   )) ; Stat r{p,false    ,"realpath              "} ; return r(orig(F(r),rp   )) ; }
	char* __realpath_chk        (CC* p,char* rp,size_t rl) NE { HEADER1(__realpath_chk        ,p,(p,rp,rl)) ; Stat r{p,false    ,"__realpath_chk        "} ; return r(orig(F(r),rp,rl)) ; }
	char* canonicalize_file_name(CC* p                   ) NE { HEADER1(canonicalize_file_name,p,(p      )) ; Stat r{p,false    ,"canonicalize_file_name"} ; return r(orig(F(r)      )) ; }

	// mkdir
	//                                                                                no_follow
	int mkdir  (      CC* p,mode_t m) NE { HEADER1(mkdir  ,p,(  p,m)) ; Solve r{   p ,true     } ; return r(orig(F(r),m)) ; }
	int mkdirat(int d,CC* p,mode_t m) NE { HEADER1(mkdirat,p,(d,p,m)) ; Solve r{{d,p},true     } ; return r(orig(P(r),m)) ; }

	// scandir
	using NmLst   = struct dirent  ***                                       ;
	using NmLst64 = struct dirent64***                                       ;
	using Fltr    = int (*)(const struct dirent  *                         ) ;
	using Fltr64  = int (*)(const struct dirent64*                         ) ;
	using Cmp     = int (*)(const struct dirent**  ,const struct dirent  **) ;
	using Cmp64   = int (*)(const struct dirent64**,const struct dirent64**) ;
	//                                                                                                             no_follow
	int scandir    (      CC* p,NmLst   nl,Fltr   f,Cmp   c) { HEADER1(scandir    ,p,(  p,nl,f,c)) ; Solve r{   p ,true     } ; return r(orig(F(r),nl,f,c)) ; }
	int scandir64  (      CC* p,NmLst64 nl,Fltr64 f,Cmp64 c) { HEADER1(scandir64  ,p,(  p,nl,f,c)) ; Solve r{   p ,true     } ; return r(orig(F(r),nl,f,c)) ; }
	int scandirat  (int d,CC* p,NmLst   nl,Fltr   f,Cmp   c) { HEADER1(scandirat  ,p,(d,p,nl,f,c)) ; Solve r{{d,p},true     } ; return r(orig(P(r),nl,f,c)) ; }
	int scandirat64(int d,CC* p,NmLst64 nl,Fltr64 f,Cmp64 c) { HEADER1(scandirat64,p,(d,p,nl,f,c)) ; Solve r{{d,p},true     } ; return r(orig(P(r),nl,f,c)) ; }

	#undef P
	#undef CC

	#undef NE

	// syscall
	long syscall( long n , ... ) {                                             // XXX : support, or at least detect tmp mapping
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
		auto it = SyscallDescr::s_tab.find(n) ;
		HEADER( syscall , (n,args[0],args[1],args[2],args[3],args[4],args[5]) , it==SyscallDescr::s_tab.end() ) ;
		//
		SyscallDescr const& descr     = it->second ;
		void*               descr_ctx ;
		{
			[[maybe_unused]] Ctx audit_ctx ;                                                   // save user errno when required
			//          vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			bool skip = descr.entry( descr_ctx , auditer() , 0/*pid*/ , args , descr.comment ) ; // may modify args if tmp is mapped
			//          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			//
			if (skip) return -1 ;                                              // return an error, as it is done in the ptrace case
		}
		//         vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		long res = orig(n,args[0],args[1],args[2],args[3],args[4],args[5]) ;
		//         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		[[maybe_unused]] Ctx audit_ctx ;                                                  // save user errno when required
		//     vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		return descr.exit( descr_ctx , auditer() , 0/*pid*/ , res , np_syscall_errno(res) ) ;
		//     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	}

	#undef HEADER2
	#undef HEADER1
	#undef HEADER0
	#undef HEADER

	#undef ORIG

}
#ifdef LD_PRELOAD
	#pragma GCC visibility pop
#endif
