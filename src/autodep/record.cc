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

bool       Record::s_auto_mkdir  = false               ;
bool       Record::s_ignore_stat = false               ;
LnkSupport Record::s_lnk_support = LnkSupport::Unknown ;
Fd         Record::s_root_fd     ;

void Record::_report( JobExecRpcReq const& jerr ) const {
	if ( jerr.proc==JobExecRpcProc::Access && !jerr.sync ) {
		bool miss = false ;
		for( auto const& [f,dd] : jerr.files ) {
			auto it = access_cache.find(f) ;
			if (it==access_cache.end()) {
				miss            = true      ;
				access_cache[f] = jerr.info ;                                  // create entry
			} else {
				JobExecRpcReq::AccessInfo& entry     = it->second        ;
				JobExecRpcReq::AccessInfo  new_entry = entry | jerr.info ;
				if (new_entry!=entry) {
					miss = true       ;
					entry = new_entry ;                                        // update entry
				}
			}
		}
		if (!miss) return ;
	}
	report_cb(jerr) ;
}

::pair_s<bool/*in_tmp*/> Record::_solve( int at , const char* file , bool no_follow , ::string const& comment ) {
	if (!file) return {{},false/*in_tmp*/} ;
	RealPath::SolveReport rp = real_path.solve(at,file,no_follow) ;
	for( ::string& real : rp.lnks ) _report_dep( ::move(real) , file_date(s_get_root_fd(),real) , DFlag::Lnk , comment+".lnk"  ) ;
	return {rp.real,rp.in_tmp} ;
}

void Record::read( int at , const char* file , bool no_follow , ::string const& comment ) {
	::string real = _solve(at,file,no_follow,comment).first ;
	if (!real.empty()) _report_dep( ::move(real) , DFlag::Reg , comment ) ;
}

void Record::exec( int at , const char* exe , bool no_follow , ::string const& comment ) {
	if (!exe) return ;
	for( auto&& [file,a] : real_path.exec(at,exe,no_follow) ) {
		DFlags fs ;
		if (a.as_lnk) fs |= DFlag::Lnk ;
		if (a.as_reg) fs |= DFlag::Reg ;
		_report_dep( ::move(file) , fs , comment ) ;
	}
}

JobExecRpcReply Record::backdoor(JobExecRpcReq&& jerr) {
	if (jerr.has_files()) {
		SWEAR(jerr.auto_date) ;
		bool     some_in_tmp = false               ;
		::string c           = jerr.comment+".lnk" ;
		for( auto& [f,dd] : jerr.files ) {
			bool in_tmp ;
			::tie(f,in_tmp)  = _solve(AT_FDCWD,f.c_str(),false/*no_follow*/,c) ;
			dd               = file_date(s_get_root_fd(),f)                    ;
			some_in_tmp     |= in_tmp                                          ;
		}
		jerr.auto_date = false ;                                             // files are now physical and dated
		if ( some_in_tmp && jerr.info.write ) _report(JobExecRpcProc::Tmp) ;
	}
	jerr.comment += ".backdoor" ;
	_report(jerr) ;
	if (jerr.sync) return get_reply_cb() ;
	else           return {}             ;
}

Record::Chdir::Chdir( bool active , Record& r , int at , const char* dir ) {
	if (!active             ) return ;
	if (!dir                ) return ;
	if (Record::s_auto_mkdir) Disk::make_dir(at,dir,false/*unlink_ok*/) ;
	r._solve( at , dir , true/*no_follow*/ , "chdir" ) ;
}
int Record::Chdir::operator()( Record& r , int rc , int pid ) {
	if (rc!=0) return rc ;
	if (pid  ) r.chdir(Disk::read_lnk("/proc/"+::to_string(pid)+"/cwd").c_str()) ;
	else       r.chdir(Disk::cwd()                                     .c_str()) ;
	return rc ;
}

Record::Lnk::Lnk( bool active , Record& r , int old_at , const char* old_file , int new_at , const char* new_file , int flags , ::string const& c ) : comment{c} {
	if (!active) return ;
	no_follow = !(flags&AT_SYMLINK_FOLLOW) ;
	//
	bool _ ;
	::tie(old_real,_     ) = r._solve( old_at,old_file , no_follow , c+".src" ) ;
	::tie(new_real,in_tmp) = r._solve( new_at,new_file , true      , c+".dst" ) ;
	comment += to_string(::hex,'.',flags) ;
}
int Record::Lnk::operator()( Record& r , int rc , int errno_ ) {
	if (old_real==new_real) return rc ;                                        // this includes case where both are outside repo as they would be both empty
	bool ok = rc>=0 ;
	//
	DFs dfs = DFlag::Reg ; if (no_follow) dfs |= DFlag::Lnk ;                                                      // if no_follow, the sym link may be hard linked
	if ( !old_real.empty() && (ok||s_no_file(errno_)) ) r._report_dep( ::move(old_real) , dfs , comment+".src" ) ; // if no_follow, the symlink can be linked
	//
	if (new_real.empty()) { if ( ok && in_tmp ) r._report       ( Proc::Tmp                         ) ; }
	else                  { if ( ok           ) r._report_target( ::move(new_real) , comment+".dst" ) ; }
	return rc ;
}

Record::Open::Open( bool active , Record& r , int at , const char* file , int flags , ::string const& c ) : comment{c} {
	if (!active) return ;
	bool no_follow = flags &  O_NOFOLLOW          ;
	bool anon      = flags &  O_TMPFILE           ;
	bool no_access = flags & (O_PATH|O_DIRECTORY) ;
	//
	if (anon     ) {                                        return ; }
	if (no_access) { r.solve( at , file , no_follow , c ) ; return ; }        // this actually behaves as a stat
	//
	/**/                                        ::tie(real,in_tmp)  = r._solve( at , file , no_follow , c ) ;
	/**/                                        do_read             = s_do_read (flags)                      ;
	/**/                                        do_write            = s_do_write(flags)                      ;
	if ( do_read && do_write && !real.empty() ) date                = file_date(s_get_root_fd(),real)        ; // file date is updated by open if it does not exist, capture date before
	/**/                                        comment            += to_string(::hex,'.',flags)             ;
}
int Record::Open::operator()( Record& r , bool has_fd , int fd_rc , int errno_ ) {
	bool ok = fd_rc>=0 ;
	//
	do_write &= ok ;
	if (real.empty()) {
		if ( do_write && in_tmp ) r._report(Proc::Tmp) ;
		return fd_rc ;
	}
	if (do_read) {
		if ( ok || s_no_file(errno_) ) {
			if      (do_write) r._report_dep_update( ::move(real) , date             , DFlag::Reg , comment+(ok?".upd":".rd"   ) ) ; // file date is updated if created, use original date
			else if (!ok     ) r._report_dep       ( ::move(real) , DD()             , DFlag::Reg , comment+(          ".rd!"  ) ) ;
			else if (has_fd  ) r._report_dep       ( ::move(real) , file_date(fd_rc) , DFlag::Reg , comment+(          ".rd"   ) ) ; // else it is not, even if writing
			else               r._report_dep       ( ::move(real) ,                    DFlag::Reg , comment+(          ".rd_ad") ) ; // if no fd available, use auto-date
		}
	} else if ( do_write && ok ) {
		r._report_target( ::move(real) , comment+".wr" ) ;
	}
	return fd_rc ;
}

Record::ReadLnk::ReadLnk( bool active , Record& r , int at , const char* file , ::string const& c ) : comment{c} {
	if (!active) return ;
	bool _ ;
	::tie(real,_) = r._solve( at , file , true/*no_follow*/ , comment ) ;
}

Record::ReadLnk::ReadLnk( bool active , Record& r , const char* file , char* buf , size_t sz , ::string const& c ) : comment{c} {
	SWEAR(active) ;
	JobExecRpcReq   jerr  = IMsgBuf::s_receive<JobExecRpcReq>(file) ;
	bool            sync  = jerr.sync                               ;
	JobExecRpcReply reply = r.backdoor(::move(jerr))                ;
	if (!sync) return ;
	SWEAR(sz>sizeof(MsgBuf::Len)) ;                                            // we cant do much if we dont even have the necessary size to report the needed size
	::string reply_str = OMsgBuf::s_send(reply) ;
	if (sz>=reply_str.size()) ::memcpy( buf , reply_str.data() , reply_str.size()    ) ;
	else                      ::memcpy( buf , reply_str.data() , sizeof(MsgBuf::Len) ) ; // if not enough room for data, just report the size we needed
}

ssize_t Record::ReadLnk::operator()( Record& r , ssize_t len , int errno_ ) {
	if (real.empty()) return len ;
	//
	if      (len>=0           ) r._report_dep( ::move(real)        , DFlag::Lnk , comment     ) ;
	else if (s_no_file(errno_)) r._report_dep( ::move(real) , DD() , DFlag::Lnk , comment+'!' ) ;
	else if (errno_==EINVAL   ) r._report_dep( ::move(real)        , DFlag::Lnk , comment+'~' ) ; // file is not a symlink
	//
	return len ;
}

// flags is not used if echange is not supported
Record::Rename::Rename( bool active , Record& r , int old_at_ , const char* old_file_ , int new_at_ , const char* new_file_ , u_int flags [[maybe_unused]] , ::string const& c ) : comment{c} {
	if ( !active                  ) return ;
	if ( !old_file_ || !new_file_ ) return ;
	bool old_in_tmp ;
	bool new_in_tmp ;
	#ifdef RENAME_EXCHANGE
		exchange = flags & RENAME_EXCHANGE ;
	#endif
	//
	old_at                      = old_at_                                                            ;
	new_at                      = new_at_                                                            ;
	old_file                    = old_file_                                                          ;
	new_file                    = new_file_                                                          ;
	::tie(old_real,old_in_tmp)  = r._solve( old_at,old_file.c_str() , true/*no_follow*/ , c+".old" ) ;
	::tie(new_real,new_in_tmp)  = r._solve( new_at,new_file.c_str() , true/*no_follow*/ , c+".new" ) ;
	in_tmp                      = old_in_tmp || new_in_tmp                                           ;
	comment                    += to_string(::hex,'.',flags)                                         ;
}
int Record::Rename::operator()( Record& r , int rc , int errno_ ) {
	constexpr DFlags DFlagsData = DFlag::Lnk|DFlag::Reg ;                      // rename may move regular files or links
	if (old_real==new_real) return rc ;                                        // this includes case where both are outside repo as they would be both empty
	if (rc==0) {                                                               // rename has occurred
		if (in_tmp) r._report(Proc::Tmp) ;
		// handle directories (remember that rename has already occured when we walk)
		// so for each directoty :
		// - files are written
		// - their coresponding files in the other directory are read and unlinked
		::vector_s reads  ;
		::vector_s writes ;
		{	::vector_s sfxs = walk(new_at,new_file) ;
			if ( !old_real.empty() ) for( ::string const& s : sfxs ) reads .push_back( old_real + s ) ;
			if ( !new_real.empty() ) for( ::string const& d : sfxs ) writes.push_back( new_real + d ) ;
		}
		if (exchange) {
			::vector_s sfxs = walk(old_at,old_file) ;
			if ( !new_real.empty() ) for( ::string const& s : sfxs ) reads .push_back( new_real + s ) ;
			if ( !old_real.empty() ) for( ::string const& d : sfxs ) writes.push_back( old_real + d ) ;
		}
		::string c = comment+(exchange?"<>":"") ;
		r._report_deps   ( ::move(reads ) , DFlagsData , true/*unlink*/ , c ) ; // do unlink before write so write has priority
		r._report_targets( ::move(writes) ,                               c ) ;
	} else if (s_no_file(errno_)) {                                             // rename has not occurred : the read part must still be reported
		// old files may exist as the errno is for both old & new, use generic report which finds the date on the file
		// if old/new are not dir, then assume they should be files as we do not have a clue of what should be inside
		::string c = comment+(exchange?"!<>":"!") ;
		if ( !old_real.empty()             ) r._report_deps( walk( old_at,old_file , old_real ) , DFlagsData , false/*unlink*/ , c ) ;
		if ( !new_real.empty() && exchange ) r._report_deps( walk( new_at,new_file , new_real ) , DFlagsData , false/*unlink*/ , c ) ;
	}
	return rc ;
}

Record::SymLnk::SymLnk( bool active , Record& r , int at , const char* file , ::string const& c ) : comment{c} {
	if (!active) return ;
	bool _ ;
	::tie(real,_) = r._solve( at , file , true/*no_follow*/ , c ) ;
}
int Record::SymLnk::operator()( Record& r , int rc ) {
	if ( !real.empty() && rc>=0 ) r._report_target( ::move(real) , comment ) ;
	return rc ;
}

Record::Unlink::Unlink( bool active , Record& r , int at , const char* file , bool remove_dir , ::string const& c ) : comment{c} {
	if (!active) return ;
	bool _ ;
	if (remove_dir)                 r._solve( at , file , true/*no_follow*/ , c+".dir" ) ; // if removing a dir, prevent unlink record generation
	else            ::tie(real,_) = r._solve( at , file , true/*no_follow*/ , c        ) ;
}
int Record::Unlink::operator()( Record& r , int rc ) {
	if ( !real.empty() && rc>=0 ) r._report_unlink( ::move(real) , comment ) ;
	return rc ;
}

//
// RecordSock
//

thread_local Fd RecordSock::_t_report_fd       ;
::string*       RecordSock::_s_service         = nullptr ;
bool            RecordSock::_s_service_is_file = false   ;
