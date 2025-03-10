// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

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

bool                                                   Record::s_static_report    = false     ;
::vmap_s<DepDigest>                                  * Record::s_deps             = nullptr   ;
::string                                             * Record::s_deps_err         = nullptr   ;
::umap_s<pair<Accesses/*accessed*/,Accesses/*seen*/>>* Record::s_access_cache     = nullptr   ; // map file to read accesses
AutodepEnv*                                            Record::_s_autodep_env     = nullptr   ; // declare as pointer to avoid late initialization
Fd                                                     Record::_s_repo_root_fd    ;
pid_t                                                  Record::_s_repo_root_pid   = 0         ;
Fd                                                     Record::_s_report_fd[2]    ;
pid_t                                                  Record::_s_report_pid[2]   = { 0 , 0 } ;

bool Record::s_is_simple(const char* file) {
	if (!file        ) return true  ;                                    // no file is simple (not documented, but used in practice)
	if (!file[0]     ) return true  ;                                    // empty file is simple
	if ( file[0]!='/') return false ;                                    // relative files are complex, in particular we dont even know relative to what (the dirfd arg is not passed in)
Restart :
	size_t pfx_sz = 0 ;
	switch (file[1]) {                                                   // recognize simple and frequent top level system directories
		case 'b' : if (strncmp(file+1,"bin" ,3)==0) pfx_sz = 5 ; break ;
		case 'd' : if (strncmp(file+1,"dev" ,3)==0) pfx_sz = 5 ; break ;
		case 'e' : if (strncmp(file+1,"etc" ,3)==0) pfx_sz = 5 ; break ;
		case 'o' : if (strncmp(file+1,"opt" ,3)==0) pfx_sz = 5 ; break ; // used to install 3rd party software, not a data dir
		case 'r' : if (strncmp(file+1,"run" ,3)==0) pfx_sz = 5 ; break ;
		case 's' : if (strncmp(file+1,"sbin",4)==0) pfx_sz = 6 ;
		/**/       if (strncmp(file+1,"sys" ,3)==0) pfx_sz = 5 ; break ;
		case 'u' : if (strncmp(file+1,"usr" ,3)==0) pfx_sz = 5 ; break ;
		case 'v' : if (strncmp(file+1,"var" ,3)==0) pfx_sz = 5 ; break ;
		case 'l' :
			if      (strncmp(file+1,"lib",3)!=0) break ;          // not in lib* => not simple
			if      (strncmp(file+4,"32" ,2)==0) pfx_sz = 7 ;     // in lib32    => simple
			else if (strncmp(file+4,"64" ,2)==0) pfx_sz = 7 ;     // in lib64    => simple
			else                                 pfx_sz = 5 ;     // in lib      => simple
		break ;                                                   // else        => not simple
		case 'p' :                                                // for /proc, must be a somewhat surgical because of jemalloc accesses and making these simple is the easiest way to avoid malloc's
			if ( strncmp(file+1,"proc",4)!=0  ) break           ; // not in /proc      => not simple
			if ( !file[5]                     ) return true     ; // /proc             => simple
			if ( file[5]!='/'                 ) return false    ; // false prefix      => not simple
			if ( file[6]>='0' && file[6]<='9' ) return false    ; // in /proc/<pid>    => not simple
			if ( strncmp(file+6,"self",4)!=0  ) goto SimpleProc ; // not in /proc/self => simple
			if ( !file[10]                    ) return true     ; // /proc/self        => simple
			if ( file[10]=='/'                ) return false    ; // in /proc/self     => not simple
		SimpleProc :
			pfx_sz = 6 ;
		break ;
	DN}
	if ( !pfx_sz                               ) return false ;   // no prefix
	if ( file[pfx_sz-1] && file[pfx_sz-1]!='/' ) return false ;   // false prefix
	int depth = 0 ;
	for ( const char* p=file+pfx_sz ; *p ; p++ ) {                                             // ensure we do not escape from top level dir
		if (p[ 0]!='/')                                                           continue ;   // not a dir boundary, go on
		if (p[-1]=='/')                                                           continue ;   // consecutive /'s, ignore
		if (p[-1]!='.') {                                               depth++ ; continue ; } // plain dir  , e.g. foo  , go down
		if (p[-2]=='/')                                                           continue ;   // dot dir    ,             stay still
		if (p[-2]!='.') {                                               depth++ ; continue ; } // plain dir  , e.g. foo. , go down
		if (p[-3]=='/') { { if (!depth) { file = p ; goto Restart ; } } depth-- ; continue ; } // dot-dot dir,             go up and restart if we get back to top-level
		/**/            {                                               depth++ ; continue ; } // plain dir  , e.g. foo.., go down
	}
	return true ;
}

void Record::_static_report(JobExecRpcReq&& jerr) const {
	switch (jerr.proc) {
		case Proc::Tmp     :
		case Proc::Trace   :
		case Proc::Confirm :
		case Proc::Guard   : break ;
		case Proc::Access :
			if      (jerr.digest.write!=No) *s_deps_err<<"unexpected write/unlink to "<<jerr.file<<'\n' ; // can have only deps from within server
			else if (!s_deps              ) *s_deps_err<<"unexpected access of "      <<jerr.file<<'\n' ; // cant have deps when no way to record them
			else {
				s_deps->emplace_back( ::move(jerr.file) , DepDigest(jerr.digest.accesses,jerr.file_info,jerr.digest.dflags,true/*parallel*/) ) ;
				s_deps->back().second.parallel = false ;                                                  // parallel bit is marked false on last of a series of parallel accesses
			}
		break ;
		default : *s_deps_err<<"unexpected "<<jerr.proc<<'\n' ;
	}
}

bool/*sent*/ Record::report_direct( JobExecRpcReq&& jerr , bool force ) const {
	jerr.chk() ;
	//
	if ( !force && !enable )                                  return false/*sent*/ ;
	if ( s_static_report   ) { _static_report(::move(jerr)) ; return true /*sent*/ ; }
	//
	//
	OMsgBuf msg { jerr } ;
	Fd fd = jerr.sync!=No ?
		s_report_fd<false/*Fast*/>()
	:	s_report_fd<true /*Fast*/>()
	;
	if (+fd) {
		try                       { msg.send(fd) ;                              }
		catch (::string const& e) { FAIL("cannot report",getpid(),jerr,':',e) ; } // this justifies panic, but we cannot report panic !
	}
	return +fd/*sent*/ ;
}

bool/*sent*/ Record::report_cached( JobExecRpcReq&& jerr , bool force ) const {
	SWEAR( jerr.proc==Proc::Access , jerr.proc ) ;
	if ( !force && !enable ) return false/*sent*/ ; // dont update cache as report is not actually done
	if (!jerr.sync) {
		SWEAR( +jerr.file , jerr.file,jerr.txt ) ;
		auto                                           [it,inserted] = s_access_cache->emplace(jerr.file,pair(Accesses(),Accesses())) ;
		::pair<Accesses/*accessed*/,Accesses/*seen*/>& entry         = it->second                                                     ;
		if (jerr.digest.write==No) {                // modifying accesses cannot be cached as we do not know what other processes may have done in between
			if (!inserted) {
				if (jerr.file_info.exists()) { if (!( jerr.digest.accesses & ~entry.second )) return false/*sent*/ ; } // no new seen accesses
				else                         { if (!( jerr.digest.accesses & ~entry.first  )) return false/*sent*/ ; } // no new      accesses
			}
			/**/                         entry.first /*accessed*/ |= jerr.digest.accesses ;
			if (jerr.file_info.exists()) entry.second/*seen    */ |= jerr.digest.accesses ;
		} else {
			entry = {~Accesses(),~Accesses()} ;                                                                        // from now on, read accesses need not be reported as file has been written
		}
	}
	return report_direct(::move(jerr)) ;
}

JobExecRpcReply Record::report_sync( JobExecRpcReq&& jerr , bool force ) const {
	thread_local ::string   codec_file   ;
	thread_local ::string   codec_ctx    ;
	thread_local ::vector_s dep_verboses ;
	//
	if (report_direct(::move(jerr),force)) {
		/**/                                   if (jerr.sync!=Yes) return {}    ;
		JobExecRpcReply reply = _get_reply() ; if (+reply        ) return reply ; // else job_exec could not contact server and generated an empty reply, process as if no job_exec
	}
	// not under lmake (typically ldebug), try to mimic server as much as possible
	switch (jerr.proc) {
		case Proc::DepVerbosePush : dep_verboses.push_back(::move(jerr.file)) ; break ;
		case Proc::CodecFile      : codec_file = ::move(jerr.file) ;            break ;
		case Proc::CodecCtx       : codec_ctx  = ::move(jerr.file) ;            break ;
		case Proc::DepVerbose : {
			::vector<pair<Bool3/*ok*/,Crc>> dep_infos ;
			for( ::string& f : dep_verboses ) dep_infos.emplace_back( Yes/*ok*/ , Crc(f) ) ;
			dep_verboses.clear() ;
			return { jerr.proc , ::move(dep_infos) } ;
		}
		case Proc::Decode :
		case Proc::Encode :
			// /!\ format must stay in sync with Codec::_s_canonicalize
			for( ::string const& line : AcFd(codec_file).read_lines(true/*no_file_ok*/) ) {
				size_t pos = 0 ;
				/**/                                             if ( line[pos++]!=' '                   ) continue ; // bad format
				::string ctx  = parse_printable<' '>(line,pos) ; if ( line[pos++]!=' ' || ctx!=codec_ctx ) continue ; // .          or bad ctx
				::string code = parse_printable<' '>(line,pos) ; if ( line[pos++]!=' '                   ) continue ; // .
				::string val  = parse_printable     (line,pos) ; if ( line[pos  ]!=0                     ) continue ; // .
				//
				if (jerr.proc==Proc::Decode) { if (code==jerr.file) return {jerr.proc,Yes/*ok*/,val } ; }
				else                         { if (val ==jerr.file) return {jerr.proc,Yes/*ok*/,code} ; }
			}
			return { jerr.proc , No/*ok*/ , "0"s } ;
	DN}
	return {} ;
}

Record::Chdir::Chdir( Record& r , Path&& path , ::string&& c ) : Solve{r,::move(path),true/*no_follow*/,false/*read*/,false/*create*/,c} {
	SWEAR(!accesses) ;                                                                                                                     // no access to last component when no_follow
	if ( s_autodep_env().auto_mkdir && file_loc==FileLoc::Repo ) mk_dir_s(at,with_slash(file)) ;                                           // in case of overlay, create dir in the view
	r.report_guard( file_loc , ::move(real_write()) , ::move(c) ) ;
}

Record::Chmod::Chmod( Record& r , Path&& path , bool exe , bool no_follow , ::string&& c ) : SolveModify{r,::move(path),no_follow,true/*read*/,false/*create*/,c} { // behave like a read-modify-write
	if (file_loc>FileLoc::Dep) return ;
	FileInfo fi { s_repo_root_fd() , real } ;
	if ( fi.exists() && exe!=(fi.tag()==FileTag::Exe) ) // only consider as a target if exe bit changes
		report_update( r , Access::Reg , ::move(c) ) ;
}

Record::Exec::Exec( Record& r , Path&& path , bool no_follow , ::string&& c ) : SolveCS{r,::move(path),no_follow,true/*read*/,false/*create*/,c} {
	if (!real) return ;
	SolveReport sr {.real=real,.file_loc=file_loc} ;
	try {
		for( auto&& [file,a] : r._real_path.exec(sr) ) r.report_access( FileLoc::Dep , { .digest={.accesses=a} , .file=::move(file) , .txt=::copy(c) } ) ;
	} catch (::string& e) { r.report_panic(::move(e)) ; }
}

Record::Lnk::Lnk( Record& r , Path&& src_ , Path&& dst_ , bool no_follow , ::string&& c ) :
	//                       no_follow   read   create
	src { r , ::move(src_) , no_follow , true  , false , c+".src" }
,	dst { r , ::move(dst_) , true      , false , true  , c+".dst" }
{
	if (src.real==dst.real) return ;                               // posix says it is nop in that case
	//
	Accesses sa = Access::Reg ; if (no_follow) sa |= Access::Lnk ; // if no_follow, a sym link may be hard linked
	src.report_dep   ( r , sa           , c+".src" ) ;
	dst.report_update( r , Access::Stat , c+".dst" ) ;             // writing to dst is sensitive to existence
}

Record::Mkdir::Mkdir( Record& r , Path&& path , ::string&& c ) : Solve{r,::move(path),true/*no_follow*/,false/*read*/,false/*create*/,c} {
	r.report_guard( file_loc , ::copy(real) , ::copy(c) ) ;
	report_dep( r , Access::Stat , ::move(c) ) ;            // fails if file exists, hence sensitive to existence
}

Record::Mount::Mount( Record& r , Path&& src_ , Path&& dst_ , ::string&& c ) :
	//                     no_follow read   create
	src { r , ::move(src_) , true  , false , false , c+".src" }
,	dst { r , ::move(dst_) , true  , false , false , c+".dst" }
{
	if (src.file_loc<=FileLoc::Dep) r.report_panic("mount from "+src.real) ;
	if (dst.file_loc<=FileLoc::Dep) r.report_panic("mount to "  +dst.real) ;
}

// note : in case the file is open WR_ONLY w/o O_TRUNC, it is true that the final content depends on the initial content.
// However :
// - if it is an official target, it is not a dep, whether you declare reading it or not
// - else, we do not compute a CRC on it and its actual content is not guaranteed. What is important in this case is that the execution of the job does not see the content.
//
static bool _no_follow(int flags) { return (flags&O_NOFOLLOW) || ( (flags&O_CREAT) && (flags&O_EXCL) )                                                        ; }
static bool _do_stat  (int flags) { return (flags&O_PATH)     || ( (flags&O_CREAT) && (flags&O_EXCL) ) || ( !(flags&O_CREAT) && (flags&O_ACCMODE)!=O_RDONLY ) ; }
static bool _do_read  (int flags) { return !(flags&O_PATH) && !(flags&O_TRUNC)                                                                                ; }
static bool _do_write (int flags) { return ( !(flags&O_PATH) && (flags&O_ACCMODE)!=O_RDONLY ) || (flags&O_TRUNC)                                              ; }
static bool _do_create(int flags) { return   flags&O_CREAT                                                                                                    ; }
//
Record::Open::Open( Record& r , Path&& path , int flags , ::string&& c ) : SolveModify{ r , ::move(path) , _no_follow(flags) , _do_read(flags) , _do_create(flags) , c } {
	if ( !file || !file[0]             ) return ;
	if ( flags&(O_DIRECTORY|O_TMPFILE) ) return ;                                                                   // we already solved, this is enough
	if ( file_loc>FileLoc::Dep         ) return ;                                                                   // fast path
	//
	bool do_stat  = _do_stat (flags) ;
	bool do_read  = _do_read (flags) ;
	bool do_write = _do_write(flags) ;
	//
	if (!( do_stat || do_read || do_write )) return ;
	//
	if (_no_follow(flags))   c += ".NF" ;
	/**/                     c += '.'   ;
	if (do_stat          ) { c += 'S'   ; if (!do_write) accesses = ~Accesses() ; else accesses |= Access::Stat ; } // if  not written, there may be a further fstat
	if (do_read          ) { c += 'R'   ; if (!do_write) accesses = ~Accesses() ; else accesses |= Access::Reg  ; } // .
	if (do_write         )   c += 'W'   ;
	//
	if (do_write) report_update( r , {} , ::move(c) ) ;
	else          report_dep   ( r , {} , ::move(c) ) ;
}

Record::Readlink::Readlink( Record& r , Path&& path , char* buf_ , size_t sz_ , ::string&& c ) : Solve{r,::move(path),true/*no_follow*/,true/*read*/,false/*create*/,c} , buf{buf_} , sz{sz_} {
	report_dep( r , Access::Lnk , ::move(c) ) ;
}

ssize_t Record::Readlink::operator()( Record& r , ssize_t len ) {
	if (at!=Backdoor::MagicFd                                    ) return len ;
	if (strncmp(file,Backdoor::MagicPfx,Backdoor::MagicPfxLen)!=0) return len ;
	::string                        cmd      = file+Backdoor::MagicPfxLen         ;
	size_t                          slash    = cmd.find('/')                      ;
	::umap_s<Backdoor::Func> const& func_tab = Backdoor::get_func_tab()           ;
	auto                            it       = func_tab.find(cmd.substr(0,slash)) ;
	if ((emulated=it!=func_tab.end())) len = it->second( r , cmd.substr(slash+1) , buf , sz ) ;
	return len ;
}

// flags is not used if exchange is not supported
Record::Rename::Rename( Record& r , Path&& src_ , Path&& dst_ , bool exchange , bool no_replace , ::string&& c ) :
	//                     no_follow read       create
	src { r , ::move(src_) , true  , true     , exchange , c+".src" }
,	dst { r , ::move(dst_) , true  , exchange , true     , c+".dst" }
{	if (src.real==dst.real) return ;                                                              // posix says in this case, it is nop
	if (exchange          ) c += "<>" ;
	// rename has not occurred yet so :
	// - files are read and unlinked in the source dir
	// - their coresponding files in the destination dir are written
	::vector_s reads  ;
	::vector_s stats  ;
	::uset_s   unlnks ;                                                                           // files listed here are read and unlinked
	::vector_s writes ;
	auto do1 = [&]( Solve const& src , Solve const& dst )->void {
		for( ::string const& f : walk(s_repo_root_fd(),src.real) ) {
			if (+src.real0) {
				if      (src.file_loc0<=FileLoc::Repo) unlnks.insert   (src.real0+f) ;
				if      (src.file_loc <=FileLoc::Dep ) reads .push_back(src.real +f) ;
			} else {
				if      (src.file_loc <=FileLoc::Repo) unlnks.insert   (src.real +f) ;
				else if (src.file_loc <=FileLoc::Dep ) reads .push_back(src.real +f) ;
			}
			if (no_replace) stats.push_back(dst.real+f) ;                                         // probe existence of destination
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
		reads.push_back(w) ;                                                                      // if a file is read, unlinked and written, it is actually not unlinked
		unlnks.erase(it) ;                                                                        // .
	}
	//
	::uset_s guards ;
	for( ::string const& w : writes ) guards.insert(dir_name_s(w)) ;
	for( ::string const& u : unlnks ) guards.insert(dir_name_s(u)) ;
	guards.erase(""s) ;
	for( ::string const& g : guards ) r.report_guard( FileLoc::Repo , no_slash(g) , c+".guard" ) ;
	//
	Pdate pd { New } ;
	for( ::string      & f : reads  )                   r.report_access( FileLoc::Dep  , { .digest={             .accesses=DataAccesses} , .date=pd , .file=::move(f) , .txt=c+".src"   } ) ;
	for( ::string      & f : stats  )                   r.report_access( FileLoc::Dep  , { .digest={             .accesses=Access::Stat} , .date=pd , .file=::move(f) , .txt=c+".probe" } ) ;
	for( ::string const& f : unlnks ) dst.to_confirm |= r.report_access( FileLoc::Repo , { .digest={.write=Maybe,.accesses=DataAccesses} , .date=pd , .file=::copy(f) , .txt=c+".unlnk" } ) ;
	for( ::string      & f : writes ) dst.to_confirm |= r.report_access( FileLoc::Repo , { .digest={.write=Maybe                       } , .date=pd , .file=::move(f) , .txt=c+".dst"   } ) ;
}

Record::Stat::Stat( Record& r , Path&& path , bool no_follow , Accesses a , ::string&& c ) :
	Solve{ r , !s_autodep_env().ignore_stat?::move(path):Path() , no_follow , true/*read*/ , false/*create*/ , c }
{
	if (no_follow) c += ".NF" ;
	if (!s_autodep_env().ignore_stat) report_dep( r , a , ::move(c) ) ;
}

Record::Symlink::Symlink( Record& r , Path&& p , ::string&& c ) : SolveModify{r,::move(p),true/*no_follow*/,false/*read*/,true/*create*/,c} {
	report_update( r , Access::Stat , ::move(c) ) ;                                                                                           // fail if file exists, hence sensitive to existence
}

Record::Unlnk::Unlnk( Record& r , Path&& p , bool remove_dir , ::string&& c ) : SolveModify{r,::move(p),true/*no_follow*/,false/*read*/,false/*create*/,c} {
	if (remove_dir) r.report_guard( file_loc , ::move(real_write()) , ::move(c) ) ;
	else            report_update( r , Access::Stat , ::move(c) )                 ; // fail if file does not exist, hence sensitive to existence
}
