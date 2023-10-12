// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <fcntl.h>
#include <linux/limits.h>
#include <netdb.h>

#include <filesystem>

#include "disk.hh"

#include "record.hh"

using namespace ::filesystem ;
using namespace Disk         ;
using namespace Time         ;

// /!\ : doing any call to libc during static initialization leads to incoherent results
// so, do  dynamic init for all static variables

//
// Record
//

bool Record::is_simple(const char* file) const {
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

void Record::_report_access( JobExecRpcReq const& jerr ) const {
	SWEAR(jerr.proc==JobExecRpcProc::Access) ;
	if (!jerr.sync) {
		bool miss = false ;
		for( auto const& [f,dd] : jerr.files ) {
			auto [it,inserted] = access_cache.emplace(f,Accesses::None) ;
			Accesses old_accesses = it->second ;
			it->second |= jerr.digest.accesses ;
			if      (!jerr.digest.idle()     ) miss = true ;                   // modifying accesses cannot be cached as we do not know what other processes may have done in between
			else if (it->second!=old_accesses) miss = true ;                   // new accesses must be recorded
		}
		if (!miss) return ;
	}
	report_cb(jerr) ;
}

Record::SolveReport Record::_solve( Path& path , bool no_follow , ::string const& comment ) {
	if (!path.file) return {} ;
	SolveReport sr = real_path.solve(path.at,path.file,no_follow) ;
	for( ::string& lnk : sr.lnks ) _report_dep( ::move(lnk) , file_date(s_root_fd(),lnk) , Access::Lnk , comment+".lnk" ) ;
	sr.lnks.clear() ;
	if (sr.mapped) {
		if      (is_abs(sr.real)) path.share   (               sr.real.empty()?"/":sr.real.c_str()             ) ;
		else if (path.has_at    ) path.share   ( s_root_fd() ,                     sr.real.c_str()             ) ;
		else                      path.allocate(               to_string(s_autodep_env().root_dir,'/',sr.real) ) ;
	}
	path.kind = sr.kind ;
	return sr ;
}

JobExecRpcReply Record::backdoor(JobExecRpcReq&& jerr) {
	if (jerr.has_files()) {
		SWEAR(jerr.auto_date) ;
		bool            some_in_tmp = false               ;
		::string        c           = jerr.comment+".lnk" ;
		::vmap_s<Ddate> files       ;
		bool            write       = jerr.digest.write   ;
		for( auto const& [f,dd] : jerr.files ) {
			Path        p  = {Fd::Cwd,f.c_str()}              ;                // we can play with the at field at will
			SolveReport sr = _solve( p , jerr.no_follow , c ) ;
			if (write?sr.kind==Kind::Repo:sr.kind<=Kind::Dep) files.emplace_back( sr.real , file_date(s_root_fd(),sr.real) ) ;
			some_in_tmp |= sr.kind==Kind::Tmp ;
		}
		jerr.files     = ::move(files) ;
		jerr.auto_date = false         ;                                       // files are now real and dated
		if ( some_in_tmp && jerr.digest.write ) _report_tmp() ;
	}
	jerr.date = Pdate::s_now() ;                                               // ensure date is posterior to links encountered while solving
	if (jerr.proc==JobExecRpcProc::Access) _report_access(jerr) ;
	else                                   report_cb     (jerr) ;
	if (jerr.sync) return get_reply_cb() ;
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

Record::Chdir::Chdir( Record& r , Path&& path ) : Solve{r,::move(path),true/*no_follow*/} {
	if (s_autodep_env().auto_mkdir && !real.empty() ) Disk::make_dir(s_root_fd(),real,false/*unlink_ok*/) ;
}
int Record::Chdir::operator()( Record& r , int rc , pid_t pid ) {
	if (rc!=0) return rc ;
	if (pid  ) r.chdir(Disk::read_lnk("/proc/"+::to_string(pid)+"/cwd").c_str()) ;
	else       r.chdir(Disk::cwd()                                     .c_str()) ;
	return rc ;
}

Record::Exec::Exec( Record& r , Path&& path , bool no_follow , ::string const& comment_ ) : Solve{r,::move(path),no_follow,comment_} {
	SolveReport sr {.real=real,.kind=kind} ;
	for( auto&& [file,a] : r.real_path.exec(sr) ) r._report_dep( ::move(file) , a , comment ) ;
}

Record::Lnk::Lnk( Record& r , Path&& src_ , Path&& dst_ , int flags , ::string const& comment_ ) :
	no_follow { bool(flags&AT_SYMLINK_FOLLOW)                                             }
,	src       { r , ::move(src_) , !no_follow        , to_string(comment_,".src",::hex,'.',flags) }
,	dst       { r , ::move(dst_) , true/*no_follow*/ , to_string(comment_,".dst",::hex,'.',flags) }
{}
int Record::Lnk::operator()( Record& r , int rc , bool no_file ) {
	if (src.real==dst.real) return rc ;                                        // this includes case where both are outside repo as they would be both empty
	bool ok = rc>=0 ;
	//
	Accesses a = Access::Reg ; if (no_follow) a |= Access::Lnk ;                                                       // if no_follow, a sym link may be hard linked
	if ( src.kind<=Kind::Dep && (ok||no_file) )                  r._report_dep( ::move(src.real) , a , src.comment ) ; // if no_follow, the symlink can be linked
	//
	if (dst.kind==Kind::Repo) { if ( ok                        ) r._report_target( ::move(dst.real) , dst.comment ) ; }
	else                      { if ( ok && dst.kind==Kind::Tmp ) r._report_tmp   (                                ) ; }
	return rc ;
}

Record::Open::Open( Record& r , Path&& path , int flags , ::string const& comment_ ) : Solve{ r , ::move(path) , bool(flags&O_NOFOLLOW) , to_string(comment_,::hex,'.',flags) } {
	bool do_stat = flags&O_PATH ;
	//
	if ( flags&(O_DIRECTORY|O_TMPFILE)          ) { kind=Kind::Ext ; return ; } // we already solved, this is enough for a directory
	if ( do_stat && s_autodep_env().ignore_stat ) { kind=Kind::Ext ; return ; }
	//
	do_read  = !do_stat && (flags&O_ACCMODE)!=O_WRONLY && !(flags&O_TRUNC) ;
	do_write = !do_stat && (flags&O_ACCMODE)!=O_RDONLY                     ;
	//
	if ( do_read && do_write && kind<=Kind::Dep ) date = file_date(s_root_fd(),real) ; // file date is updated by open if it does not exist, capture date before
}
int Record::Open::operator()( Record& r , bool has_fd , int fd_rc , bool no_file ) {
	bool     ok = fd_rc>=0                                       ;
	Accesses a  = do_read||do_write ? Access::Reg : Access::Stat ;
	//
	if (kind>Kind::Dep) {
		if ( do_write && ok && kind==Kind::Tmp ) r._report_tmp() ;
	} else if (do_write) {
		comment += do_read ? ".upd" : ".wr" ;
		if (do_read) {
			if        (ok     ) { if (kind==Kind::Repo) r._report_update( ::move(real) , date , a  ,               comment  ) ; // file date is updated if created, use original date
			/**/                  else                  r._report_dep   ( ::move(real) , date , a  ,               comment  ) ; // in src dirs, only the read side is reported
			} else if (no_file)                         r._report_dep   ( ::move(real) , DD() , a  , (comment+='!',comment) ) ;
		} else       {
			if        (ok     )   if (kind==Kind::Repo) r._report_target( ::move(real) ,                           comment  ) ;
		}
	} else {
		comment += do_read ? ".rd" : ".path" ;
		if        (ok     ) { if (has_fd) r._report_dep( ::move(real) , file_date(fd_rc) , a ,               comment  ) ;
		/**/                  else        r._report_dep( ::move(real) ,                    a , (comment+="*",comment) ) ; // if no fd available, use auto-date
		} else if (no_file)               r._report_dep( ::move(real) , DD()             , a , (comment+='!',comment) ) ;
	}
	return fd_rc ;
}

Record::Read::Read( Record& r , Path&& path , bool no_follow , ::string const& comment_ ) : Solve{r,::move(path),no_follow,comment_} {
	if (kind<=Kind::Dep) r._report_dep( ::move(real) , Access::Reg , comment ) ;
}

Record::ReadLnk::ReadLnk( Record& r , Path&& path , char* buf_ , size_t sz_ , ::string const& comment_ ) : Solve{r,::move(path),true/*no_follow*/,comment_} , buf{buf_} , sz{sz_} {
	SWEAR(at!=Backdoor) ;
}

ssize_t Record::ReadLnk::operator()( Record& r , ssize_t len ) {
	if ( r.real_path.has_tmp_view && kind==Kind::Proc && len>0 ) {             // /proc my contain links to tmp_dir that we must show to job as pointing to tmp_view
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
		if (len>=0) r._report_dep( ::move(real) , Access::Lnk , comment     ) ;
		else        r._report_dep( ::move(real) , Access::Lnk , comment+'~' ) ; // file may be regular, so let _report_dep determine the date ...
	}                                                                           // optimizing based on no_file (date is DD()) is not reliable as no_file may have false positives
	return len ;
}

// flags is not used if echange is not supported
Record::Rename::Rename( Record& r , Path&& src_ , Path&& dst_ , u_int flags [[maybe_unused]] , ::string const& comment_ ) :
	src{ r , ::move(src_) , true/*no_follow*/ , to_string(comment_+".rd",::hex,'.',flags) }
,	dst{ r , ::move(dst_) , true/*no_follow*/ , to_string(comment_+".wr",::hex,'.',flags) }
{
	#ifdef RENAME_EXCHANGE
		exchange = flags & RENAME_EXCHANGE ;
	#endif
}
int Record::Rename::operator()( Record& r , int rc , bool no_file ) {
	// XXX : protect code against access rights while walking : the rename of a dir can be ok while walking inside it could be forbidden
	if (src.real==dst.real) return rc ;                                           // this includes case where both are outside repo as they would be both empty
	if (exchange) {
		src.comment += "<>" ;
		dst.comment += "<>" ;
	}
	if (rc==0) {                                                                        // rename has occurred
		if ( dst.kind==Kind::Tmp || (exchange&&src.kind==Kind::Tmp) ) r._report_tmp() ;
		// handle directories (remember that rename has already occured when we walk)
		// so for each directoty :
		// - files are written
		// - their coresponding files in the other directory are read and unlinked
		::vector_s reads  ;
		::vector_s writes ;
		if ( src.kind<=Kind::Dep || dst.kind==Kind::Repo ) {
			::vector_s sfxs = walk(dst.at,dst.file) ;
			if (src.kind<=Kind::Dep ) for( ::string const& s : sfxs ) reads .push_back( src.real + s ) ;
			if (dst.kind==Kind::Repo) for( ::string const& d : sfxs ) writes.push_back( dst.real + d ) ;
		}
		if ( exchange && ( dst.kind<=Kind::Dep || src.kind==Kind::Repo ) ) {
			::vector_s sfxs = walk(src.at,src.file) ;
			if (dst.kind<=Kind::Dep ) for( ::string const& s : sfxs ) reads .push_back( dst.real + s ) ;
			if (src.kind==Kind::Repo) for( ::string const& d : sfxs ) writes.push_back( src.real + d ) ;
		}
		r._report_deps   ( ::move(reads ) , DataAccesses , true/*unlink*/ , src.comment ) ; // do unlink before write so write has priority
		r._report_targets( ::move(writes) ,                                 dst.comment ) ;
	} else if (no_file) {                                                         // rename has not occurred : the read part must still be reported
		// old files may exist as the errno is for both old & new, use generic report which finds the date on the file
		// if old/new are not dir, then assume they should be files as we do not have a clue of what should be inside
		if ( src.kind<=Kind::Dep             ) r._report_deps( walk( s_root_fd() , src.real ) , DataAccesses , false/*unlink*/ , src.comment ) ; // src.comment is for the read part
		if ( dst.kind<=Kind::Dep && exchange ) r._report_deps( walk( s_root_fd() , dst.real ) , DataAccesses , false/*unlink*/ , src.comment ) ; // .
	}
	return rc ;
}

Record::Search::Search( Record& r , Path const& path , bool exec , const char* path_var , ::string const& comment ) {
	SWEAR(path.at==Fd::Cwd) ;                                                                                         // it is meaningless to search with a dir fd
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
	if (path_val.empty()) {
		size_t n = confstr(_CS_PATH,nullptr,0) ;
		path_val.resize(n) ;
		confstr(_CS_PATH,path_val.data(),n) ;
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

Record::Stat::Stat( Record& r , Path&& path , bool no_follow , ::string const& comment_ ) : Solve{r,::move(path),no_follow,comment_} {
	if (s_autodep_env().ignore_stat) kind = Kind::Ext ;                        // no report if stats are ignored
}
int Record::Stat::operator()( Record& r , int rc , bool no_file ) {
	if ( kind<=Kind::Dep && (rc>=0||no_file) ) r._report_dep( ::move(real) , Access::Stat , comment ) ;
	return rc ;
}

Record::SymLnk::SymLnk( Record& r , Path&& path , ::string const& comment_ ) : Solve{r,::move(path),true/*no_follow*/,comment_} {}
int Record::SymLnk::operator()( Record& r , int rc ) {
	if ( kind==Kind::Repo && rc>=0 ) r._report_target( ::move(real) , comment ) ;
	return rc ;
}

Record::Unlink::Unlink( Record& r , Path&& path, bool remove_dir , ::string const& comment_ ) : Solve{r,::move(path),true/*no_follow*/,comment_} {
	if (remove_dir) kind = Kind::Ext ;
}
int Record::Unlink::operator()( Record& r , int rc ) {
	if ( kind==Kind::Repo && rc>=0 ) r._report_unlink( ::move(real) , comment ) ;
	return rc ;
}

//
// RecordSock
//

Fd RecordSock::_s_report_fd ;
