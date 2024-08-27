// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <linux/limits.h>

#include "disk.hh"

#include "backdoor.hh"

#include "record.hh"

using namespace Disk ;
using namespace Time ;

// /!\ : doing call to libc during static initialization may lead impredictably to incoherent results as we may be called very early in the init process
// so, do dynamic init for all static variables

//
// Record
//

bool                                                   Record::s_static_report = false        ;
::vmap_s<DepDigest>                                  * Record::s_deps          = nullptr      ;
::string                                             * Record::s_deps_err      = nullptr      ;
::umap_s<pair<Accesses/*accessed*/,Accesses/*seen*/>>* Record::s_access_cache  = nullptr      ; // map file to read accesses
AutodepEnv*                                            Record::_s_autodep_env  = nullptr      ; // declare as pointer to avoid late initialization
Fd                                                     Record::_s_root_fd      ;
pid_t                                                  Record::_s_root_pid     = 0            ;
Fd                                                     Record::_s_report_fd    ;
pid_t                                                  Record::_s_report_pid   = 0            ;
uint64_t                                               Record::_s_id           = 0/*garbage*/ ;

bool Record::s_is_simple(const char* file) {
	if (!file        ) return true  ;                                     // no file is simple (not documented, but used in practice)
	if (!file[0]     ) return true  ;                                     // empty file is simple
	if ( file[0]!='/') return false ;                                     // relative files are complex, in particular we dont even know relative to hat (the dirfd arg is not passed in)
	size_t top_sz = 0 ;
	switch (file[1]) {                                                    // recognize simple and frequent top level system directories
		case 'b' : if (strncmp(file+1,"bin/" ,4)==0) top_sz = 5 ; break ;
		case 'd' : if (strncmp(file+1,"dev/" ,4)==0) top_sz = 5 ; break ;
		case 'e' : if (strncmp(file+1,"etc/" ,4)==0) top_sz = 5 ; break ;
		case 's' : if (strncmp(file+1,"sbin/",5)==0) top_sz = 6 ;
		/**/       if (strncmp(file+1,"sys/" ,4)==0) top_sz = 5 ; break ;
		case 'u' : if (strncmp(file+1,"usr/" ,4)==0) top_sz = 5 ; break ;
		case 'v' : if (strncmp(file+1,"var/" ,4)==0) top_sz = 5 ; break ;
		case 'l' :
			if      (strncmp(file+1,"lib",3)!=0) break ;          // not in lib* => not simple
			if      (strncmp(file+4,"/"  ,1)   ) top_sz = 5 ;     // in lib      => simple
			else if (strncmp(file+4,"32/",3)   ) top_sz = 7 ;     // in lib32    => simple
			else if (strncmp(file+4,"64/",3)   ) top_sz = 7 ;     // in lib64    => simple
		break ;                                                   // else        => not simple
		case 'p' :                                                // for /proc, must be a somewhat surgical because of jemalloc accesses and making these simple is the easiest way to avoid malloc's
			if ( strncmp(file+1,"proc/",5)!=0 ) break ;           // not in /proc      => not simple
			if ( file[6]>='0' && file[6]<='9' ) break ;           // in /proc/<pid>    => not simple
			if ( strncmp(file+6,"self/",5)==0 ) break ;           // not in /proc/self => not simple
			top_sz = 6 ;                                          // else              => simple
		break ;
		default  : ;
	}
	if (!top_sz) return false ;
	int depth = 0 ;
	for ( const char* p=file+top_sz ; *p ; p++ ) {                // ensure we do not escape from top level dir
		if (p[ 0]!='/')                          continue     ;   // not a dir boundary, go on
		if (p[-1]=='/')                          continue     ;   // consecutive /'s, ignore
		if (p[-1]!='.') { depth++ ;              continue     ; } // plain dir  , e.g. foo  , go down
		if (p[-2]=='/')                          continue     ;   // dot dir    ,             stay still
		if (p[-2]!='.') { depth++ ;              continue     ; } // plain dir  , e.g. foo. , go down
		if (p[-2]=='/') { depth-- ; if (depth<0) return false ; } // dot-dot dir,             go up and exit if we escape top level system dir
		/**/            { depth++ ;              continue     ; } // plain dir  , e.g. foo.., go down
	}
	return true ;
}

void Record::_static_report(JobExecRpcReq&& jerr) const {
	switch (jerr.proc) {
		case Proc::Access  :
			if      (jerr.digest.write!=No) for( auto& [f,dd] : jerr.files ) *s_deps_err<<"unexpected write/unlink to "<<f<<'\n' ; // can have only deps from within server
			else if (!s_deps              ) for( auto& [f,dd] : jerr.files ) *s_deps_err<<"unexpected access of "      <<f<<'\n' ; // cant have deps when no way to record them
			else {
				for( auto& [f,dd] : jerr.files ) s_deps->emplace_back( ::move(f) , DepDigest(jerr.digest.accesses,dd,jerr.digest.dflags,true/*parallel*/) ) ;
				if (+jerr.files) s_deps->back().second.parallel = false ; // parallel bit is marked false on last of a series of parallel accesses
			}
		break ;
		case Proc::Confirm :
		case Proc::Guard   :
		case Proc::Tmp     :
		case Proc::Trace   : break ;
		default            : *s_deps_err<<"unexpected "<<snake(jerr.proc)<<'\n' ;
	}
}

bool/*sent*/ Record::report_async_access( JobExecRpcReq&& jerr , bool force ) const {
	SWEAR( jerr.proc==Proc::Access || jerr.proc==Proc::DepVerbose , jerr.proc ) ;
	if ( !force && disabled ) return false/*sent*/ ;                                       // dont update cache as report is not actually done
	if (!jerr.sync) {
		bool miss = false ;
		for( auto const& [f,dd] : jerr.files ) {
			SWEAR( +f , jerr.files , jerr.txt ) ;
			auto                                           [it,inserted] = s_access_cache->emplace(f,pair(Accesses(),Accesses())) ;
			::pair<Accesses/*accessed*/,Accesses/*seen*/>& entry         = it->second                                             ;
			if (jerr.digest.write==No) {
				if (!inserted) {
					if (+dd) { if (!( jerr.digest.accesses & ~entry.second )) continue ; } // no new seen accesses
					else     { if (!( jerr.digest.accesses & ~entry.first  )) continue ; } // no new      accesses
				}
				/**/     entry.first /*accessed*/ |= jerr.digest.accesses ;
				if (+dd) entry.second/*seen    */ |= jerr.digest.accesses ;
			} else {
				entry = {~Accesses(),~Accesses()} ;                                        // from now on, read accesses need not be reported as file has been written
			}
			miss = true ;
		}
		if (!miss) return false/*sent*/ ;                                                  // modifying accesses cannot be cached as we do not know what other processes may have done in between
	}
	return report_direct(::move(jerr)) ;
}

JobExecRpcReply Record::report_sync_direct( JobExecRpcReq&& jerr , bool force ) const {
	bool sent = report_direct(::move(jerr),force) ;
	if (!jerr.sync) return {}           ;
	if (sent      ) return _get_reply() ;
	// not under lmake, try to mimic server as much as possible, but of course no real info available
	switch (jerr.proc) {
		case Proc::Decode :
		case Proc::Encode : throw "encode/decode not yet implemented without server"s ;
		default           : return {} ;
	}
}

JobExecRpcReply Record::report_sync_access( JobExecRpcReq&& jerr , bool force ) const {
	bool sent = report_async_access(::move(jerr),force) ;
	if (!jerr.sync) return {}           ;
	if (sent      ) return _get_reply() ;
	// not under lmake, try to mimic server as much as possible, but of course no real info available
	// XXX : for Encode/Decode, we should interrogate the server or explore association file directly so as to allow jobs to run with reasonable data
	switch (jerr.proc) {
		case Proc::DepVerbose : return { jerr.proc , ::vector<pair<Bool3/*ok*/,Hash::Crc>>(jerr.files.size(),{Yes,{}}) } ;
		default               : return {                                                                               } ;
	}
}

Record::Chdir::Chdir( Record& r , Path&& path , ::string&& c ) : Solve{r,::move(path),true/*no_follow*/,false/*read*/,false/*create*/,c} {
	SWEAR(!accesses) ;                                                                                                                     // no access to last component when no_follow
	if ( s_autodep_env().auto_mkdir && file_loc==FileLoc::Repo ) mk_dir_s(at,with_slash(file)) ;                                           // in case of overlay, create dir in the view
	r._report_guard( file_loc , ::move(real_write()) , ::move(c) ) ;
}
int Record::Chdir::operator()( Record& r , int rc ) {
	if (rc==0) r.chdir() ;
	return rc ;
}

Record::Chmod::Chmod( Record& r , Path&& path , bool exe , bool no_follow , ::string&& c ) : Solve{r,::move(path),no_follow,true/*read*/,false/*create*/,c} { // behave like a read-modify-write
	if (file_loc>FileLoc::Dep) return ;
	FileInfo fi { s_root_fd() , real } ;
	if ( +fi && exe!=(fi.tag()==FileTag::Exe) )
		id = r._report_update( file_loc , ::move(real) , ::move(real0) , fi , accesses|Access::Reg , ::move(c) ) ; // only consider as a target if exe bit changes
}

Record::Exec::Exec( Record& r , Path&& path , bool no_follow , ::string&& c ) : SolveCS{r,::move(path),no_follow,true/*read*/,false/*create*/,c} {
	if (!real) return ;
	SolveReport sr {.real=real,.file_loc=file_loc} ;
	try {
		for( auto&& [file,a] : r._real_path.exec(sr) ) r._report_dep( FileLoc::Dep , ::move(file) , a , ::copy(c) ) ;
	} catch (::string& e) { r.report_panic(::move(e)) ; }
}

Record::Lnk::Lnk( Record& r , Path&& src_ , Path&& dst_ , bool no_follow , ::string&& c ) :
	//                       no_follow   read   create
	src { r , ::move(src_) , no_follow , true  , false , c+".src" }
,	dst { r , ::move(dst_) , true      , false , true  , c+".dst" }
{
	if (src.real==dst.real) { dst.file_loc = FileLoc::Ext ; return ; }                                                    // posix says it is nop in that case
	//
	Accesses sa = Access::Reg ; if (no_follow) sa |= Access::Lnk ;                                                        // if no_follow, a sym link may be hard linked
	/**/ r._report_dep   ( src.file_loc , ::move(src.real) ,                     src.accesses|sa           , c+".src" ) ;
	id = r._report_update( dst.file_loc , ::move(dst.real) , ::move(dst.real0) , dst.accesses|Access::Stat , c+".dst" ) ; // fails if file exists, hence sensitive to existence
}

Record::Mkdir::Mkdir( Record& r , Path&& path , ::string&& c ) : Solve{r,::move(path),true/*no_follow*/,false/*read*/,false/*create*/,c} {
	r._report_dep  ( file_loc , ::copy(real) , accesses|Access::Stat , ::copy(c) ) ;                                                       // fails if file exists, hence sensitive to existence
	r._report_guard( file_loc , ::move(real) ,                         ::move(c) ) ;
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
static bool _ignore   (int flags) { return (flags&O_PATH) && Record::s_autodep_env().ignore_stat              ; }
static bool _no_follow(int flags) { return (flags&O_NOFOLLOW) || ( (flags&O_CREAT) && (flags&O_EXCL) )        ; }
static bool _do_stat  (int flags) { return  flags&O_PATH      || ( (flags&O_CREAT) && (flags&O_EXCL) )        ; }
static bool _do_read  (int flags) { return !(flags&O_PATH) && (flags&O_ACCMODE)!=O_WRONLY && !(flags&O_TRUNC) ; }
static bool _do_write (int flags) { return !(flags&O_PATH) && (flags&O_ACCMODE)!=O_RDONLY                     ; }
static bool _do_create(int flags) { return   flags&O_CREAT                                                    ; }
//
Record::Open::Open( Record& r , Path&& path , int flags , ::string&& c ) :
	Solve{ r , !_ignore(flags)?::move(path):Path() , _no_follow(flags) , _do_read(flags) , _do_create(flags) , c }
{
	if ( !file || !file[0]             ) return ;                                                        // includes ignore_stat cases
	if ( flags&(O_DIRECTORY|O_TMPFILE) ) return ;                                                        // we already solved, this is enough
	if ( file_loc>FileLoc::Dep         ) return ;                                                        // fast path
	//
	bool do_stat  = _do_stat (flags) ;
	bool do_read  = _do_read (flags) ;
	bool do_write = _do_write(flags) ;
	//
	c << '.' << flags << '.' ;
	if (do_stat ) { c += 'S' ; if (!do_write) accesses = ~Accesses() ; else accesses |= Access::Stat ; } // if  not written, user can access all info with a further fstat, which is not piggy-backed
	if (do_read ) { c += 'R' ; if (!do_write) accesses = ~Accesses() ; else accesses |= Access::Reg  ; } // .
	if (do_write)   c += 'W' ;
	//
	if      ( do_write           ) id = r._report_update( file_loc , ::move(real) , ::move(real0) , accesses , ::move(c) ) ;
	else if ( do_read || do_stat )      r._report_dep   ( file_loc , ::move(real) ,                 accesses , ::move(c) ) ;
}

Record::Readlink::Readlink( Record& r , Path&& path , char* buf_ , size_t sz_ , ::string&& c ) : Solve{r,::move(path),true/*no_follow*/,true/*read*/,false/*create*/,c} , buf{buf_} , sz{sz_} {
	r._report_dep( file_loc , ::move(real) , accesses|Access::Lnk , ::move(c) ) ;
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
Record::Rename::Rename( Record& r , Path&& src_ , Path&& dst_ , bool exchange_ , bool no_replace , ::string&& c ) :
	//                         no_follow read       create
	src     { r , ::move(src_) , true  , true      , false , c+".src" }
,	dst     { r , ::move(dst_) , true  , exchange_ , true  , c+".dst" }
,	exchange{ exchange_                                               }
{
	if (src.real==dst.real) return ;                                                                                          // posix says in this case, it is nop
	if (exchange          ) c += "<>" ;
	// rename has not occurred yet so for each dir :
	// - files are read and unlinked
	// - their coresponding files in the other dir are written
	// also scatter files into 3 categories :
	// - files that are unlinked         (typically all files in src  dir, but not necesssarily all of them)
	// - files that are written          (          all files in dst  dir                                  )
	// - files that are read and written (              files in both dirs in case of exchange             )
	// in case of exchange, both dirs are both src and dst
	::vector_s reads  ;
	::uset_s   unlnks ;
	::vector_s writes ;
	if ( src.file_loc<=FileLoc::Dep || dst.file_loc==FileLoc::Repo ) {
		::vector_s sfxs = walk(s_root_fd(),src.real) ;                                                                        // list only accessible files
		if ( src.file_loc<=FileLoc::Dep  && +src.real0 ) for( ::string const& s : sfxs ) unlnks.insert   ( src.real + s ) ;
		if ( src.file_loc<=FileLoc::Dep  && !src.real0 ) for( ::string const& s : sfxs ) reads .push_back( src.real + s ) ;   // overlaid files are not unlinked
		if ( dst.file_loc==FileLoc::Repo               ) for( ::string const& d : sfxs ) writes.push_back( dst.real + d ) ;
	}
	if ( exchange && ( dst.file_loc<=FileLoc::Dep || src.file_loc==FileLoc::Repo ) ) {
		::vector_s sfxs = walk(s_root_fd(),dst.real) ;                                                                        // list only accessible files
		if ( dst.file_loc<=FileLoc::Dep  && +dst.real0 ) for( ::string const& s : sfxs ) unlnks.insert   ( dst.real + s ) ;
		if ( dst.file_loc<=FileLoc::Dep  && !dst.real0 ) for( ::string const& s : sfxs ) reads .push_back( dst.real + s ) ;   // overlaid files are not unlinked
		if ( src.file_loc==FileLoc::Repo               ) for( ::string const& d : sfxs ) writes.push_back( src.real + d ) ;
	}
	for( ::string const& w : writes ) {
		auto it = unlnks.find(w) ;
		if (it==unlnks.end()) continue ;
		reads.push_back(w) ;
		unlnks.erase(it) ;
	}
	//                                                                                                 unlnk
	if (+reads    )            r._report_deps   (                 ::move   (reads   ) , DataAccesses , false , c+".src"   ) ; // file_loc's already handled
	if (+unlnks   ) unlnk_id = r._report_deps   (                 mk_vector(unlnks  ) , DataAccesses , true  , c+".unlnk" ) ; // .
	if (no_replace)            r._report_dep    ( dst.file_loc  , ::copy   (dst.real) , Access::Stat ,         c+".probe" ) ;
	if (+writes   ) write_id = r._report_targets(                 ::move   (writes  ) ,                        c+".dst"   ) ; // .
	/**/                       r._report_guard  ( src.file_loc  , ::move   (src.real) ,                        c+".src"   ) ; // only necessary if renamed dirs, perf is low prio as not that frequent
	/**/                       r._report_guard  ( dst.file_loc  , ::move   (dst.real) ,                        c+".dst"   ) ;
}

Record::Stat::Stat( Record& r , Path&& path , bool no_follow , Accesses a , ::string&& c ) :
	Solve{ r , !s_autodep_env().ignore_stat?::move(path):Path() , no_follow , true/*read*/ , false/*create*/ , c }
{
	if (!s_autodep_env().ignore_stat) r._report_dep( file_loc , ::move(real) , accesses|a , ::move(c) ) ;
}

Record::Symlink::Symlink( Record& r , Path&& p , ::string&& c ) : Solve{r,::move(p),true/*no_follow*/,false/*read*/,true/*create*/,c} {
	id = r._report_update( file_loc , ::move(real) , ::move(real0) , accesses|Access::Stat , ::move(c) ) ;                              // fail if file exists, hence sensitive to existence
}

Record::Unlnk::Unlnk( Record& r , Path&& p , bool remove_dir , ::string&& c ) : Solve{r,::move(p),true/*no_follow*/,false/*read*/,false/*create*/,c} {
	if (remove_dir) {      r._report_guard( file_loc , ::move(real_write()) , ::move(c) ) ; file_loc = FileLoc::Ext ; }
	else              id = r._report_unlnk( file_loc , ::move(real_write()) , ::move(c) ) ;
}
