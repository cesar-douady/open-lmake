// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <dirent.h>
#include <stdarg.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "disk.hh"
#include "lib.hh"
#include "record.hh"

#include "autodep_ld.hh"
#include "gather_deps.hh"

// compiled with flag -fvisibility=hidden : this is good for perf and with LD_PRELOAD, we do not polute application namespace

using namespace Disk ;

bool g_force_orig = false ;

bool is_libc(const char* c_name) {
	// search for string (.*/)?libc.so(.<number>)*
	static const char LibC[] = "libc.so" ;
	//
	::string_view name { c_name } ;
	size_t        pos  = name.rfind(LibC) ;
	/**/                                            if ( pos==NPos                  ) return false ;
	/**/                                            if ( pos!=0 && name[pos-1]!='/' ) return false ;
	for( char c : name.substr(pos+sizeof(LibC)-1) ) if ( (c<'0'||c>'9') && c!='.'   ) return false ;
	/**/                                                                              return true  ;
}

static inline void* get_libc_handle_cooked() {
	void* res = get_libc_handle() ;
	if (!res) exit(2,"cannot use autodep method ld_audit or ld_preload with statically linked libc") ;
	return res ;
}
void* get_orig(const char* syscall) {
	static void* s_libc_handle = get_libc_handle_cooked() ;
	Save  save { g_force_orig , true } ;                                       // avoid loop during dlsym execution
	void* res  = ::dlsym(s_libc_handle,syscall) ;
	swear_prod(res,"cannot find symbol ",syscall," in libc") ;
	return res ;
}

extern "C" {
	// the following functions are defined in libc not in #include's, so they may be called by application code
	extern int     __close          ( int fd                                                        ) ;
	extern int     __dup2           ( int oldfd , int newfd                                         ) ;
	extern pid_t   __fork           (                                                               ) ;
	extern pid_t   __libc_fork      (                                                               ) ;
	extern pid_t   __vfork          (                                                               ) ;
	extern int     __open           (             const char* pth , int flgs , ...                  ) ;
	extern int     __open_nocancel  (             const char* pth , int flgs , ...                  ) ;
	extern int     __open_2         (             const char* pth , int flgs                        ) ;
	extern int     __open64         (             const char* pth , int flgs , ...                  ) ;
	extern int     __open64_nocancel(             const char* pth , int flgs , ...                  ) ;
	extern int     __open64_2       (             const char* pth , int flgs                        ) ;
	extern int     __openat_2       ( int dfd   , const char* pth , int flgs                        ) ;
	extern int     __openat64_2     ( int dfd   , const char* pth , int flgs                        ) ;
	extern ssize_t __readlink_chk   (             const char* pth , char*   , size_t l , size_t bsz ) ;
	extern ssize_t __readlinkat_chk ( int dfd   , const char* pth , char* b , size_t l , size_t bsz ) ;
	extern char*   __realpath_chk   (             const char* pth , char* rpth , size_t rlen        ) ;
	//
	extern int __xstat     ( int v ,           const char* pth , struct stat  * buf            ) ;
	extern int __xstat64   ( int v ,           const char* pth , struct stat64* buf            ) ;
	extern int __lxstat    ( int v ,           const char* pth , struct stat  * buf            ) ;
	extern int __lxstat64  ( int v ,           const char* pth , struct stat64* buf            ) ;
	extern int __fxstatat  ( int v , int dfd , const char* pth , struct stat  * buf , int flgs ) ;
	extern int __fxstatat64( int v , int dfd , const char* pth , struct stat64* buf , int flgs ) ;
	// following syscalls may not be defined on all systems (but it does not hurt to redeclare them if they are already declared)
	extern int  close_range( unsigned int fd1 , unsigned int fd2 , int flgs                                   ) noexcept ;
	extern void closefrom  ( int          fd1                                                                 ) noexcept ;
	extern int  execveat   ( int dirfd , const char* pth , char* const argv[] , char *const envp[] , int flgs ) noexcept ;
	extern int  faccessat2 ( int dirfd , const char* pth , int mod , int flgs                                 ) noexcept ;
	extern int  renameat2  ( int odfd  , const char* op  , int ndfd , const char* np , unsigned int flgs      ) noexcept ;
	extern int  statx      ( int dirfd , const char* pth , int flgs , unsigned int msk , struct statx* buf    ) noexcept ;
	#ifndef CLOSE_RANGE_CLOEXEC
		#define CLOSE_RANGE_CLOEXEC 0                                          // if not defined, close_range is not going to be used and we do not need this flag, just allow compilation
	#endif
}

//
// Audit
//

// User program may have global variables whose cxtor/dxtor do accesses.
// In that case, they may come before our own Audit is constructed if declared global (in the case of LD_PRELOAD).
// To face this order problem, we declare our Audit as a static within a funciton which will be constructed upon first call.
// As all statics with cxtor/dxtor, we define it through new so as to avoid destruction during finalization.
Audit& Audit::t_audit() {
	static bool                s_inited [[maybe_unused]] = (s_init(),true) ;
	static thread_local Audit* s_res                     = new Audit       ;
	return *s_res ;
}

void Audit::hide(int fd) {
	if (Lock::s_busy()) return ;
	if (_t_report_fd.fd==fd) _t_report_fd.detach() ;                           // fd is about to be closed or are already closed, so no need to close
	if (s_root_fd   .fd==fd) s_root_fd   .detach() ;                           // .
}

void Audit::hide_range( int min , int max ) {
	if (Lock::s_busy()) return ;
	if ( _t_report_fd.fd>=min && _t_report_fd.fd<=max ) _t_report_fd.detach() ; // min<=fd<=max are about to be closed or are already closed, so no need to close
	if ( s_root_fd   .fd>=min && s_root_fd   .fd<=max ) s_root_fd   .detach() ; // .
}

//
// Audited
//

// search file in PATH if asked to do so
static void _search( const char* file , bool do_search , bool do_exec , const char* env_var , ::string const& comment="search" ) {
	const char* path = nullptr ;
	if ( do_search && !::strchr(file,'/') ) path = getenv(env_var) ;            // file contains a /, do not search
	if (!path) path = "" ;
	for( const char* start = path ;;) {
		char        buf[PATH_MAX] ;
		const char* end           = ::strchrnul(start,':') ;
		size_t      len           = end-start              ;
		const char* trial         ;
		if (len>=PATH_MAX) continue ;                                          // dont search this entry if there is overflow before copying
		if (len) {
			::strncpy(buf,start,len) ;
			buf[len] = '/' ;
			buf[PATH_MAX-1] = 0 ;
			::strncpy(buf+len+1,file,PATH_MAX-len-1) ;
			if(buf[PATH_MAX-1]) continue ;                                     // if we overflow while creating full name, do not consider this entry
			trial = buf ;
		} else {
			trial = file ;
		}
		if (do_exec) Audit::exec(AT_FDCWD,trial,false/*no_follow*/,comment) ;
		else         Audit::read(AT_FDCWD,trial,false/*no_follow*/,comment) ;
		if (!*end           ) break ;                                          // exhausted all entries
		if (is_target(trial)) break ;                                          // found entry, do not search further
		start = end+1 ;
	}
}

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

	// cwd is implicitely accessed by mostly all syscalls, so we have to ensure mutual exclusion as cwd could change between actual access and path resolution in audit
	// hence we should use a shared lock when reading and an exclusive lock when chdir
	// however, we have to ensure exclusivity for lnk cache, so we end up to exclusive access anyway, so simpler to lock exclusively here
	// use short macros as lines are very long in defining audited calls to libc
	#define LCK      Lock lock                                                 // lock shared    for cwd and exclusive for lnk cache & sock management
	#define LCK_EXCL Lock lock                                                 // lock exclusive for cwd and               lnk cache & sock management
	#define MK_MOD   mode_t mod = 0 ; if ( fs & (O_CREAT|O_TMPFILE) ) { va_list lst ; va_start(lst,fs) ; mod = va_arg(lst,mode_t) ; va_end(lst) ; }
	//
	#define ORIG(syscall) static decltype(::syscall)* orig = reinterpret_cast<decltype(::syscall)*>(get_orig(#syscall))

	// chdir
	int chdir (const char* path) { ORIG(chdir ) ; LCK_EXCL ; Audit::Chdir r{AT_FDCWD,path} ; return r(orig(path)) ; }
	int fchdir(int         fd  ) { ORIG(fchdir) ; LCK_EXCL ; Audit::Chdir r{fd           } ; return r(orig(fd  )) ; }

	// close
	// in case close is called with one our fd's, we must hide somewhere else
	int  close      (int          fd                           )          { ORIG(close      ) ; LCK ;                                  Audit::hide      (fd     ) ; return orig(fd          ) ; }
	int  __close    (int          fd                           )          { ORIG(__close    ) ; LCK ;                                  Audit::hide      (fd     ) ; return orig(fd          ) ; }
	int  close_range(unsigned int fd1,unsigned int fd2,int flgs) noexcept { ORIG(close_range) ; LCK ; if (!(flgs&CLOSE_RANGE_CLOEXEC)) Audit::hide_range(fd1,fd2) ; return orig(fd1,fd2,flgs) ; }
	void closefrom  (int          fd1                          ) noexcept { ORIG(closefrom  ) ; LCK ;                                  Audit::hide_range(fd1    ) ; return orig(fd1         ) ; }

	// dlopen
	#ifndef LD_PRELOAD // XXX : fix dlopen with centOS-7
	void* dlopen (             const char* p , int fs ) { ORIG(dlopen ) ; LCK ; _search(p,true/*search*/,false/*exec*/,"LD_LIBRARY_PATH","dlopen" ) ; return orig(   p,fs) ; }
	void* dlmopen( Lmid_t lm , const char* p , int fs ) { ORIG(dlmopen) ; LCK ; _search(p,true/*search*/,false/*exec*/,"LD_LIBRARY_PATH","dlmopen") ; return orig(lm,p,fs) ; }
	#endif

	// dup2
	// in case dup2/3 is called with one our fd's, we must hide somewhere else
	int dup2  ( int oldfd , int newfd             ) { ORIG(dup2  ) ; LCK ; Audit::hide(newfd) ; return orig(oldfd,newfd      ) ; }
	int dup3  ( int oldfd , int newfd , int flags ) { ORIG(dup3  ) ; LCK ; Audit::hide(newfd) ; return orig(oldfd,newfd,flags) ; }
	int __dup2( int oldfd , int newfd             ) { ORIG(__dup2) ; LCK ; Audit::hide(newfd) ; return orig(oldfd,newfd      ) ; }

	// execv
	int execv  ( const char* path , char* const argv[]                      ) { ORIG(execv  ) ; LCK ; _search(path,false/*search*/,true/*exec*/,"PATH","execv"  ) ; return orig(path,argv     ) ; }
	int execvp ( const char* path , char* const argv[]                      ) { ORIG(execvp ) ; LCK ; _search(path,true /*search*/,true/*exec*/,"PATH","execvp" ) ; return orig(path,argv     ) ; }
	int execve ( const char* path , char* const argv[] , char* const envp[] ) { ORIG(execve ) ; LCK ; _search(path,false/*search*/,true/*exec*/,"PATH","execve" ) ; return orig(path,argv,envp) ; }
	int execvpe( const char* path , char* const argv[] , char* const envp[] ) { ORIG(execvpe) ; LCK ; _search(path,true /*search*/,true/*exec*/,"PATH","execvpe") ; return orig(path,argv,envp) ; }
	//
	int execveat( int dirfd , const char* path , char* const argv[] , char *const envp[] , int flags ) noexcept {
		ORIG(execveat) ;
		LCK ;
		Audit::exec( dirfd , path , flags&AT_SYMLINK_NOFOLLOW , "execveat" ) ;
		return orig(dirfd,path,argv,envp,flags) ;
	}

	// execl
	#define MK_ARGS(end_action) \
		char*   cur         = const_cast<char*>(arg)              ;          \
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
	int execl ( const char* path , const char* arg , ... ) { MK_ARGS(                                               ) ; int rc = execv (path,args     ) ; delete[] args ; return rc ; }
	int execlp( const char* path , const char* arg , ... ) { MK_ARGS(                                               ) ; int rc = execvp(path,args     ) ; delete[] args ; return rc ; }
	int execle( const char* path , const char* arg , ... ) { MK_ARGS( char* const* envp = va_arg(args_lst,char**) ; ) ; int rc = execve(path,args,envp) ; delete[] args ; return rc ; }
	#undef MK_ARGS

	// fopen
	FILE* fopen    ( const char* pth , const char* mode            ) { ORIG(fopen    ) ; LCK ; Audit::Fopen r{pth,mode,"fopen"    } ; return r(orig(pth,mode   )) ; }
	FILE* fopen64  ( const char* pth , const char* mode            ) { ORIG(fopen64  ) ; LCK ; Audit::Fopen r{pth,mode,"fopen64"  } ; return r(orig(pth,mode   )) ; }
	FILE* freopen  ( const char* pth , const char* mode , FILE* fp ) { ORIG(freopen  ) ; LCK ; Audit::Fopen r{pth,mode,"freopen"  } ; return r(orig(pth,mode,fp)) ; }
	FILE* freopen64( const char* pth , const char* mode , FILE* fp ) { ORIG(freopen64) ; LCK ; Audit::Fopen r{pth,mode,"freopen64"} ; return r(orig(pth,mode,fp)) ; }

	// fork
	pid_t vfork  () { return fork  () ; } // mapped to fork as restrictions prevent most actions before following exec and we need a clean semantic to instrument exec
	pid_t __vfork() { return __fork() ; } // .

	// link
	int link  (       const char* op,       const char* np         ) { ORIG(link  ) ; LCK ; Audit::Lnk r{AT_FDCWD,op,AT_FDCWD,np     } ; return r(orig(   op,   np     )) ; }
	int linkat(int od,const char* op,int nd,const char* np,int flgs) { ORIG(linkat) ; LCK ; Audit::Lnk r{od      ,op,nd      ,np,flgs} ; return r(orig(od,op,nd,np,flgs)) ; }

	// mkstemp makes files in the TMPDIR directory which normally is not tracked
	// also, to generate the proper path, we should read_link /proc/<pid_or_self>/fd/<fd> to determine it
	//static constexpr int MkstempFlags = O_RDWR|O_TRUNC|O_CREAT|O_NOFOLLOW|O_EXCL ;
	//int mkstemp    ( char* tmpl                             ) { ORIG(mkstemp    ) ; LCK ; Audit::Open r{AT_FDCWD,???,MkstempFlags,"mkstemp"    } ; return r(orig(tmpl                )) ; }
	//int mkostemp   ( char* tmpl , int flags                 ) { ORIG(mkostemp   ) ; LCK ; Audit::Open r{AT_FDCWD,???,MkstempFlags,"mkostemp"   } ; return r(orig(tmpl,flags          )) ; }
	//int mkstemps   ( char* tmpl             , int suffixlen ) { ORIG(mkstemps   ) ; LCK ; Audit::Open r{AT_FDCWD,???,MkstempFlags,"mkstemps"   } ; return r(orig(tmpl      ,suffixlen)) ; }
	//int mkostemps  ( char* tmpl , int flags , int suffixlen ) { ORIG(mkostemps  ) ; LCK ; Audit::Open r{AT_FDCWD,???,MkstempFlags,"mkostemps"  } ; return r(orig(tmpl,flags,suffixlen)) ; }
	//int mkstemp64  ( char* tmpl                             ) { ORIG(mkstemp64  ) ; LCK ; Audit::Open r{AT_FDCWD,???,MkstempFlags,"mkstemp64"  } ; return r(orig(tmpl                )) ; }
	//int mkostemp64 ( char* tmpl , int flags                 ) { ORIG(mkostemp64 ) ; LCK ; Audit::Open r{AT_FDCWD,???,MkstempFlags,"mkostemp64" } ; return r(orig(tmpl,flags          )) ; }
	//int mkstemps64 ( char* tmpl             , int suffixlen ) { ORIG(mkstemps64 ) ; LCK ; Audit::Open r{AT_FDCWD,???,MkstempFlags,"mkstemps64" } ; return r(orig(tmpl      ,suffixlen)) ; }
	//int mkostemps64( char* tmpl , int flags , int suffixlen ) { ORIG(mkostemps64) ; LCK ; Audit::Open r{AT_FDCWD,???,MkstempFlags,"mkostemps64"} ; return r(orig(tmpl,flags,suffixlen)) ; }

	// open
	#define O_CWT O_CREAT|O_WRONLY|O_TRUNC
	int open             (         const char* p , int fs , ... ) { ORIG(open             ) ; MK_MOD ; LCK ; Audit::Open r{AT_FDCWD,p,fs   ,"open"             } ; return r(true,orig(  p,fs,mod)) ; }
	int __open           (         const char* p , int fs , ... ) { ORIG(__open           ) ; MK_MOD ; LCK ; Audit::Open r{AT_FDCWD,p,fs   ,"__open"           } ; return r(true,orig(  p,fs,mod)) ; }
	int __open_nocancel  (         const char* p , int fs , ... ) { ORIG(__open_nocancel  ) ; MK_MOD ; LCK ; Audit::Open r{AT_FDCWD,p,fs   ,"__open_nocancel"  } ; return r(true,orig(  p,fs,mod)) ; }
	int __open_2         (         const char* p , int fs       ) { ORIG(__open_2         ) ;          LCK ; Audit::Open r{AT_FDCWD,p,fs   ,"__open_2"         } ; return r(true,orig(  p,fs    )) ; }
	int open64           (         const char* p , int fs , ... ) { ORIG(open64           ) ; MK_MOD ; LCK ; Audit::Open r{AT_FDCWD,p,fs   ,"open64"           } ; return r(true,orig(  p,fs,mod)) ; }
	int __open64         (         const char* p , int fs , ... ) { ORIG(__open64         ) ; MK_MOD ; LCK ; Audit::Open r{AT_FDCWD,p,fs   ,"__open64"         } ; return r(true,orig(  p,fs,mod)) ; }
	int __open64_nocancel(         const char* p , int fs , ... ) { ORIG(__open64_nocancel) ; MK_MOD ; LCK ; Audit::Open r{AT_FDCWD,p,fs   ,"__open64_nocancel"} ; return r(true,orig(  p,fs,mod)) ; }
	int __open64_2       (         const char* p , int fs       ) { ORIG(__open64_2       ) ;          LCK ; Audit::Open r{AT_FDCWD,p,fs   ,"__open64_2"       } ; return r(true,orig(  p,fs    )) ; }
	int openat           ( int d , const char* p , int fs , ... ) { ORIG(openat           ) ; MK_MOD ; LCK ; Audit::Open r{d       ,p,fs   ,"openat"           } ; return r(true,orig(d,p,fs,mod)) ; }
	int __openat_2       ( int d , const char* p , int fs       ) { ORIG(__openat_2       ) ;          LCK ; Audit::Open r{d       ,p,fs   ,"__openat_2"       } ; return r(true,orig(d,p,fs    )) ; }
	int openat64         ( int d , const char* p , int fs , ... ) { ORIG(openat64         ) ; MK_MOD ; LCK ; Audit::Open r{d       ,p,fs   ,"openat64"         } ; return r(true,orig(d,p,fs,mod)) ; }
	int __openat64_2     ( int d , const char* p , int fs       ) { ORIG(__openat64_2     ) ;          LCK ; Audit::Open r{d       ,p,fs   ,"__openat64_2"     } ; return r(true,orig(d,p,fs    )) ; }
	int creat            (         const char* p , mode_t mod   ) { ORIG(creat            ) ;          LCK ; Audit::Open r{AT_FDCWD,p,O_CWT,"creat"            } ; return r(true,orig(  p,   mod)) ; }
	int creat64          (         const char* p , mode_t mod   ) { ORIG(creat64          ) ;          LCK ; Audit::Open r{AT_FDCWD,p,O_CWT,"creat64"          } ; return r(true,orig(  p,   mod)) ; }
	//
	int name_to_handle_at( int d , const char* p , struct ::file_handle *h , int *mount_id , int flgs ) {
		ORIG(name_to_handle_at) ;
		LCK ;
		Audit::Open r{d,p,flgs,"name_to_handle_at"} ;
		return r(false/*has_fd*/,orig(d,p,h,mount_id,flgs)) ;
	}

	// readlink
	// use short names to fit within a line
	#define READ_LNK(syscall,dfd,pth,buf,sz,res) \
		ORIG(syscall) ;                                                                                  \
		LCK ;                                                                                            \
		if (dfd==AT_BACKDOOR) { Audit::ReadLnk r{    pth,buf,sz,#syscall".backdoor"} ; return 0      ; } \
		else                  { Audit::ReadLnk r{dfd,pth       ,#syscall           } ; return r(res) ; }
	ssize_t readlink        (        const char* pth,char* buf          ,size_t buf_sz) { READ_LNK(readlink        ,AT_FDCWD,pth,buf,buf_sz,orig(    pth,buf,   buf_sz)) ; }
	ssize_t readlinkat      (int dfd,const char* pth,char* buf          ,size_t buf_sz) { READ_LNK(readlinkat      ,dfd     ,pth,buf,buf_sz,orig(dfd,pth,buf,   buf_sz)) ; }
	ssize_t __readlink_chk  (        const char* pth,char* buf,size_t sz,size_t buf_sz) { READ_LNK(__readlink_chk  ,AT_FDCWD,pth,buf,buf_sz,orig(    pth,buf,sz,buf_sz)) ; }
	ssize_t __readlinkat_chk(int dfd,const char* pth,char* buf,size_t sz,size_t buf_sz) { READ_LNK(__readlinkat_chk,dfd     ,pth,buf,buf_sz,orig(dfd,pth,buf,sz,buf_sz)) ; }
	#undef READ_LNK

	// rename
	#define RENAME(syscall,odfd,opth,ndfd,npth,flgs,res) ORIG(syscall) ; LCK ; Audit::Rename r{odfd,opth,ndfd,npth,flgs,#syscall} ; return r(res)
	int rename   (         const char* opth,         const char* npth                  )          { RENAME(rename   ,AT_FDCWD,opth,AT_FDCWD,npth,0u  ,orig(     opth,     npth     )) ; }
	int renameat (int odfd,const char* opth,int ndfd,const char* npth                  )          { RENAME(renameat ,odfd    ,opth,ndfd    ,npth,0u  ,orig(odfd,opth,ndfd,npth     )) ; }
	int renameat2(int odfd,const char* opth,int ndfd,const char* npth,unsigned int flgs) noexcept { RENAME(renameat2,odfd    ,opth,ndfd    ,npth,flgs,orig(odfd,opth,ndfd,npth,flgs)) ; }
	#undef RENAME

	// symlink
	int symlink  ( const char* target ,             const char* path ) { ORIG(symlink  ) ; LCK ; Audit::SymLnk r{AT_FDCWD,path,"symlink"  } ; return r(orig(target,      path)) ; }
	int symlinkat( const char* target , int dirfd , const char* path ) { ORIG(symlinkat) ; LCK ; Audit::SymLnk r{dirfd   ,path,"symlinkat"} ; return r(orig(target,dirfd,path)) ; }

	// truncate
	int truncate  (const char* pth,off_t len) { ORIG(truncate  ) ; LCK ; Audit::Open r{AT_FDCWD,pth,len?O_RDWR:O_WRONLY,"truncate"  } ; return r(false/*has_fd*/,orig(pth,len)) ; }
	int truncate64(const char* pth,off_t len) { ORIG(truncate64) ; LCK ; Audit::Open r{AT_FDCWD,pth,len?O_RDWR:O_WRONLY,"truncate64"} ; return r(false/*has_fd*/,orig(pth,len)) ; }

	// unlink
	int unlink  (             const char* path             ) { ORIG(unlink  ) ; LCK ; Audit::Unlink r{AT_FDCWD,path,false                   ,"unlink"  } ; return r(orig(      path      )) ; }
	int unlinkat( int dirfd , const char* path , int flags ) { ORIG(unlinkat) ; LCK ; Audit::Unlink r{dirfd   ,path,bool(flags&AT_REMOVEDIR),"unlinkat"} ; return r(orig(dirfd,path,flags)) ; }

	// mere path accesses (neeed to solve path, but no actual access to file data)
	#define SOLVE(syscall,dfd,pth,no_follow,res) ORIG(syscall) ; LCK ; Audit::solve(dfd,pth,no_follow,#syscall) ; return res
	//
	int  access   (           const char* pth , int mod            ) { SOLVE(access   ,AT_FDCWD,pth,false/*no_follow*/      ,orig(    pth,mod     )) ; }
	int  faccessat( int dfd , const char* pth , int mod , int flgs ) { SOLVE(faccessat,dfd     ,pth,flgs&AT_SYMLINK_NOFOLLOW,orig(dfd,pth,mod,flgs)) ; }
	DIR* opendir  (           const char* pth                      ) { SOLVE(opendir  ,AT_FDCWD,pth,true /*no_follow*/      ,orig(    pth         )) ; }
	int  rmdir    (           const char* pth                      ) { SOLVE(rmdir    ,AT_FDCWD,pth,true /*no_follow*/      ,orig(    pth         )) ; }
	//
	int __xstat     ( int v ,           const char* pth , struct stat  * buf            ) { SOLVE(__xstat     ,AT_FDCWD,pth,false/*no_follow*/      ,orig(v,    pth,buf     )) ; }
	int __xstat64   ( int v ,           const char* pth , struct stat64* buf            ) { SOLVE(__xstat64   ,AT_FDCWD,pth,false/*no_follow*/      ,orig(v,    pth,buf     )) ; }
	int __lxstat    ( int v ,           const char* pth , struct stat  * buf            ) { SOLVE(__lxstat    ,AT_FDCWD,pth,true /*no_follow*/      ,orig(v,    pth,buf     )) ; }
	int __lxstat64  ( int v ,           const char* pth , struct stat64* buf            ) { SOLVE(__lxstat64  ,AT_FDCWD,pth,true /*no_follow*/      ,orig(v,    pth,buf     )) ; }
	int __fxstatat  ( int v , int dfd , const char* pth , struct stat  * buf , int flgs ) { SOLVE(__fxstatat  ,dfd     ,pth,flgs&AT_SYMLINK_NOFOLLOW,orig(v,dfd,pth,buf,flgs)) ; }
	int __fxstatat64( int v , int dfd , const char* pth , struct stat64* buf , int flgs ) { SOLVE(__fxstatat64,dfd     ,pth,flgs&AT_SYMLINK_NOFOLLOW,orig(v,dfd,pth,buf,flgs)) ; }
	#if !NEED_STAT_WRAPPERS
		int stat     (           const char* pth , struct stat  * buf            ) { SOLVE(stat     ,AT_FDCWD,pth,false/*no_follow*/      ,orig(    pth,buf     )) ; }
		int stat64   (           const char* pth , struct stat64* buf            ) { SOLVE(stat64   ,AT_FDCWD,pth,false/*no_follow*/      ,orig(    pth,buf     )) ; }
		int lstat    (           const char* pth , struct stat  * buf            ) { SOLVE(lstat    ,AT_FDCWD,pth,true /*no_follow*/      ,orig(    pth,buf     )) ; }
		int lstat64  (           const char* pth , struct stat64* buf            ) { SOLVE(lstat64  ,AT_FDCWD,pth,true /*no_follow*/      ,orig(    pth,buf     )) ; }
		int fstatat  ( int dfd , const char* pth , struct stat  * buf , int flgs ) { SOLVE(fstatat  ,dfd     ,pth,flgs&AT_SYMLINK_NOFOLLOW,orig(dfd,pth,buf,flgs)) ; }
		int fstatat64( int dfd , const char* pth , struct stat64* buf , int flgs ) { SOLVE(fstatat64,dfd     ,pth,flgs&AT_SYMLINK_NOFOLLOW,orig(dfd,pth,buf,flgs)) ; }
	#endif
	//
	int statx( int dfd , const char* pth , int flgs , unsigned int msk , struct statx* buf ) noexcept { SOLVE(statx,AT_FDCWD,pth,true/*no_follow*/,orig(dfd,pth,flgs,msk,buf)) ; }
	// realpath
	char* realpath              ( const char* pth , char* rpth               ) { SOLVE(realpath              ,AT_FDCWD,pth,false/*no_follow*/,orig(pth,rpth     )) ; }
	char* __realpath_chk        ( const char* pth , char* rpth , size_t rlen ) { SOLVE(__realpath_chk        ,AT_FDCWD,pth,false/*no_follow*/,orig(pth,rpth,rlen)) ; }
	char* canonicalize_file_name( const char* pth                            ) { SOLVE(canonicalize_file_name,AT_FDCWD,pth,false/*no_follow*/,orig(pth          )) ; }
	// mkdir
	int mkdir  (           const char* pth , mode_t mod ) { SOLVE(mkdir  ,AT_FDCWD,pth,true/*no_follow*/,orig(    pth,mod)) ; }
	int mkdirat( int dfd , const char* pth , mode_t mod ) { SOLVE(mkdirat,dfd     ,pth,true/*no_follow*/,orig(dfd,pth,mod)) ; }
	// scandir
	using NameList   = struct dirent  ***                                       ;
	using NameList64 = struct dirent64***                                       ;
	using Filter     = int (*)(const struct dirent  *                         ) ;
	using Filter64   = int (*)(const struct dirent64*                         ) ;
	using Compare    = int (*)(const struct dirent**  ,const struct dirent  **) ;
	using Compare64  = int (*)(const struct dirent64**,const struct dirent64**) ;
	int scandir    (           const char* pth , NameList   nlst , Filter   fltr , Compare   cmp ) { SOLVE(scandir    ,AT_FDCWD,pth,true/*no_follow*/,orig(    pth,nlst,fltr,cmp)) ; }
	int scandir64  (           const char* pth , NameList64 nlst , Filter64 fltr , Compare64 cmp ) { SOLVE(scandir64  ,AT_FDCWD,pth,true/*no_follow*/,orig(    pth,nlst,fltr,cmp)) ; }
	int scandirat  ( int dfd , const char* pth , NameList   nlst , Filter   fltr , Compare   cmp ) { SOLVE(scandirat  ,dfd     ,pth,true/*no_follow*/,orig(dfd,pth,nlst,fltr,cmp)) ; }
	int scandirat64( int dfd , const char* pth , NameList64 nlst , Filter64 fltr , Compare64 cmp ) { SOLVE(scandirat64,dfd     ,pth,true/*no_follow*/,orig(dfd,pth,nlst,fltr,cmp)) ; }
	#undef SOLVE

	#undef LCK
	#undef LCK_EXCL
	#undef MK_MODE
}
#ifdef LD_PRELOAD
	#pragma GCC visibility pop
#endif
