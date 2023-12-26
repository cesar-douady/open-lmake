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

bool Record::s_is_simple(const char* file) {
	if (!file        ) return true  ;                                          // no file is simple (not documented, but used in practice)
	if (!file[0]     ) return true  ;                                          // empty file is simple
	if ( file[0]!='/') return false ;                                          // relative files are complex
	size_t top_sz = 0 ;
	switch (file[1]) {                                                         // recognize simple and frequent top level system directories
		case 'b' : if (strncmp(file+1,"bin/",4)==0) top_sz = 5 ; break ;
		case 'd' : if (strncmp(file+1,"dev/",4)==0) top_sz = 5 ; break ;
		case 'e' : if (strncmp(file+1,"etc/",4)==0) top_sz = 5 ; break ;
		case 's' : if (strncmp(file+1,"sys/",4)==0) top_sz = 5 ; break ;
		case 'u' : if (strncmp(file+1,"usr/",4)==0) top_sz = 5 ; break ;
		case 'v' : if (strncmp(file+1,"var/",4)==0) top_sz = 5 ; break ;
		case 'l' :
			if (strncmp(file+1,"lib",3)!=0) break ;
			//
			if      (strncmp(file+4,"/"  ,1)) top_sz = 5 ;
			else if (strncmp(file+4,"32/",3)) top_sz = 7 ;
			else if (strncmp(file+4,"64/",3)) top_sz = 7 ;
		break ;
		default  : ;
	}
	if (!top_sz) return false ;
	int depth = 0 ;
	for ( const char* p=file+top_sz ; *p ; p++ ) {                             // ensure we do not escape from top level dir
		if (p[ 0]!='/') {                        continue ;     }              // not a dir boundary, go on
		if (p[-1]=='/') {                        continue ;     }              // consecutive /'s, ignore
		if (p[-1]!='.') { depth++ ;              continue ;     }              // plain dir  , e.g. foo  , go down
		if (p[-2]=='/') {                        continue ;     }              // dot dir    ,             stay still
		if (p[-2]!='.') { depth++ ;              continue ;     }              // plain dir  , e.g. foo. , go down
		if (p[-2]=='/') { depth-- ; if (depth<0) return false ; }              // dot-dot dir,             go up and exit if we escape top level system dir
		/**/            { depth++ ;              continue ;     }              // plain dir  , e.g. foo.., go down
	}
	return true ;
}

AutodepEnv* Record::_s_autodep_env = nullptr ;                                 // declare as pointer to avoid late initialization
Fd          Record::_s_root_fd     ;
Fd          Record::_s_report_fd   ;

void Record::_report_access( JobExecRpcReq const& jerr ) const {
	SWEAR( jerr.proc==JobExecRpcProc::Access , jerr.proc ) ;
	if (!jerr.sync) {
		bool miss = false              ;
		bool idle = jerr.digest.idle() ;
		for( auto const& [f,dd] : jerr.files ) {
			SWEAR( +f , jerr.comment ) ;
			auto [it,inserted] = access_cache.emplace(f,Accesses::None) ;
			if ( idle && !( jerr.digest.accesses & ~it->second ) ) continue ;  // no new accesses
			it->second |= idle ? jerr.digest.accesses : Accesses::All ;
			miss = true ;
		}
		if (!miss) return ;                                                    // modifying accesses cannot be cached as we do not know what other processes may have done in between
	}
	_s_report(jerr) ;
}

Record::SolveReport Record::_solve( Path& path , bool no_follow , bool read , ::string const& comment ) {
	if (!path.file) return {} ;
	SolveReport sr = real_path.solve(path.at,path.file,no_follow) ;
	for( ::string& lnk : sr.lnks ) _report_dep( ::move(lnk        ) , file_date(s_root_fd(),lnk        ) , Access::Lnk , comment+".lnk" ) ;
	if ( !read && +sr.last_lnk   ) _report_dep( ::move(sr.last_lnk) , file_date(s_root_fd(),sr.last_lnk) , Access::Lnk , comment+".dir" ) ; // if read, we indirectly depend on last_lnk as uphill
	sr.lnks.clear() ;
	if ( sr.mapped && path.file && path.file[0] ) {                                                                // else path is ok as it is
		if      (is_abs(sr.real)) path.share   (               +sr.real?sr.real.c_str():"/"                    ) ;
		else if (path.has_at    ) path.share   ( s_root_fd() ,          sr.real.c_str()                        ) ;
		else                      path.allocate(               to_string(s_autodep_env().root_dir,'/',sr.real) ) ;
	}
	path.kind = sr.kind ;
	return sr ;
}

JobExecRpcReply Record::backdoor(JobExecRpcReq&& jerr) {
	if (jerr.proc>=JobExecRpcProc::HasFile) {
		SWEAR(jerr.auto_date) ;
		bool            some_in_tmp = false               ;
		::string        c           = jerr.comment+".lnk" ;
		::vmap_s<Ddate> files       ;
		for( auto const& [f,dd] : jerr.files ) {
			SWEAR(+f) ;
			Path        p  = {Fd::Cwd,f.c_str()}                                      ; // we can play with the at field at will
			SolveReport sr = _solve( p , jerr.no_follow , +jerr.digest.accesses , c ) ;
			if ( jerr.digest.write ? sr.kind==Kind::Repo : sr.kind<=Kind::Dep ) files.emplace_back( sr.real , file_date(s_root_fd(),sr.real) ) ;
			some_in_tmp |= sr.kind==Kind::Tmp ;
		}
		jerr.files     = ::move(files) ;
		jerr.auto_date = false         ;                                       // files are now real and dated
		if ( some_in_tmp && jerr.digest.write ) _report_tmp() ;
	}
	jerr.date = Pdate::s_now() ;                                               // ensure date is posterior to links encountered while solving
	if (jerr.proc==JobExecRpcProc::Access) _report_access(jerr) ;
	else                                   _s_report     (jerr) ;
	if (jerr.sync) return _s_get_reply() ;
	else           return {}             ;
}

ssize_t Record::backdoor( const char* msg , char* buf , size_t sz ) {
	JobExecRpcReq   jerr  = IMsgBuf::s_receive<JobExecRpcReq>(msg) ;
	JobExecRpcReply reply = backdoor(::move(jerr))                 ;
	//
	if (sz<sizeof(MsgBuf::Len)) return 0 ;                                     // we cant report anything if we dont even have the necessary size to report the needed size
	//
	::string reply_str = OMsgBuf::s_send(reply)                                        ;
	ssize_t  len       = reply_str.size()<=sz ? reply_str.size() : sizeof(MsgBuf::Len) ; // if not enough room for data, just report the size we needed
	::memcpy( buf , reply_str.data() , len ) ;
	return len ;
}

::ostream& operator<<( ::ostream& os , Record::Path const& p ) {
	if (p.at!=Fd::Cwd) os <<'@'<< p.at.fd <<':' ;
	return             os << p.file             ;
}

Record::ChDir::ChDir( Record& r , Path&& path ) : Solve{r,::move(path),true/*no_follow*/,false/*read*/} {
	if ( s_autodep_env().auto_mkdir && kind==Kind::Repo ) {
		Disk::mkdir(s_root_fd(),real) ;
		r._report_guard(::move(real),"chdir") ;
	}
}
int Record::ChDir::operator()( Record& r , int rc , pid_t pid ) {
	if (rc!=0) return rc ;
	if (pid  ) r.chdir(Disk::read_lnk("/proc/"+::to_string(pid)+"/cwd").c_str()) ;
	else       r.chdir(Disk::cwd()                                     .c_str()) ;
	return rc ;
}

Record::Chmod::Chmod( Record& r , Path&& path , bool exe , bool no_follow , ::string const& comment_ ) : Solve{r,::move(path),no_follow,true/*read*/,comment_} { // behave like a read-modify-write
	if (kind>Kind::Dep) return ;
	FileInfo fi{s_root_fd(),real} ;
	if ( !fi || exe==(fi.tag==FileTag::Exe) ) kind = Kind::Ext ;                                          // only consider as a target if exe bit changes, file date is updated, capture date before
	if (kind==Kind::Repo) r._report_update( ::string(real) , fi.date , accesses|Access::Reg , comment ) ; // file date is updated if created, use original date
}
int Record::Chmod::operator()( Record& r , int rc ) {
	if (kind==Kind::Repo) r._report_confirm( ::move(real) , rc>=0 ) ;
	return rc ;
}

Record::Exec::Exec( Record& r , Path&& path , bool no_follow , ::string const& comment_ ) : Solve{r,::move(path),no_follow,true/*read*/,comment_} {
	SolveReport sr {.real=real,.kind=kind} ;
	for( auto&& [file,a] : r.real_path.exec(sr) ) r._report_dep( ::move(file) , a , comment ) ;
}

Record::Lnk::Lnk( Record& r , Path&& src_ , Path&& dst_ , bool no_follow , ::string const& comment_ ) :
	src { r , ::move(src_) , no_follow         , true /*read*/ , to_string(comment_,".src") }
,	dst { r , ::move(dst_) , true/*no_follow*/ , false/*read*/ , to_string(comment_,".dst") }
{
	if (src.real==dst.real) { dst.kind = Kind::Ext ; return ; }                // posix says it is nop in that case
	//
	Accesses a = Access::Reg ; if (no_follow) a |= Access::Lnk ;                                      // if no_follow, a sym link may be hard linked
	if (src.kind<=Kind::Dep ) r._report_dep   ( ::move  (src.real) , src.accesses|a , src.comment ) ;
	if (dst.kind==Kind::Repo) r._report_target( ::string(dst.real) ,                  dst.comment ) ;
}
int Record::Lnk::operator()( Record& r , int rc ) {
	bool ok = rc>=0 ;
	if      ( dst.kind==Kind::Repo       ) r._report_confirm( ::move(dst.real) , ok ) ;
	else if ( dst.kind==Kind::Tmp  && ok ) r._report_tmp    (                       ) ;
	return rc ;
}

Record::Mkdir::Mkdir( Record& r , Path&& path , ::string const& comment_ ) : Solve{r,::move(path),true/*no_follow*/,false/*read*/,comment_} {
	r._report_guard(::move(real),"mkdir") ;
}

static inline bool _do_stat (int flags) { return flags&O_PATH                                                        ; }
static inline bool _do_read (int flags) { return !_do_stat(flags) && (flags&O_ACCMODE)!=O_WRONLY && !(flags&O_TRUNC) ; }
static inline bool _do_write(int flags) { return !_do_stat(flags) && (flags&O_ACCMODE)!=O_RDONLY                     ; }
Record::Open::Open( Record& r , Path&& path , int flags , ::string const& comment_ ) :
	Solve{ r , ::move(path) , bool(flags&O_NOFOLLOW) , _do_read(flags) , to_string(comment_,::hex,'.',flags) }
,	do_write{_do_write(flags)}
{
	bool do_read = _do_read(flags) ;
	bool do_stat = _do_stat(flags) ;
	//
	if ( flags&(O_DIRECTORY|O_TMPFILE)          ) { kind=Kind::Ext ; return ; } // we already solved, this is enough
	if ( do_stat && s_autodep_env().ignore_stat ) { kind=Kind::Ext ; return ; }
	if ( kind>Kind::Dep                         )                    return ;
	//
	if (!do_write) {
		if      (do_read) r._report_dep   ( ::string(real) , file_date(s_root_fd(),real) , accesses|Access::Reg  , comment+".rd"   ) ;
		else if (do_stat) r._report_dep   ( ::string(real) , file_date(s_root_fd(),real) , accesses|Access::Stat , comment+".path" ) ;
	} else if (kind==Kind::Repo) {
		if      (do_read) r._report_update( ::string(real) , file_date(s_root_fd(),real) , accesses|Access::Reg  , comment+".upd"  ) ; // file date is updated if created, use original date
		else              r._report_target( ::string(real) ,                                                       comment+".wr"   ) ; // .
	} else {
		if      (do_read) r._report_dep   ( ::string(real) , file_date(s_root_fd(),real) , accesses|Access::Reg  , comment+".upd"  ) ; // in src dirs, only the read side is reported
	}
}
int Record::Open::operator()( Record& r , int rc ) {
	if (do_write) {
		bool ok = rc>=0 ;
		switch (kind) {
			case Kind::Repo :         r._report_confirm( ::move(real) , ok ) ; break ;
			case Kind::Tmp  : if (ok) r._report_tmp    (                   ) ; break ;
			default : ;
		}
	}
	return rc ;
}

Record::Read::Read( Record& r , Path&& path , bool no_follow , ::string const& comment_ ) : Solve{r,::move(path),no_follow,true/*read*/,comment_} {
	if (kind<=Kind::Dep) r._report_dep( ::move(real) , accesses|Access::Reg , comment ) ;
}

Record::ReadLnk::ReadLnk( Record& r , Path&& path , char* buf_ , size_t sz_ , ::string const& comment_ ) : Solve{r,::move(path),true/*no_follow*/,true/*read*/,comment_} , buf{buf_} , sz{sz_} {
	SWEAR(at!=Backdoor) ;
}

ssize_t Record::ReadLnk::operator()( Record& r , ssize_t len ) {
	if ( Record::s_has_tmp_view() && kind==Kind::Proc && len>0 ) {             // /proc my contain links to tmp_dir that we must show to job as pointing to tmp_view
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
	if (kind<=Kind::Dep) {
		if (len>=0) r._report_dep( ::move(real) , accesses|Access::Lnk , comment     ) ;
		else        r._report_dep( ::move(real) , accesses|Access::Lnk , comment+'~' ) ;
	}
	return len ;
}

// flags is not used if exchange is not supported
Record::Rename::Rename( Record& r , Path&& src_ , Path&& dst_ , bool exchange , ::string const& comment_ ) :
	src{ r , ::move(src_) , true/*no_follow*/ , true/*read*/ , to_string(comment_+".src") }
,	dst{ r , ::move(dst_) , true/*no_follow*/ , exchange     , to_string(comment_+".dst") }
{
	if (src.real==dst.real) return ;                                           // posix says in this case, it is nop
	if (exchange) {
		src.comment += "<>" ;
		dst.comment += "<>" ;
	}
	SWEAR(!(src.accesses&~DataAccesses)) ;                                     // ensure we need not add _report_dep for last level solve access
	SWEAR(!(dst.accesses&~DataAccesses)) ;                                     // .
	// rename has not occurred yet so for each directory :
	// - files are read and unlinked
	// - their coresponding files in the other directory are written
	// also scatter files into 3 categories :
	// - files that are unlinked         (typically all files in src  dir, but not necesssarily all of them)
	// - files that are written          (          all files in dst  dir                                  )
	// - files that are read and written (              files in both dirs in case of exchange             )
	// in case of exchange, both dirs are both src and dst
	::uset_s   froms ;
	::vector_s reads ;
	if ( src.kind<=Kind::Dep || dst.kind==Kind::Repo ) {
		::vector_s sfxs = walk(s_root_fd(),src.real) ;                                               // list only accessible files
		if (src.kind<=Kind::Dep ) for( ::string const& s : sfxs ) froms .insert   ( src.real + s ) ;
		if (dst.kind==Kind::Repo) for( ::string const& d : sfxs ) writes.push_back( dst.real + d ) ;
	}
	if ( exchange && ( dst.kind<=Kind::Dep || src.kind==Kind::Repo ) ) {
		::vector_s sfxs = walk(s_root_fd(),dst.real) ;                                               // list only accessible files
		if (dst.kind<=Kind::Dep ) for( ::string const& s : sfxs ) froms .insert   ( dst.real + s ) ;
		if (src.kind==Kind::Repo) for( ::string const& d : sfxs ) writes.push_back( src.real + d ) ;
	}
	for( ::string const& w : writes ) {
		auto it = froms.find(w) ;
		if (it==froms.end()) continue ;
			reads.push_back(w) ;
			froms.erase(it) ;
	}
	unlinks = mk_vector(froms) ;
	r._report_deps   ( ::move    (reads  ) , DataAccesses , false/*unlink*/ , src.comment ) ;
	r._report_deps   ( ::vector_s(unlinks) , DataAccesses , true /*unlink*/ , src.comment ) ;
	r._report_targets( ::vector_s(writes ) ,                                  dst.comment ) ;
	if (src.kind==Kind::Repo) r._report_guard(::move(src.real)) ;                             // only necessary in presence of renamed dirs, but this is not that frequent, perf is low priority
	if (dst.kind==Kind::Repo) r._report_guard(::move(dst.real)) ;                             // .
}
int Record::Rename::operator()( Record& r , int rc ) {
	if (+unlinks) r._report_confirm( ::move(unlinks) , rc>=0 ) ;
	if (+writes ) r._report_confirm( ::move(writes ) , rc>=0 ) ;
	return rc ;
}

Record::Search::Search( Record& r , Path const& path , bool exec , const char* path_var , ::string const& comment ) {
	SWEAR( path.at==Fd::Cwd , path.at ) ;                                                                             // it is meaningless to search with a dir fd
	const char* file = path.file ;
	if (!file) return ;
	//
	auto wrap_up = [&](const char* f) -> bool/*done*/ {
		bool done ;
		if (exec) { Exec res{r,f,false/*no_follow*/,comment} ; if ((done=is_exe   (s_root_fd(),res.real))) static_cast<Real&>(*this) = ::move(res) ; }
		else      { Read res{r,f,false/*no_follow*/,comment} ; if ((done=is_target(s_root_fd(),res.real))) static_cast<Real&>(*this) = ::move(res) ; }
		return done ;
	} ;
	//
	if (::strchr(file,'/')) { wrap_up(file) ; return ; }                       // if file contains a /, no search is performed
	//
	::string path_val = get_env(path_var) ;
	if (!path_val) {
		size_t n = ::confstr(_CS_PATH,nullptr,0) ;
		path_val.resize(n) ;
		::confstr(_CS_PATH,path_val.data(),n) ;
	}
	//
	for( size_t pos=0 ;;) {
		size_t end = path_val.find(':',pos) ;
		size_t len = end-pos                ;
		if (len>=PATH_MAX) {                    return ; }                     // burp
		if (!len         ) { if (wrap_up(file)) return ; }
		else {
			::string full_file = to_string(::string_view(path_val).substr(pos,len),'/',file) ;
			if (wrap_up(full_file.c_str())) {
				allocate(full_file) ;
				return ;
			}
		}
		if (end==Npos) return ;
		pos = end+1 ;
	}
}

Record::Stat::Stat( Record& r , Path&& path , bool no_follow , ::string const& comment_ ) : Solve{r,::move(path),no_follow,true/*read*/,comment_} {
	if (s_autodep_env().ignore_stat) kind = Kind::Ext ;                        // no report if stats are ignored
}
int Record::Stat::operator()( Record& r , int rc ) {
	if (kind<=Kind::Dep) r._report_dep( ::move(real) , Access::Stat , comment ) ;
	return rc ;
}

Record::Symlnk::Symlnk( Record& r , Path&& p , ::string const& c ) : Solve{r,::move(p),true/*no_follow*/,false/*read*/,c} {
	if (kind==Kind::Repo) r._report_target( ::string(real) , comment ) ;
}
int Record::Symlnk::operator()( Record& r , int rc ) {
	if (kind==Kind::Repo) r._report_confirm( ::move(real) , rc>=0  ) ;
	return rc ;
}

Record::Unlink::Unlink( Record& r , Path&& p , bool remove_dir , ::string const& c ) : Solve{r,::move(p),true/*no_follow*/,false/*read*/,c} {
	if (kind!=Kind::Repo)   return ;
	if (remove_dir      ) { r._report_guard ( ::move  (real) , comment ) ; kind = Kind::Ext ; } // we can move real as it will not be used in operator()
	else                    r._report_unlink( ::string(real) , comment ) ;
}
int Record::Unlink::operator()( Record& r , int rc ) {
	if (kind==Kind::Repo) r._report_confirm( ::move(real) , rc>=0 ) ;
	return rc ;
}
