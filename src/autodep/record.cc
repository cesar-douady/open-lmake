// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <glob.h>

#include "disk.hh"

#include "backdoor.hh"

#include "record.hh"

#ifndef O_PATH
	#define O_PATH 0    // no check for O_PATH if it does not exist
#endif
#ifndef O_TMPFILE
	#define O_TMPFILE 0 // no check for O_TMPFILE if it does not exist
#endif

using namespace Disk ;
using namespace Time ;

// /!\ : doing call to libc during static initialization may lead impredictably to incoherent results as we may be called very early in the init process
// so, do dynamic init for all static variables

//
// Record
//

bool                                        Record::s_static_report       = false     ;
bool                                        Record::s_enable_was_modified = false     ;
::vmap_s<DepDigest>*                        Record::s_deps                = nullptr   ;
::string           *                        Record::s_deps_err            = nullptr   ;
StaticUniqPtr<::umap_s<Record::CacheEntry>> Record::s_access_cache        ;             // map file to read accesses
Mutex<MutexLvl::Record>                     Record::s_mutex               ;
StaticUniqPtr<AutodepEnv                  > Record::_s_autodep_env        ;             // declare as pointer to avoid late initialization
Fd                                          Record::_s_repo_root_fd       ;
pid_t                                       Record::_s_repo_root_pid      = 0         ;
Fd                                          Record::_s_report_fd[2]       ;
pid_t                                       Record::_s_report_pid[2]      = { 0 , 0 } ;

bool Record::s_is_simple( const char* file , bool empty_is_simple ) {
	if (!file        ) return empty_is_simple ;                       // no file is simple (not documented, but used in practice)
	if (!file[0]     ) return empty_is_simple ;                       // empty file is simple
	if ( file[0]!='/') return false           ;                       // relative files are complex, in particular we dont even know relative to what (the dirfd arg is not passed in)
Restart :
	size_t pfx_sz = 0 ;
	switch (file[1]) {                                                                                                              // recognize simple and frequent top level system directories
		case 0   : return true ;                                                                                                    // / is simple
		case 'b' : if (::strncmp(file+1,"bin" ,3)!=0) goto ReturnFalse ; pfx_sz = 5 ; break ;
		case 'e' : if (::strncmp(file+1,"etc" ,3)!=0) goto ReturnFalse ; pfx_sz = 5 ; break ;
		case 'o' : if (::strncmp(file+1,"opt" ,3)!=0) goto ReturnFalse ; pfx_sz = 5 ; break ;                                       // used to install 3rd party software, not a data dir
		case 'r' : if (::strncmp(file+1,"run" ,3)!=0) goto ReturnFalse ; pfx_sz = 5 ; break ;
		case 's' : if (::strncmp(file+1,"sbin",4)!=0) goto ReturnFalse ; pfx_sz = 6 ;
		/**/       if (::strncmp(file+1,"sys" ,3)!=0) goto ReturnFalse ; pfx_sz = 5 ; break ;
		case 'u' : if (::strncmp(file+1,"usr" ,3)!=0) goto ReturnFalse ; pfx_sz = 5 ; break ;
		case 'v' : if (::strncmp(file+1,"var" ,3)!=0) goto ReturnFalse ; pfx_sz = 5 ; break ;
		case 'd' :
			if (::strncmp(file+1,"dev",3)!=0) goto ReturnFalse ;                                                                    // not in /dev  => not simple
			if (!file[4]                    ) return true      ;                                                                    // /dev         =>     simple
			if (file[4]!='/'                ) goto ReturnFalse ;                                                                    // false prefix => not simple
			switch (file[5]) {
				case 'f' : if ( ::strncmp(file+5,"fd",2   )==0 && (!file[7]||file[7]=='/') ) goto ReturnFalse ; break ;             // in /dev/fd   => not simple
				case 'r' : if ( ::strcmp (file+5,"random" )==0                             ) return false     ; break ;             // /dev/random  => not simple
				case 'u' : if ( ::strcmp (file+5,"urandom")==0                             ) return false     ; break ;             // /dev/urandom => not simple
				case 's' :
					if (::strncmp(file+5,"std",3)==0)
						switch (file[8]) {
							case 'e' : if ( ::strncmp(file+8,"err",3)==0 && (!file[11]||file[11]=='/') ) goto ReturnFalse ; break ; // /dev/stderr => not simple
							case 'i' : if ( ::strncmp(file+8,"in" ,2)==0 && (!file[10]||file[10]=='/') ) goto ReturnFalse ; break ; // /dev/stdin  => not simple
							case 'o' : if ( ::strncmp(file+8,"out",3)==0 && (!file[11]||file[11]=='/') ) goto ReturnFalse ; break ; // /dev/stdout => not simple
						DN}
				break ;
			DN}
			pfx_sz = 5 ;
		break ;
		case 'l' :
			if      (::strncmp(file+1,"lib",3)!=0) goto ReturnFalse ;                                                               // not in lib* => not simple
			if      (::strncmp(file+4,"32" ,2)==0) pfx_sz = 7 ;                                                                     // in lib32    =>     simple
			else if (::strncmp(file+4,"64" ,2)==0) pfx_sz = 7 ;                                                                     // in lib64    =>     simple
			else                                   pfx_sz = 5 ;                                                                     // in lib      =>     simple
		break ;
		case 'p' :                                                  // for /proc, must be a somewhat surgical because of jemalloc accesses and making these simple is the easiest way to avoid malloc's
			if ( ::strncmp(file+1,"proc",4)!=0 ) goto ReturnFalse ; // not in /proc            => not simple
			if ( !file[5]                      ) return true      ; // /proc                   =>     simple
			if ( file[5]!='/'                  ) goto ReturnFalse ; // false prefix            => not simple
			if ( file[6]>='0' && file[6]<='9'  ) goto ReturnFalse ; // probably in /proc/<pid> => not simple
			if ( ::strncmp(file+6,"self",4)==0 ) goto ReturnFalse ; // probably in /proc/self  => not simple
			pfx_sz = 6 ;
		break ;
		default : goto ReturnFalse ;
	}
	SWEAR( pfx_sz , file ) ;
	if (!file[pfx_sz-1]     ) return true      ;                    // known top level dir =>     simple
	if ( file[pfx_sz-1]!='/') goto ReturnFalse ;                    // false prefix        => not simple
	//
	{	int depth = 0 ;
		for ( const char* p=file+pfx_sz ; *p ; p++ ) {              // ensure we do not escape from top level dir
			if (p[ 0]!='/')              continue     ;             // not a dir boundary, go on
			if (p[-1]=='/')              continue     ;             // consecutive /'s, ignore
			if (p[-1]!='.') { depth++  ; continue     ; }           // plain dir  , e.g. foo  , go down
			if (p[-2]=='/')              continue     ;             // dot dir    ,             stay still
			if (p[-2]!='.') { depth++  ; continue     ; }           // plain dir  , e.g. foo. , go down
			if (p[-3]!='/') { depth++  ; continue     ; }           // plain dir  , e.g. foo.., go down
			if (!depth    ) { file = p ; goto Restart ; }           // dot-dot dir, restart if we get back to top-level, BACWARD
			/**/              depth--  ;                            // dot-dot dir
		}
	}
	return true ;
ReturnFalse :
	return ::strnlen(file,PATH_MAX+1)==PATH_MAX+1 ;                 // above PATH_MAX, no disk access can succeed, so it is not a dep and this makes sure no unreasonable data access
}

Sent Record::_do_send_report(pid_t pid) {
	SWEAR(+_buf) ;
	s_mutex.swear_locked() ;
	//
	bool fast = _is_slow!=Yes && _buf.size()<=PIPE_BUF                              ;                                         // several processes share fast report, so only small messages can be sent
	Fd   fd   = fast ? report_fd<true/*Fast*/>(pid) : report_fd<false/*Fast*/>(pid) ;
	if (+fd)
		try                       { _buf.send(fd) ;                                                                         }
		catch (::string const& e) { exit(Rc::System,read_lnk("/proc/self/exe"),'(',pid,") : cannot report accesses : ",e) ; } // NO_COV this justifies panic : do our best
	_buf = {} ;
	return !fd ? Sent::NotSent : fast ? Sent::Fast : Sent::Slow ;
}

void Record::_static_report(JobExecRpcReq&& jerr) const {
	jerr.chk() ;
	switch (jerr.proc) {
		case Proc::Tmp     :
		case Proc::Trace   :
		case Proc::Confirm :
		case Proc::Guard   : break ;
		case Proc::Access :
			if      (jerr.digest.write!=No) for( auto const& [f,_] : jerr.files ) *s_deps_err<<"unexpected write/unlink to "<<f<<'\n' ; // can have only deps from within server
			else if (!s_deps              ) for( auto const& [f,_] : jerr.files ) *s_deps_err<<"unexpected access of "      <<f<<'\n' ; // cant have deps when no way to record them
			else
				for( auto const& [f,fi] : jerr.files ) s_deps->emplace_back( ::move(f) , DepDigest(jerr.digest.accesses,fi,jerr.digest.flags.dflags) ) ;
		break ;
		default : *s_deps_err<<"unexpected "<<jerr.proc<<'\n' ;
	}
}

void Record::report_direct( JobExecRpcReq&& jerr , bool force ) {                             // dont touch jerr when returning false
	jerr.chk() ;
	//
	if ( !force && !enable )                                  return ;
	if ( s_static_report   ) { _static_report(::move(jerr)) ; return ; }
	s_mutex.swear_locked() ;
	//
	pid_t pid          = ::getpid()       ; if ( +_buf && _buf_pid!=pid ) _buf = {} ;         // if pid's do not match, we are in the child of a fork and _buf must be ignored
	Bool3 will_be_slow = _is_slow & +_buf ;
	//
	if      (jerr.sync!=No) will_be_slow  = Yes   ;                                           // no reply is possible on the fast channel as it is a (monodirectional) pipe
	else if (jerr.id      ) will_be_slow |= Maybe ;                                           // if we have an id, we need to be aware of the used channel
	//
	if ( +_buf && _buf.size()<=PIPE_BUF && _is_slow==No ) {                                   // send whatever can be sent over fast link
		if (will_be_slow==Yes) {                                                              // fast path : no need to serialize on the side and copy content
			send_report() ;                                                                   // better to send what can be sent over fast channel
		} else {
			::string jerr_str = serialize(jerr) ;
			if ( _buf.size()+sizeof(MsgBuf::Len)+jerr_str.size() > PIPE_BUF ) send_report() ; // send over fast link while still below threshold so as to stay with fast link as long as possible
			_buf.add_serialized(jerr_str) ;
			goto Return ;
		}
	}
	_buf.add(jerr) ;
Return :
	SWEAR( +_buf ) ;
	_buf_pid = pid          ;
	_is_slow = will_be_slow ;
}

void Record::report_cached( JobExecRpcReq&& jerr , bool force ) {
	SWEAR( jerr.proc==Proc::Access , jerr.proc ) ;
	if ( !force && !enable ) return ;
	if (jerr.sync!=Yes) {
		switch (jerr.digest.write) {
			case No : {
				CacheEntry::Acc acc { jerr.digest.accesses , jerr.digest.read_dir } ;
				::erase_if( jerr.files , [&]( ::pair_s<FileInfo> const& f_fi ) {
					auto        [it,inserted] = s_access_cache->try_emplace(f_fi.first) ;
					CacheEntry& entry         = it->second                              ;
					if (!inserted) { //!                                                                               erase
						if (f_fi.second.exists()) { if ( entry.flags>=jerr.digest.flags && entry.seen    >=acc ) return true ; } // no new seen accesses
						else                      { if ( entry.flags>=jerr.digest.flags && entry.accessed>=acc ) return true ; } // no new      accesses
					}
					/**/                      entry.flags    |= jerr.digest.flags ;
					/**/                      entry.accessed |= acc               ;
					if (f_fi.second.exists()) entry.seen     |= acc               ;
					return false/*erase*/ ;
				} ) ;
			} break ;
			case Yes :                                                                       // from now on, read accesses need not be reported as file has been written
				for( auto const& [f,_] : jerr.files ) (*s_access_cache)[f] = ~CacheEntry() ;
			break ;
		DN}
	}
	if (+jerr.files) report_direct(::move(jerr),force) ;
}

JobExecRpcReply Record::report_sync(JobExecRpcReq&& jerr) {
	Bool3 sync = jerr.sync ;                                      // sample before move
	report_direct(::move(jerr),true/*force*/) ;
	send_report() ;
	if      (sync!=Yes     ) return {}                          ;
	else if (s_has_server()) return _get_reply()                ;
	else                     return ::move(jerr).mimic_server() ; // if no server, try to mimic it as much as possible
}

// for modifying accesses :
// - if we report after  the access, it may be that job is interrupted inbetween and repo is modified without server being notified and we have a manual case
// - if we report before the access, we may notify an access that will not occur if job is interrupted or if access is finally an error
// so the choice is to manage Maybe :
// - access is reported as Maybe before the access
// - it is then confirmed (with an ok arg to manage errors) after the access
// in job_exec, if an access is left Maybe, i.e. if job is interrupted between the Maybe reporting and the actual access, disk is interrogated to see if access did occur
//
JobExecRpcReq::Id Record::report_access( JobExecRpcReq&& jerr , bool force ) {
	using Jerr = JobExecRpcReq ;
	// auto-generated id must be different for all accesses (could be random)
	// if _real_path.pid is not 0, we are ptracing, pid is meaningless but we are single thread and dates are different
	// else, ::gettid() would be a good id but it is not available on all systems,
	// however, within a process, dates are always different, so mix pid and date (multipication is to inject entropy in high order bits, practically suppressing all conflict possibilities)
	Time::Pdate now     ;
	Jerr::Id    id      = jerr.id                                        ;
	bool        need_id = !id && jerr.digest.write==Maybe && +jerr.files ;
	static_assert(sizeof(Jerr::Id)==8) ;                                                                                                          // else, rework shifting
	if      (need_id   ) { now = New ; id = now.val() ; if (!_real_path.pid) id += Jerr::Id(::getpid())*((uint64_t(1)<<48)+(uint64_t(1)<<32)) ; } // .
	else if (!jerr.date)   now = New ;
	//
	/**/                                                                 jerr.proc = JobExecProc::Access  ;
	if (!jerr.date           )                                           jerr.date = now                  ;
	if (need_id              )                                           jerr.id   = id                   ;
	if (+jerr.digest.accesses) for( auto& [f,fi] : jerr.files ) if (!fi) fi        = {s_repo_root_fd(),f} ;
	//
	report_cached( ::move(jerr) , force ) ;
	if (jerr.digest.write==Maybe) return id ;
	else                          return 0  ;
}

JobExecRpcReq::Id Record::report_access( FileLoc fl , JobExecRpcReq&& jerr , bool force ) {
	SWEAR( jerr.files.size()==1 , jerr ) ;
	if (fl>FileLoc::Dep ) return 0 ;
	if (fl>FileLoc::Repo) jerr.digest.write = No ;
	return report_access( ::move(jerr) , force ) ;
}

// if f0 is not empty, write is done to f0 rather than to jerr.file
JobExecRpcReq::Id Record::report_access( FileLoc fl , JobExecRpcReq&& jerr , FileLoc fl0 , ::string&& f0 , bool force ) {
	SWEAR( jerr.files.size()==1 , jerr ) ;
	if (+f0) {
		if (!jerr.date) jerr.date = New ; // ensure read and write dates are identical
		// read part
		JobExecRpcReq read_jerr = jerr ; read_jerr.digest.write = No ;
		report_access( fl , ::move(read_jerr) , force ) ;
		// write part
		fl                  = fl0 ;
		jerr.files[0].first = f0  ;
	}
	return report_access( fl , ::move(jerr) , force ) ;
}

Record::Chdir::Chdir( Record& r , Path&& path , Comment c ) : Solve<>{r,::move(path),true/*no_follow*/,false/*read*/,false/*create*/,c} {
	SWEAR(!accesses) ;                                                                                                                    // no access to last component when no_follow
	if ( s_autodep_env().auto_mkdir && file_loc==FileLoc::Repo ) mk_dir_s(at,with_slash(file)) ;                                          // in case of overlay, create dir in the view
	r.report_guard( file_loc , ::move(real_write()) ) ;
	send_report(r) ;
}

Record::Chmod::Chmod( Record& r , Path&& path , bool exe , bool no_follow , Comment c ) : SolveModify{r,::move(path),no_follow,true/*read*/,false/*create*/,c} { // behave like a read-modify-write
	if (file_loc>FileLoc::Dep) return ;
	FileInfo fi { s_repo_root_fd() , real } ;
	if ( fi.exists() && exe!=(fi.tag()==FileTag::Exe) ) // only consider as a target if exe bit changes
		report_update( r , Access::Reg , c ) ;
	send_report(r) ;
}

Record::Chroot::Chroot( Record& r , Path&& path , Comment c ) : Solve<>{r,::move(path),true/*no_follow*/,false/*read*/,false/*create*/,c} {
	r.report_panic("chroot to"+real) ;
	send_report(r) ;
}

static Record* _g_glob_auditor = nullptr/*garbage*/ ;
struct dirent* _glob_readdir(void* p) {
	DIR*            dirp = static_cast<DIR*>(p)                               ;
	Record::ReadDir rd   { *_g_glob_auditor , ::dirfd(dirp) , Comment::glob } ;
	return rd( *_g_glob_auditor , ::readdir(dirp) ) ;
}
void _glob_closedir(void* p) {
	::closedir(static_cast<DIR*>(p)) ;
}
void* _glob_opendir(const char* name) {
	return ::opendir(name) ;
}
Record::Glob::Glob( Record& r , const char* pat , int flags , Comment ) {
	_g_glob_auditor = &r ;
	::glob_t g {} ;
	g.gl_closedir = _glob_closedir ;
	g.gl_readdir  = _glob_readdir  ;
	g.gl_opendir  = _glob_opendir  ;
	g.gl_lstat    = ::lstat        ;
	g.gl_stat     = ::stat         ;
	::glob( pat , ( flags | (GLOB_NOSORT|GLOB_ALTDIRFUNC) ) & ~(GLOB_DOOFFS|GLOB_APPEND) , nullptr/*errfunc*/ , &g ) ;
	::globfree(&g) ;
}

Record::Lnk::Lnk( Record& r , Path&& src_ , Path&& dst_ , bool no_follow , Comment c ) :
	//                       no_follow   read   create
	src { r , ::move(src_) , no_follow , true  , false , c , CommentExt::Read  }
,	dst { r , ::move(dst_) , true      , false , true  , c , CommentExt::Write }
{
	if (src.real==dst.real) return ;                                      // posix says it is nop in that case
	//
	Pdate    now { New }      ;
	Accesses sa = Access::Reg ; if (no_follow) sa |= Access::Lnk ;        // if no_follow, a sym link may be hard linked
	src.report_dep   ( r , sa           , c , CommentExt::Read  , now ) ;
	dst.report_update( r , Access::Stat , c , CommentExt::Write , now ) ; // writing to dst is sensitive to existence
	dst.send_report(r) ;
}

Record::Mkdir::Mkdir( Record& r , Path&& path , Comment c ) : Solve<>{ r , ::move(path) , true/*no_follow*/ , false/*read*/ , false/*create*/ , c } {
	r.report_guard( file_loc , ::copy(real) ) ; // although dirs are not considered targets, it stays that disk has been modified and we want to propagate
	report_dep( r , Access::Stat , c ) ;        // fails if file exists, hence sensitive to existence
	send_report(r) ;
}

Record::Mount::Mount( Record& r , Path&& src_ , Path&& dst_ , Comment c ) :
	//                     no_follow read   create
	src { r , ::move(src_) , true  , false , false , c , CommentExt::Read  }
,	dst { r , ::move(dst_) , true  , false , false , c , CommentExt::Write }
{
	if (src.file_loc<=FileLoc::Dep) r.report_panic("mount from "+src.real) ;
	if (dst.file_loc<=FileLoc::Dep) r.report_panic("mount to "  +dst.real) ;
	dst.send_report(r) ;
}

static bool _no_follow(int flags) { return (flags&O_NOFOLLOW) || ( (flags&O_CREAT) && (flags&O_EXCL) )                                                        ; }
static bool _do_stat  (int flags) { return (flags&O_PATH)     || ( (flags&O_CREAT) && (flags&O_EXCL) ) || ( !(flags&O_CREAT) && (flags&O_ACCMODE)!=O_RDONLY ) ; }
static bool _do_read  (int flags) { return !(flags&O_PATH) && !(flags&O_TRUNC)                                                                                ; }
static bool _do_write (int flags) { return ( !(flags&O_PATH) && (flags&O_ACCMODE)!=O_RDONLY ) || (flags&O_TRUNC)                                              ; }
static bool _do_create(int flags) { return flags&O_CREAT                                                                                                      ; }
//
Record::Open::Open( Record& r , Path&& path , int flags , Comment c ) : SolveModify{ r , ::move(path) , _no_follow(flags) , _do_read(flags) , _do_create(flags) , c } {
	if ( !file || !file[0]             ) return ;
	if ( flags&(O_DIRECTORY|O_TMPFILE) ) return ;                                                                                    // we already solved, this is enough
	if ( file_loc>FileLoc::Dep         ) return ;                                                                                    // fast path
	//
	bool do_stat  = _do_stat (flags) ;
	bool do_read  = _do_read (flags) ;
	bool do_write = _do_write(flags) ;
	//
	if (!( do_stat || do_read || do_write )) return ;
	//
	CommentExts ces ;
	if (_no_follow(flags))   ces |= CommentExt::NoFollow ;
	if (do_stat          ) { ces |= CommentExt::Stat     ; if (!do_write) accesses = ~Accesses() ; else accesses |= Access::Stat ; } // if  not written, there may be a further fstat
	if (do_read          ) { ces |= CommentExt::Read     ; if (!do_write) accesses = ~Accesses() ; else accesses |= Access::Reg  ; } // .
	if (do_write         )   ces |= CommentExt::Write    ;
	//
	if (do_write) report_update( r , Accesses() , c , ces ) ;
	else          report_dep   ( r , Accesses() , c , ces ) ;
	send_report(r) ;
}

Record::Readlink::Readlink( Record& r , Path&& path , char* buf_ , size_t sz_ , Comment c ) : Solve<>{r,::move(path),true/*no_follow*/,true/*read*/,false/*create*/,c} , buf{buf_} , sz{sz_} {
	report_dep( r , Access::Lnk , c ) ;
	send_report(r) ;
}

ssize_t Record::Readlink::operator()( Record& r , ssize_t len ) {
	if (!( at==Backdoor::MagicFd && ::strncmp(file,Backdoor::MagicPfx,Backdoor::MagicPfxLen)==0 )) return len ;
	//
	::string                        cmd      = file+Backdoor::MagicPfxLen         ;
	size_t                          slash    = cmd.find('/')                      ; SWEAR(slash!=Npos) ;
	::umap_s<Backdoor::Func> const& func_tab = Backdoor::get_func_tab()           ;
	auto                            it       = func_tab.find(cmd.substr(0,slash)) ;
	if ((magic=it!=func_tab.end())) {
		if (!buf) buf = new char[sz] ;                           // if no buf, we are in ptrace mode and we allocate it when necessary, it will be deleted by caller
		len = it->second( r , cmd.substr(slash+1) , buf , sz ) ;
	}
	SWEAR( len<=ssize_t(sz) , len,sz ) ;
	return len ;
}

// flags is not used if exchange is not supported
Record::Rename::Rename( Record& r , Path&& src_ , Path&& dst_ , bool exchange , bool no_replace , Comment c ) :
	//                     no_follow read       create
	src { r , ::move(src_) , true  , true     , exchange , c , CommentExt::Read  }
,	dst { r , ::move(dst_) , true  , exchange , true     , c , CommentExt::Write }
{	if (src.real==dst.real) return ;                                                                            // posix says in this case, it is nop
	// rename has not occurred yet so :
	// - files are read and unlinked in the source dir
	// - their coresponding files in the destination dir are written
	::vmap_s<FileInfo>     reads     ;
	::vmap_s<FileInfo>     stats     ;
	::umap_s<bool/*read*/> unlnk_map ;                                                                          // files listed here are read and unlinked
	::vmap_s<FileInfo>     unlnks    ;
	::vmap_s<FileInfo>     writes    ;
	auto do1 = [&]( Solve<> const& src , Solve<> const& dst ) {
		for( auto const& [f,_] : walk(s_repo_root_fd(),src.real,TargetTags) ) {
			if (+src.real0) {
				if      (src.file_loc0<=FileLoc::Repo) unlnk_map.try_emplace (src.real0+f,false/*read*/) ;      // real is read, real0 is unlinked
				if      (src.file_loc <=FileLoc::Dep ) reads    .emplace_back(src.real +f,FileInfo()   ) ;
			} else {
				if      (src.file_loc<=FileLoc::Repo) unlnk_map.try_emplace (src.real+f,true/*read*/) ;         // real is both read and unlinked
				else if (src.file_loc<=FileLoc::Dep ) reads    .emplace_back(src.real+f,FileInfo()  ) ;
			}
			if (no_replace) { if (dst.file_loc <=FileLoc::Dep ) stats .emplace_back(dst.real +f,FileInfo()) ; } // probe existence of destination
			if (+dst.real0) { if (dst.file_loc0<=FileLoc::Repo) writes.emplace_back(dst.real0+f,FileInfo()) ; }
			else            { if (dst.file_loc <=FileLoc::Repo) writes.emplace_back(dst.real +f,FileInfo()) ; }
		}
	} ;
	/**/          do1(src,dst) ;
	if (exchange) do1(dst,src) ;
	//
	for( auto const& [w,_] : writes ) {
		auto it = unlnk_map.find(w) ;
		if (it==unlnk_map.end()) continue ;
		reads.emplace_back(w,FileInfo()) ;                                                                      // if a file is read, unlinked and written, it is actually not unlinked
		unlnk_map.erase(it) ;                                                                                   // .
	}
	for( auto const& [u,r] : unlnk_map )
		if (r) unlnks.emplace_back( u , FileInfo() ) ;
		else   writes.emplace_back( u , FileInfo() ) ;                                                          // if not read, unlnk is like write
	//
	::umap_s<FileInfo> guards ;
	for( auto const& [w,_] : writes ) if (+w) guards.try_emplace( no_slash(dir_name_s(w)) , FileInfo() ) ;
	for( auto const& [u,_] : unlnks ) if (+u) guards.try_emplace( no_slash(dir_name_s(u)) , FileInfo() ) ;
	if (+guards) r.report_guard( mk_vmap(guards) ) ;
	//
	Pdate now { New } ;
	/**/         r.report_access( { .comment=c , .comment_exts=CommentExt::Read  , .digest={             .accesses=DataAccesses} ,                  .date=now , .files=::move(reads ) } ) ;
	/**/         r.report_access( { .comment=c , .comment_exts=CommentExt::Stat  , .digest={             .accesses=Access::Stat} ,                  .date=now , .files=::move(stats ) } ) ;
	confirm_id = r.report_access( { .comment=c , .comment_exts=CommentExt::Unlnk , .digest={.write=Maybe,.accesses=DataAccesses} ,                  .date=now , .files=::move(unlnks) } ) ;
	confirm_id = r.report_access( { .comment=c , .comment_exts=CommentExt::Write , .digest={.write=Maybe                       } , .id=confirm_id , .date=now , .files=::move(writes) } ) ;
	confirm_fd = r.send_report() ;
}

Record::Stat::Stat( Record& r , Path&& path , bool no_follow , Accesses a , Comment c ) :
	Solve<>{ r , !s_autodep_env().ignore_stat?::move(path):Path() , no_follow , true/*read*/ , false/*create*/ , c }
{
	CommentExts ces ; if (no_follow) ces |= CommentExt::NoFollow ;
	if (!s_autodep_env().ignore_stat) report_dep( r , a , c , ces ) ;
	send_report(r) ;
}

Record::Symlink::Symlink( Record& r , Path&& p , Comment c ) : SolveModify{r,::move(p),true/*no_follow*/,false/*read*/,true/*create*/,c} {
	report_update( r , Access::Stat , c ) ;                                                                                                // fail if file exists, hence sensitive to existence
	send_report(r) ;
}

Record::Unlnk::Unlnk( Record& r , Path&& p , bool remove_dir , Comment c ) : SolveModify{r,::move(p),true/*no_follow*/,false/*read*/,false/*create*/,c} {
	if (remove_dir) r.report_guard( file_loc , ::move(real_write()) ) ;
	else            report_update( r , Access::Stat , c )             ; // fail if file does not exist, hence sensitive to existence
	send_report(r) ;
}
