// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <linux/limits.h>

#include "disk.hh"

#include "record.hh"

using namespace Disk ;
using namespace Time ;

// /!\ : doing any call to libc during static initialization leads to incoherent results
// so, do  dynamic init for all static variables

//
// Record
//

::vmap_s<Accesses>* Record::s_deps          = nullptr ;
::string          * Record::s_deps_err      = nullptr ;
bool                Record::s_static_report = false   ;
AutodepEnv*         Record::_s_autodep_env  = nullptr ; // declare as pointer to avoid late initialization
Fd                  Record::_s_root_fd      ;

bool Record::s_is_simple(const char* file) {
	if (!file        ) return true  ;                                     // no file is simple (not documented, but used in practice)
	if (!file[0]     ) return true  ;                                     // empty file is simple
	if ( file[0]!='/') return false ;                                     // relative files are complex, in particular we dont even know relative to hat (the dirfd arg is not passed in)
	size_t top_sz = 0 ;
	switch (file[1]) {                                                    // recognize simple and frequent top level system directories
		case 'b' : if (strncmp(file+1,"bin/" ,4)==0) top_sz = 5 ; break ;
		case 'd' : if (strncmp(file+1,"dev/" ,4)==0) top_sz = 5 ; break ;
		case 'e' : if (strncmp(file+1,"etc/" ,4)==0) top_sz = 5 ; break ;
		case 's' : if (strncmp(file+1,"sys/" ,4)==0) top_sz = 5 ; break ;
		case 'u' : if (strncmp(file+1,"usr/" ,4)==0) top_sz = 5 ; break ;
		case 'v' : if (strncmp(file+1,"var/" ,4)==0) top_sz = 5 ; break ;
		case 'l' :
			if      (strncmp(file+1,"lib",3)!=0) break ;          // not in lib* => not simple
			if      (strncmp(file+4,"/"  ,1)   ) top_sz = 5 ;     // in lib      => simple
			else if (strncmp(file+4,"32/",3)   ) top_sz = 7 ;     // in lib32    => simple
			else if (strncmp(file+4,"64/",3)   ) top_sz = 7 ;     // in lib64    => simple
		break ;
		case 'p' :                                                // for /proc, must be a somewhat surgical because of jemalloc accesses and making these simple is the easiest way to avoid malloc's
			if ( strncmp(file+1,"proc/",5)!=0 ) break ;           // not in /proc      => not simple
			if ( file[6]>='0' && file[6]<='9' ) break ;           // in /proc/<pid>    => not simple
			if ( strncmp(file+6,"self/",5)==0 ) break ;           // not in /proc/self => not simple
			top_sz = 6 ;
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
		case JobExecRpcProc::Access  :
			if      (jerr.digest.write) for( auto& [f,dd] : jerr.files ) append_to_string(*s_deps_err,"unexpected write to " ,f,'\n') ;
			else if (jerr.unlnk       ) for( auto& [f,dd] : jerr.files ) append_to_string(*s_deps_err,"unexpected unlink of ",f,'\n') ;
			else if (!s_deps          ) for( auto& [f,dd] : jerr.files ) append_to_string(*s_deps_err,"unexpected access of ",f,'\n') ;
			else                        for( auto& [f,dd] : jerr.files ) s_deps->emplace_back(::move(f),jerr.digest.accesses) ;
		break ;
		case JobExecRpcProc::Confirm :
		case JobExecRpcProc::Guard   :
		case JobExecRpcProc::Tmp     :
		case JobExecRpcProc::Trace   : break ;
		default                      : append_to_string(*s_deps_err,"unexpected proc ",jerr.proc,'\n') ;
	}
}

void Record::_report_access( JobExecRpcReq&& jerr ) const {
	SWEAR( jerr.proc==JobExecRpcProc::Access , jerr.proc ) ;
	if (!jerr.sync) {
		bool miss = false              ;
		bool idle = jerr.digest.idle() ;
		for( auto const& [f,dd] : jerr.files ) {
			SWEAR( +f , jerr.txt ) ;
			auto                                           [it,inserted] = _access_cache.emplace(f,pair(Accesses::None,Accesses::None)) ;
			::pair<Accesses/*accessed*/,Accesses/*seen*/>& entry         = it->second                                                   ;
			if ( !inserted && idle ) {
				if (+dd) { if (!( jerr.digest.accesses & ~entry.second )) continue ; } // no new seen accesses
				else     { if (!( jerr.digest.accesses & ~entry.first  )) continue ; } // no new      accesses
			}
			if (!idle) {
				entry = {Accesses::All,Accesses::All} ;                                // from now on, read accesses need not be reported as file has been written
			} else {
				/**/     entry.first /*accessed*/ |= jerr.digest.accesses ;
				if (+dd) entry.second/*seen    */ |= jerr.digest.accesses ;
			}
			miss = true ;
		}
		if (!miss) return ;                                                            // modifying accesses cannot be cached as we do not know what other processes may have done in between
	}
	_report(::move(jerr)) ;
}

Record::SolveReport Record::_solve( Path& path , bool no_follow , bool read , ::string const& comment ) {
	if (!path.file) return {} ;
	SolveReport sr = _real_path.solve(path.at,path.file,no_follow) ;
	for( ::string& lnk : sr.lnks ) _report_dep( ::move(lnk        ) , file_date(s_root_fd(),lnk) , Access::Lnk , comment+".lnk" ) ; // lnk exists
	if ( !read && +sr.last_lnk   ) _report_dep( ::move(sr.last_lnk) , Ddate()                    , Access::Lnk , comment+".lst" ) ; // sr.lastlnk does not exist and we have not looked at errno ...
	sr.lnks.clear() ;                                                                                                               // ... so we can report an unseen dep
	if ( sr.mapped && path.file && path.file[0] ) {                                                                                 // else path is ok as it is
		if      (is_abs(sr.real)) { if (+sr.real) path.allocate(sr.real) ; else path.share("/") ;      }                            // dont share real with file as real may be moved
		else if (path.has_at    )   path.allocate( s_root_fd() , sr.real                           ) ;
		else                        path.allocate( to_string(s_autodep_env().root_dir,'/',sr.real) ) ;
	}
	path.file_loc = sr.file_loc ;
	return sr ;
}

JobExecRpcReply Record::direct(JobExecRpcReq&& jerr) {
	if (s_active()) {
		bool sync = jerr.sync ; // save before moving jerr
		_report(::move(jerr)) ;
		if (sync) return _get_reply() ;
		else      return {}           ;
	} else {
		// not under lmake, try to mimic server as much as possible, but of course no real info available
		// XXX : for Encode/Decode, we should interrogate the server or explore association file directly so as to allow jobs to run with reasonable data
		if ( jerr.sync && jerr.proc==JobExecRpcProc::DepInfos) return { jerr.proc , ::vector<pair<Bool3/*ok*/,Hash::Crc>>(jerr.files.size(),{Yes,{}}) } ;
		else                                                   return {                                                                               } ;
	}
}

::ostream& operator<<( ::ostream& os , Record::Path const& p ) {
	if (p.at!=Fd::Cwd) os <<'@'<< p.at.fd <<':'         ;
	return             os << p.file <<':' << p.file_loc ;
}

Record::Chdir::Chdir( Record& r , Path&& path , ::string&& c ) : Solve{r,::move(path),true/*no_follow*/,false/*read*/,true/*allow_tmp_map*/,c} {
	if ( s_autodep_env().auto_mkdir && file_loc==FileLoc::Repo ) {
		Disk::mkdir(s_root_fd(),real) ;
		r._report_guard(::move(real),::move(c)) ;
	}
}
int Record::Chdir::operator()( Record& r , int rc , pid_t pid ) {
	if (rc!=0) return rc ;
	if (pid  ) r.chdir(Disk::read_lnk("/proc/"+::to_string(pid)+"/cwd").c_str()) ;
	else       r.chdir(Disk::cwd()                                     .c_str()) ;
	return rc ;
}

Record::Chmod::Chmod( Record& r , Path&& path , bool exe , bool no_follow , ::string&& c ) : Solve{r,::move(path),no_follow,true/*read*/,true/*allow_tmp_map*/,c} { // behave like a read-modify-write
	if (file_loc>FileLoc::Dep) return ;
	FileInfo fi{s_root_fd(),real} ;
	if ( !fi || exe==(fi.tag==FileTag::Exe) ) file_loc = FileLoc::Ext ;                                                       // only consider as a target if exe bit changes
	if ( file_loc==FileLoc::Repo            ) r._report_update( ::move(real) , fi.date , accesses|Access::Reg , ::move(c) ) ; // file date is updated if created, use original date
}
int Record::Chmod::operator()( Record& r , int rc ) {
	if (file_loc==FileLoc::Repo) r._report_confirm(false/*unlnk*/,rc>=0) ;
	return rc ;
}

Record::Exec::Exec( Record& r , Path&& path , bool no_follow , ::string&& c ) : Solve{r,::move(path),no_follow,true/*read*/,true/*allow_tmp_map*/,c} {
	SolveReport sr {.real=real,.file_loc=file_loc} ;
	try {
		for( auto&& [file,a] : r._real_path.exec(sr) ) r._report_dep( ::move(file) , a , ::move(c) ) ;
	} catch (::string const& e) { r.report_panic(e) ; }
}

Record::Lnk::Lnk( Record& r , Path&& src_ , Path&& dst_ , bool no_follow , ::string&& c ) :
	src { r , ::move(src_) , no_follow         , true /*read*/ , true/*allow_tmp_map*/ , c+".src" }
,	dst { r , ::move(dst_) , true/*no_follow*/ , false/*read*/ , true/*allow_tmp_map*/ , c+".dst" }
{
	if (src.real==dst.real) { dst.file_loc = FileLoc::Ext ; return ; }                                  // posix says it is nop in that case
	//
	Accesses a = Access::Reg ; if (no_follow) a |= Access::Lnk ;                                        // if no_follow, a sym link may be hard linked
	if (src.file_loc<=FileLoc::Dep ) r._report_dep   ( ::move(src.real) , src.accesses|a , c+".src" ) ;
	if (dst.file_loc==FileLoc::Repo) r._report_target( ::move(dst.real) ,                  c+".dst" ) ;
}
int Record::Lnk::operator()( Record& r , int rc ) {
	bool ok = rc>=0 ;
	if      ( dst.file_loc==FileLoc::Repo       ) r._report_confirm(false/*unlnk*/,ok) ;
	else if ( dst.file_loc==FileLoc::Tmp  && ok ) r._report_tmp    (                 ) ;
	return rc ;
}

Record::Mkdir::Mkdir( Record& r , Path&& path , ::string&& c ) : Solve{r,::move(path),true/*no_follow*/,false/*read*/,true/*allow_tmp_map*/,c} {
	if (file_loc==FileLoc::Repo) r._report_guard(::move(real),::move(c)) ;
}

static inline bool _do_stat (int flags) { return flags&O_PATH                                                        ; }
static inline bool _do_read (int flags) { return !_do_stat(flags) && (flags&O_ACCMODE)!=O_WRONLY && !(flags&O_TRUNC) ; }
static inline bool _do_write(int flags) { return !_do_stat(flags) && (flags&O_ACCMODE)!=O_RDONLY                     ; }
Record::Open::Open( Record& r , Path&& path , int flags , ::string&& c ) :
	Solve{ r , ::move(path) , bool(flags&O_NOFOLLOW) , _do_read(flags) , true/*allow_tmp_map*/ , to_string(c,::hex,'.',flags) }
,	do_write{_do_write(flags)}
{
	bool do_read = _do_read(flags) ;
	bool do_stat = _do_stat(flags) ;
	//
	if ( flags&(O_DIRECTORY|O_TMPFILE)          ) { file_loc=FileLoc::Ext ; return ; }           // we already solved, this is enough
	if ( do_stat && s_autodep_env().ignore_stat ) { file_loc=FileLoc::Ext ; return ; }
	if ( file_loc>FileLoc::Dep                  )                           return ;
	//
	if (!do_write) {
		if      (do_read) r._report_dep   ( ::move(real) , accesses|Access::Reg  , c+".rd"   ) ;
		else if (do_stat) r._report_dep   ( ::move(real) , accesses|Access::Stat , c+".path" ) ;
	} else if (file_loc==FileLoc::Repo) {
		if      (do_read) r._report_update( ::move(real) , accesses|Access::Reg  , c+".upd"  ) ; // file date is updated if created, use original date
		else              r._report_target( ::move(real) ,                         c+".wr"   ) ; // .
	} else {
		if      (do_read) r._report_dep   ( ::move(real) , accesses|Access::Reg  , c+".upd"  ) ; // in src dirs, only the read side is reported
	}
}
int Record::Open::operator()( Record& r , int rc ) {
	if (do_write) {
		bool ok = rc>=0 ;
		switch (file_loc) {
			case FileLoc::Repo :         r._report_confirm(false/*unlnk*/,ok) ; break ;
			case FileLoc::Tmp  : if (ok) r._report_tmp    (                 ) ; break ;
			default : ;
		}
	}
	return rc ;
}

Record::Read::Read( Record& r , Path&& path , bool no_follow , bool keep_real , bool allow_tmp_map , ::string&& c ) : Solve{r,::move(path),no_follow,true/*read*/,allow_tmp_map,c} {
	if (file_loc>FileLoc::Dep) return ;
	if (keep_real            ) r._report_dep( ::copy(real) , accesses|Access::Reg , ::move(c) ) ;
	else                       r._report_dep( ::move(real) , accesses|Access::Reg , ::move(c) ) ;
}

Record::Readlnk::Readlnk( Record& r , Path&& path , char* buf_ , size_t sz_ , ::string&& c ) : Solve{r,::move(path),true/*no_follow*/,true/*read*/,true/*allow_tmp_map*/,c} , buf{buf_} , sz{sz_} {
	if (file_loc<=FileLoc::Dep) r._report_dep( ::copy(real) , accesses|Access::Lnk , ::move(c) ) ;
}

ssize_t Record::Readlnk::operator()( Record& , ssize_t len ) {
	if ( Record::s_has_tmp_view() && file_loc==FileLoc::Proc && len>0 ) { // /proc may contain links to tmp_dir that we must show to job as pointing to tmp_view
		::string const& tmp_dir  = s_autodep_env().tmp_dir  ;
		::string const& tmp_view = s_autodep_env().tmp_view ;
		size_t          ulen     = len                      ;
		if (ulen<sz) {                                                                                                                                      // easy, we have the full info
			if (::string_view(buf,ulen).starts_with(tmp_dir) && (ulen==tmp_dir.size()||buf[tmp_dir.size()]=='/') ) {                                        // match, do the subtitution
				if (tmp_view.size()>tmp_dir.size()) ::memmove( buf+tmp_view.size() , buf+tmp_dir.size() , ::min(ulen-tmp_dir.size(),sz-tmp_view.size()) ) ; // memmove takes care of overlap
				/**/                                ::memcpy ( buf                 , tmp_view.c_str()   ,                              tmp_view.size()  ) ; // memcopy does not but is fast
				if (tmp_view.size()<tmp_dir.size()) ::memmove( buf+tmp_view.size() , buf+tmp_dir.size() ,       ulen-tmp_dir.size()                     ) ; // no risk of buffer overflow
				len = ::min( ulen+tmp_view.size()-tmp_dir.size() , sz ) ;
			}
		} else {                                                                                                 // difficult, we only have part of the info, this should be rare, no need to optimize
			::string target = ::read_lnk(real) ;                                                                 // restart access from scratch, we enough memory
			if ( target.starts_with(tmp_dir) && (target.size()==tmp_dir.size()||target[tmp_dir.size()]=='/') ) {
				/**/                    ::memcpy( buf                 , tmp_view.c_str()              , ::min(sz                ,tmp_view.size()             ) ) ; // no overlap
				if (sz>tmp_view.size()) ::memcpy( buf+tmp_view.size() , target.c_str()+tmp_dir.size() , ::min(sz-tmp_view.size(),target.size()-tmp_dir.size()) ) ; // .
				len = ::min( target.size()+tmp_view.size()-tmp_dir.size() , sz ) ;
			}
		}
	}
	return len ;
}

// flags is not used if exchange is not supported
Record::Rename::Rename( Record& r , Path&& src_ , Path&& dst_ , bool exchange , ::string&& c ) :
	src{ r , ::move(src_) , true/*no_follow*/ , true/*read*/ , true/*allow_tmp_map*/ , c+".src" }
,	dst{ r , ::move(dst_) , true/*no_follow*/ , exchange     , true/*allow_tmp_map*/ , c+".dst" }
{
	if (src.real==dst.real) return ;                                                                                         // posix says in this case, it is nop
	if (exchange          ) c += "<>" ;
	SWEAR(!(src.accesses&~DataAccesses)) ;                                                                                   // ensure we need not add _report_dep for last level solve access
	SWEAR(!(dst.accesses&~DataAccesses)) ;                                                                                   // .
	// rename has not occurred yet so for each directory :
	// - files are read and unlinked
	// - their coresponding files in the other directory are written
	// also scatter files into 3 categories :
	// - files that are unlinked         (typically all files in src  dir, but not necesssarily all of them)
	// - files that are written          (          all files in dst  dir                                  )
	// - files that are read and written (              files in both dirs in case of exchange             )
	// in case of exchange, both dirs are both src and dst
	::vector_s reads  ;
	::uset_s   unlnks ;
	::vector_s writes ;
	if ( src.file_loc<=FileLoc::Dep || dst.file_loc==FileLoc::Repo ) {
		::vector_s sfxs = walk(s_root_fd(),src.real) ;                                                                       // list only accessible files
		if (src.file_loc<=FileLoc::Dep ) for( ::string const& s : sfxs ) unlnks.insert   ( src.real + s ) ;
		if (dst.file_loc==FileLoc::Repo) for( ::string const& d : sfxs ) writes.push_back( dst.real + d ) ;
	}
	if ( exchange && ( dst.file_loc<=FileLoc::Dep || src.file_loc==FileLoc::Repo ) ) {
		::vector_s sfxs = walk(s_root_fd(),dst.real) ;                                                                       // list only accessible files
		if (dst.file_loc<=FileLoc::Dep ) for( ::string const& s : sfxs ) unlnks.insert   ( dst.real + s ) ;
		if (src.file_loc==FileLoc::Repo) for( ::string const& d : sfxs ) writes.push_back( src.real + d ) ;
	}
	for( ::string const& w : writes ) {
		auto it = unlnks.find(w) ;
		if (it==unlnks.end()) continue ;
			reads.push_back(w) ;
			unlnks.erase(it) ;
	}
	has_unlnks = +unlnks ;
	has_writes = +writes ;
	if (+reads                     ) r._report_deps   ( ::move   (reads   ) , DataAccesses , false/*unlnk*/ , c+".src"   ) ;
	if (+unlnks                    ) r._report_deps   ( mk_vector(unlnks  ) , DataAccesses , true /*unlnk*/ , c+".unlnk" ) ;
	if (+writes                    ) r._report_targets( ::move   (writes  ) ,                                 c+".dst"   ) ;
	if (src.file_loc==FileLoc::Repo) r._report_guard  ( ::move   (src.real) ,                                 c+".src"   ) ; // only necessary if renamed dirs, perf is low prio as not that frequent
	if (dst.file_loc==FileLoc::Repo) r._report_guard  ( ::move   (dst.real) ,                                 c+".dst"   ) ; // .
}
int Record::Rename::operator()( Record& r , int rc ) {
	if (has_unlnks) r._report_confirm(true /*unlnk*/,rc>=0) ;
	if (has_writes) r._report_confirm(false/*unlnk*/,rc>=0) ;
	return rc ;
}

Record::Stat::Stat( Record& r , Path&& path , bool no_follow , ::string&& c ) : Solve{r,::move(path),no_follow,true/*read*/,true/*allow_tmp_map*/,c} {
	if ( !s_autodep_env().ignore_stat && file_loc<=FileLoc::Dep ) r._report_dep( ::move(real) , Access::Stat , ::move(c) ) ;
}

Record::Symlnk::Symlnk( Record& r , Path&& p , ::string&& c ) : Solve{r,::move(p),true/*no_follow*/,false/*read*/,true/*allow_tmp_map*/,c} {
	if (file_loc==FileLoc::Repo) r._report_target( ::move(real) , ::move(c) ) ;
}
int Record::Symlnk::operator()( Record& r , int rc ) {
	if (file_loc==FileLoc::Repo) r._report_confirm(false/*unlnk*/,rc>=0) ;
	return rc ;
}

Record::Unlnk::Unlnk( Record& r , Path&& p , bool remove_dir , ::string&& c ) : Solve{r,::move(p),true/*no_follow*/,false/*read*/,true/*allow_tmp_map*/,c} {
	if (file_loc!=FileLoc::Repo)   return ;
	if (remove_dir         ) { r._report_guard( ::move(real) , ::move(c) ) ; file_loc = FileLoc::Ext ; } // we can move real as it will not be used in operator()
	else                       r._report_unlnk( ::copy(real) , ::move(c) ) ;
}
int Record::Unlnk::operator()( Record& r , int rc ) {
	if (file_loc==FileLoc::Repo) r._report_confirm(true/*unlnk*/,rc>=0) ;
	return rc ;
}
