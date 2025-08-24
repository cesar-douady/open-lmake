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

bool                                        Record::s_static_report  = false     ;
::vmap_s<DepDigest>*                        Record::s_deps           = nullptr   ;
::string           *                        Record::s_deps_err       = nullptr   ;
StaticUniqPtr<::umap_s<Record::CacheEntry>> Record::s_access_cache   ;             // map file to read accesses
StaticUniqPtr<AutodepEnv                  > Record::_s_autodep_env   ;             // declare as pointer to avoid late initialization
Fd                                          Record::_s_repo_root_fd  ;
pid_t                                       Record::_s_repo_root_pid = 0         ;
Fd                                          Record::_s_report_fd[2]  ;
pid_t                                       Record::_s_report_pid[2] = { 0 , 0 } ;

bool Record::s_is_simple( const char* file , bool empty_is_simple ) {
	if (!file        ) return empty_is_simple ;                                  // no file is simple (not documented, but used in practice)
	if (!file[0]     ) return empty_is_simple ;                                  // empty file is simple
	if ( file[0]!='/') return false           ;                                  // relative files are complex, in particular we dont even know relative to what (the dirfd arg is not passed in)
Restart :
	size_t pfx_sz = 0 ;
	switch (file[1]) {                                                           // recognize simple and frequent top level system directories
		case 0   :                                                 return true ; // / is simple
		case 'b' : if (::strncmp(file+1,"bin" ,3)==0) pfx_sz = 5 ; break       ;
		case 'd' : if (::strncmp(file+1,"dev" ,3)==0) pfx_sz = 5 ; break       ;
		case 'e' : if (::strncmp(file+1,"etc" ,3)==0) pfx_sz = 5 ; break       ;
		case 'o' : if (::strncmp(file+1,"opt" ,3)==0) pfx_sz = 5 ; break       ; // used to install 3rd party software, not a data dir
		case 'r' : if (::strncmp(file+1,"run" ,3)==0) pfx_sz = 5 ; break       ;
		case 's' : if (::strncmp(file+1,"sbin",4)==0) pfx_sz = 6 ;
		/**/       if (::strncmp(file+1,"sys" ,3)==0) pfx_sz = 5 ; break       ;
		case 'u' : if (::strncmp(file+1,"usr" ,3)==0) pfx_sz = 5 ; break       ;
		case 'v' : if (::strncmp(file+1,"var" ,3)==0) pfx_sz = 5 ; break       ;
		case 'l' :
			if      (::strncmp(file+1,"lib",3)!=0) break ;          // not in lib* => not simple
			if      (::strncmp(file+4,"32" ,2)==0) pfx_sz = 7 ;     // in lib32    => simple
			else if (::strncmp(file+4,"64" ,2)==0) pfx_sz = 7 ;     // in lib64    => simple
			else                                   pfx_sz = 5 ;     // in lib      => simple
		break ;                                                     // else        => not simple
		case 'p' :                                                  // for /proc, must be a somewhat surgical because of jemalloc accesses and making these simple is the easiest way to avoid malloc's
			if ( ::strncmp(file+1,"proc",4)!=0 ) break            ;  // not in /proc      => not simple
			if ( !file[5]                      ) return true      ;  // /proc             => simple
			if ( file[5]!='/'                  ) goto ReturnFalse ;  // false prefix      => not simple
			if ( file[6]>='0' && file[6]<='9'  ) goto ReturnFalse ;  // in /proc/<pid>    => not simple
			if ( ::strncmp(file+6,"self",4)!=0 ) goto SimpleProc  ;  // not in /proc/self => simple
			if ( !file[10]                     ) return true      ;  // /proc/self        => simple
			if ( file[10]=='/'                 ) goto ReturnFalse ;  // in /proc/self     => not simple
		SimpleProc :
			pfx_sz = 6 ;
		break ;
	DN}
	if ( !pfx_sz                               ) goto ReturnFalse ; // no prefix
	if ( file[pfx_sz-1] && file[pfx_sz-1]!='/' ) goto ReturnFalse ; // false prefix
	//
	{	int depth = 0 ;
		for ( const char* p=file+pfx_sz ; *p ; p++ ) {              // ensure we do not escape from top level dir
			if (p[ 0]!='/')              continue     ;             // not a dir boundary, go on
			if (p[-1]=='/')              continue     ;             // consecutive /'s, ignore
			if (p[-1]!='.') { depth++  ; continue     ; }           // plain dir  , e.g. foo  , go down
			if (p[-2]=='/')              continue     ;             // dot dir    ,             stay still
			if (p[-2]!='.') { depth++  ; continue     ; }           // plain dir  , e.g. foo. , go down
			if (p[-3]!='/') { depth++  ; continue     ; }           // plain dir  , e.g. foo.., go down
			if (!depth    ) { file = p ; goto Restart ; }           // dot-dot dir, restart if we get back to top-level
			/**/              depth--  ;                            // dot-dot dir
		}
	}
	return true ;
ReturnFalse :
	return ::strnlen(file,PATH_MAX+1)==PATH_MAX+1 ;                 // above PATH_MAX, no disk access can succeed, so it is not a dep and this makes sure no unreasonable data access
}

void Record::_static_report(JobExecRpcReq&& jerr) const {
	jerr.chk() ;
	switch (jerr.proc) {
		case Proc::Tmp     :
		case Proc::Trace   :
		case Proc::Confirm :
		case Proc::Guard   : break ;
		case Proc::Access :
			if      (jerr.digest.write!=No) *s_deps_err<<"unexpected write/unlink to "<<jerr.file<<'\n' ; // can have only deps from within server
			else if (!s_deps              ) *s_deps_err<<"unexpected access of "      <<jerr.file<<'\n' ; // cant have deps when no way to record them
			else {
				s_deps->emplace_back( ::move(jerr.file) , DepDigest(jerr.digest.accesses,jerr.file_info,jerr.digest.flags.dflags,true/*parallel*/) ) ;
				s_deps->back().second.parallel = false ;                                                  // parallel bit is marked false on last of a series of parallel accesses
			}
		break ;
		default : *s_deps_err<<"unexpected "<<jerr.proc<<'\n' ;
	}
}

Sent Record::report_direct( JobExecRpcReq&& jerr , bool force ) const {
	jerr.chk() ;
	//
	if ( !force && !enable )                                  return Sent::NotSent ;
	if ( s_static_report   ) { _static_report(::move(jerr)) ; return Sent::Static  ; }
	//
	OMsgBuf msg  { jerr }                                                                                ;
	bool    fast = jerr.sync==No && msg.size()<=PIPE_BUF                                                 ; // several processes share fast report, so only small messages can be sent
	Fd      fd   = fast ? s_report_fd<true/*Fast*/>(::getpid()) : s_report_fd<false/*Fast*/>(::getpid()) ;
	if (+fd) {
		try                       { msg.send(fd) ;                                                                                                }
		catch (::string const& e) { exit(Rc::System,read_lnk("/proc/self/exe"),'(',::getpid(),") : cannot report access to ",jerr.file," : ",e) ; } // NO_COV this justifies panic : do our best
	}
	Sent sent = !fd ? Sent::NotSent : fast ? Sent::Fast : Sent::Slow ;
	return sent ;
}

Sent Record::report_cached( JobExecRpcReq&& jerr , bool force ) const {
	SWEAR( jerr.proc==Proc::Access , jerr.proc ) ;
	if ( !force && !enable ) return {}/*sent*/ ;                                      // dont update cache as report is not actually done
	if (!jerr.sync) {
		SWEAR( +jerr.file , jerr ) ;
		auto        [it,inserted] = s_access_cache->try_emplace(jerr.file) ;
		CacheEntry& entry         = it->second                             ;
		if (jerr.digest.write==No) {                                                  // modifying accesses cannot be cached as we do not know what other processes may have done in between
			CacheEntry::Acc acc { jerr.digest.accesses , jerr.digest.read_dir } ;
			if (!inserted) { //!                                              sent
				if (jerr.file_info.exists()) { if (entry.seen    >=acc) return {} ; } // no new seen accesses
				else                         { if (entry.accessed>=acc) return {} ; } // no new      accesses
			}
			/**/                         entry.accessed |= acc ;
			if (jerr.file_info.exists()) entry.seen     |= acc ;
		} else {
			entry = ~CacheEntry() ;                                                   // from now on, read accesses need not be reported as file has been written
		}
	}
	return report_direct(::move(jerr),force) ;
}

JobExecRpcReply Record::report_sync( JobExecRpcReq&& jerr , bool force ) const {
	thread_local ::string   codec_file   ;
	thread_local ::string   codec_ctx    ;
	thread_local ::vector_s dep_verboses ;
	//
	if (+report_direct(::move(jerr),force)) {
		/**/                                   if (jerr.sync!=Yes) return {}    ;
		JobExecRpcReply reply = _get_reply() ; if (+reply        ) return reply ; // else job_exec could not contact server and generated an empty reply, process as if no job_exec
	}
	// not under lmake (typically ldebug), try to mimic server as much as possible
	switch (jerr.proc) {
		case Proc::DepVerbosePush : dep_verboses.push_back(::move(jerr.file)) ; break ;
		case Proc::CodecFile      : codec_file = ::move(jerr.file) ;            break ;
		case Proc::CodecCtx       : codec_ctx  = ::move(jerr.file) ;            break ;
		case Proc::DepVerbose : {
			::vector<DepVerboseInfo> dep_infos ;
			for( ::string& f : dep_verboses ) dep_infos.push_back({ .ok=Yes , .crc=Crc(f) }) ;
			dep_verboses.clear() ;
			return { .proc=jerr.proc , .dep_infos=::move(dep_infos) } ;
		}
		case Proc::Decode :
		case Proc::Encode :
			// /!\ format must stay in sync with Codec::_s_canonicalize
			for( ::string const& line : AcFd(codec_file,true/*err_ok*/).read_lines() ) {
				size_t pos = 0 ;
				/**/                                             if ( line[pos++]!=' '                   ) continue ; // bad format
				::string ctx  = parse_printable<' '>(line,pos) ; if ( line[pos++]!=' ' || ctx!=codec_ctx ) continue ; // .          or bad ctx
				::string code = parse_printable<' '>(line,pos) ; if ( line[pos++]!=' '                   ) continue ; // .
				::string val  = parse_printable     (line,pos) ; if ( line[pos  ]!=0                     ) continue ; // .
				//
				if (jerr.proc==Proc::Decode) { if (code==jerr.file) return { .proc=jerr.proc , .ok=Yes , .txt=val } ; }
				else                         { if (val ==jerr.file) return { .proc=jerr.proc , .ok=Yes , .txt=code} ; }
			}
			return { .proc=jerr.proc , .ok=No , .txt="0" } ;
	DN}
	return {} ;
}

// for modifying accesses :
// - if we report after  the access, it may be that job is interrupted inbetween and repo is modified without server being notified and we have a manual case
// - if we report before the access, we may notify an access that will not occur if job is interrupted or if access is finally an error
// so the choice is to manage Maybe :
// - access is reported as Maybe before the access
// - it is then confirmed (with an ok arg to manage errors) after the access
// in job_exec, if an access is left Maybe, i.e. if job is interrupted between the Maybe reporting and the actual access, disk is interrogated to see if access did occur
//
::pair<Sent/*confirm*/,JobExecRpcReq::Id> Record::report_access( FileLoc fl , JobExecRpcReq&& jerr , bool force ) const {
	using Jerr = JobExecRpcReq ;
	if (fl>FileLoc::Dep ) return { {}/*confirm*/ , 0 } ;
	if (fl>FileLoc::Repo) jerr.digest.write = No ;
	// auto-generated id must be different for all accesses (could be random)
	// if _real_path.pid is not 0, we are ptracing, pid is meaningless but we are single thread and dates are different
	// else, ::gettid() would be a good id but it is not available on all systems,
	// however, within a process, dates are always different, so mix pid and date (multipication is to inject entropy in high order bits, practically suppressing all conflict possibilities)
	Time::Pdate now     ;
	Jerr::Id    id      = jerr.id                         ;
	bool        need_id = !id && jerr.digest.write==Maybe ;
	static_assert(sizeof(Jerr::Id)==8) ;                                                                                                          // else, rework shifting
	if      (need_id   ) { now = New ; id = now.val() ; if (!_real_path.pid) id += Jerr::Id(::getpid())*((uint64_t(1)<<48)+(uint64_t(1)<<32)) ; } // .
	else if (!jerr.date)   now = New ;
	//
	/**/                                            jerr.proc      = JobExecProc::Access          ;
	if ( !jerr.date                               ) jerr.date      = now                          ;
	if ( need_id                                  ) jerr.id        = id                           ;
	if ( !jerr.file_info && +jerr.digest.accesses ) jerr.file_info = {s_repo_root_fd(),jerr.file} ;
	//
	Sent sent = report_cached( ::move(jerr) , force ) ;
	if (jerr.digest.write==Maybe) return { sent/*confirm*/ , id      } ;
	else                          return { {}  /*confirm*/ , 0/*id*/ } ;
}

// if f0 is not empty, write is done to f0 rather than to jerr.file
::pair<Sent/*confirm*/,JobExecRpcReq::Id> Record::report_access( FileLoc fl , JobExecRpcReq&& jerr , FileLoc fl0 , ::string&& f0 , bool force ) const {
	if (+f0) {
		// read part
		JobExecRpcReq read_jerr = jerr ; read_jerr.digest.write = No ;
		report_access( fl , ::move(read_jerr) , force ) ;
		// write part
		fl        = fl0 ;
		jerr.file = f0  ;
	}
	return report_access( fl , ::move(jerr) , force ) ;
}

Record::Chdir::Chdir( Record& r , Path&& path , Comment c ) : Solve{r,::move(path),true/*no_follow*/,false/*read*/,false/*create*/,c} {
	SWEAR(!accesses) ;                                                                                                                  // no access to last component when no_follow
	if ( s_autodep_env().auto_mkdir && file_loc==FileLoc::Repo ) mk_dir_s(at,with_slash(file)) ;                                        // in case of overlay, create dir in the view
	r.report_guard( file_loc , ::move(real_write()) ) ;
}

Record::Chmod::Chmod( Record& r , Path&& path , bool exe , bool no_follow , Comment c ) : SolveModify{r,::move(path),no_follow,true/*read*/,false/*create*/,c} { // behave like a read-modify-write
	if (file_loc>FileLoc::Dep) return ;
	FileInfo fi { s_repo_root_fd() , real } ;
	if ( fi.exists() && exe!=(fi.tag()==FileTag::Exe) ) // only consider as a target if exe bit changes
		report_update( r , Access::Reg , c ) ;
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
	glob_t g {} ;
	g.gl_closedir = _glob_closedir ;
	g.gl_readdir  = _glob_readdir  ;
	g.gl_opendir  = _glob_opendir  ;
	g.gl_lstat    = ::lstat        ;
	g.gl_stat     = ::stat         ;
	glob( pat , ( flags | (GLOB_NOSORT|GLOB_ALTDIRFUNC) ) & ~(GLOB_DOOFFS|GLOB_APPEND) , nullptr/*errfunc*/ , &g ) ;
	globfree(&g) ;
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
}

Record::Mkdir::Mkdir( Record& r , Path&& path , Comment c ) : Solve{ r , ::move(path) , true/*no_follow*/ , false/*read*/ , false/*create*/ , c } {
	r.report_guard( file_loc , ::copy(real) ) ; // although dirs are not considered targets, it stays that disk has been modified and we want to propagate
	report_dep( r , Access::Stat , c ) ;        // fails if file exists, hence sensitive to existence
}

Record::Mount::Mount( Record& r , Path&& src_ , Path&& dst_ , Comment c ) :
	//                     no_follow read   create
	src { r , ::move(src_) , true  , false , false , c , CommentExt::Read  }
,	dst { r , ::move(dst_) , true  , false , false , c , CommentExt::Write }
{
	if (src.file_loc<=FileLoc::Dep) r.report_panic("mount from "+src.real) ;
	if (dst.file_loc<=FileLoc::Dep) r.report_panic("mount to "  +dst.real) ;
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
}

Record::Readlink::Readlink( Record& r , Path&& path , char* buf_ , size_t sz_ , Comment c ) : Solve{r,::move(path),true/*no_follow*/,true/*read*/,false/*create*/,c} , buf{buf_} , sz{sz_} {
	report_dep( r , Access::Lnk , c ) ;
}

ssize_t Record::Readlink::operator()( Record& r , ssize_t len ) {
	if (at!=Backdoor::MagicFd                                      ) return len ;
	if (::strncmp(file,Backdoor::MagicPfx,Backdoor::MagicPfxLen)!=0) return len ;
	::string                        cmd      = file+Backdoor::MagicPfxLen         ;
	size_t                          slash    = cmd.find('/')                      ;
	::umap_s<Backdoor::Func> const& func_tab = Backdoor::get_func_tab()           ;
	auto                            it       = func_tab.find(cmd.substr(0,slash)) ;
	if ((emulated=it!=func_tab.end())) len = it->second( r , cmd.substr(slash+1) , buf , sz ) ;
	return len ;
}

// flags is not used if exchange is not supported
Record::Rename::Rename( Record& r , Path&& src_ , Path&& dst_ , bool exchange , bool no_replace , Comment c ) :
	//                     no_follow read       create
	src { r , ::move(src_) , true  , true     , exchange , c , CommentExt::Read  }
,	dst { r , ::move(dst_) , true  , exchange , true     , c , CommentExt::Write }
{	if (src.real==dst.real) return ;                                                                   // posix says in this case, it is nop
	// rename has not occurred yet so :
	// - files are read and unlinked in the source dir
	// - their coresponding files in the destination dir are written
	::vector_s             reads  ;
	::vector_s             stats  ;
	::umap_s<bool/*read*/> unlnks ;                                                                    // files listed here are read and unlinked
	::vector_s             writes ;
	auto do1 = [&]( Solve const& src , Solve const& dst )->void {
		for( auto const& [f,_] : walk(s_repo_root_fd(),src.real,TargetTags) ) {
			if (+src.real0) {
				if      (src.file_loc0<=FileLoc::Repo) unlnks.try_emplace(src.real0+f,false/*read*/) ; // real is read, real0 is unlinked
				if      (src.file_loc <=FileLoc::Dep ) reads .push_back  (src.real +f              ) ;
			} else {
				if      (src.file_loc <=FileLoc::Repo) unlnks.try_emplace(src.real +f,true/*read*/) ;  // real is both read and unlinked
				else if (src.file_loc <=FileLoc::Dep ) reads .push_back  (src.real +f             ) ;
			}
			if (no_replace) stats.push_back(dst.real+f) ;                                              // probe existence of destination
			if (+dst.real0) { if (dst.file_loc0<=FileLoc::Repo) writes.push_back(dst.real0+f) ; }
			else            { if (dst.file_loc <=FileLoc::Repo) writes.push_back(dst.real +f) ; }
		}
	} ;
	/**/          do1(src,dst) ;
	if (exchange) do1(dst,src) ;
	//
	for( ::string const& w : writes ) {
		auto it = unlnks.find(w) ;
		if (it==unlnks.end()) continue ;
		reads.push_back(w) ;                                                                           // if a file is read, unlinked and written, it is actually not unlinked
		unlnks.erase(it) ;                                                                             // .
	}
	//
	::uset_s guards ;
	for( ::string const& w     : writes ) guards.insert(dir_name_s(w)) ;
	for( auto     const& [u,_] : unlnks ) guards.insert(dir_name_s(u)) ;
	guards.erase(""s) ;
	for( ::string const& g : guards ) r.report_guard( FileLoc::Repo , no_slash(g) ) ;
	//
	Pdate now { New } ;
	src.file_loc = FileLoc::Dep  ; src.accesses = {} ;                                                 // use src/dst as holders for reporting
	dst.file_loc = FileLoc::Repo ; dst.accesses = {} ; dst.real0.clear() ;                             // .
	//
	for( ::string&    f    : reads  ) { src.real =        f  ; src.report_dep   ( r , DataAccesses              , c , CommentExt::Read  , now ) ; }
	for( ::string&    f    : stats  ) { src.real =        f  ; src.report_dep   ( r , Access::Stat              , c , CommentExt::Stat  , now ) ; }
	for( auto const& [f,a] : unlnks ) { dst.real =        f  ; dst.report_update( r , a?DataAccesses:Accesses() , c , CommentExt::Unlnk , now ) ; }
	for( ::string&    f    : writes ) { dst.real = ::move(f) ; dst.report_update( r , {}                        , c , CommentExt::Write , now ) ; }
}

Record::Stat::Stat( Record& r , Path&& path , bool no_follow , Accesses a , Comment c ) :
	Solve{ r , !s_autodep_env().ignore_stat?::move(path):Path() , no_follow , true/*read*/ , false/*create*/ , c }
{
	CommentExts ces ; if (no_follow) ces |= CommentExt::NoFollow ;
	if (!s_autodep_env().ignore_stat) report_dep( r , a , c , ces ) ;
}

Record::Symlink::Symlink( Record& r , Path&& p , Comment c ) : SolveModify{r,::move(p),true/*no_follow*/,false/*read*/,true/*create*/,c} {
	report_update( r , Access::Stat , c ) ;                                                                                                // fail if file exists, hence sensitive to existence
}

Record::Unlnk::Unlnk( Record& r , Path&& p , bool remove_dir , Comment c ) : SolveModify{r,::move(p),true/*no_follow*/,false/*read*/,false/*create*/,c} {
	if (remove_dir) r.report_guard( file_loc , ::move(real_write()) ) ;
	else            report_update( r , Access::Stat , c )             ; // fail if file does not exist, hence sensitive to existence
}
