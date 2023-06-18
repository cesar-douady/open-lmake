// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <cstring> // strdup, strlen

#include <filesystem>

#include "disk.hh"
#include "gather_deps.hh"
#include "rpc_job.hh"
#include "time.hh"

static constexpr int AT_BACKDOOR = Fd::Cwd.fd==-200 ? -300 : -200 ;            // special value to pass commands to autodep, stay away from used values (AT_FDCWD & >=0)

struct Record {
	using Proc       = JobExecRpcProc                                    ;
	using DD         = Time::DiskDate                                    ;
	using DFs        = DFlags                                            ;
	using ReportCb   = ::function<void           (JobExecRpcReq const&)> ;
	using GetReplyCb = ::function<JobExecRpcReply(                    )> ;
	// init
	static void s_init( AutodepEnv const& ade ) {
		s_auto_mkdir  = ade.auto_mkdir  ;
		s_ignore_stat = ade.ignore_stat ;
		s_lnk_support = ade.lnk_support ;
	}
	// statics
	static Fd s_get_root_fd() {
		if (!s_root_fd) {
			s_root_fd = Disk::open_read(*g_root_dir) ;
			s_root_fd.no_std() ;                                               // avoid poluting standard descriptors
		}
		return s_root_fd ;
	}
	// cxtors & casts
	Record() = default ;
	//
	Record   ( ReportCb const& rcb , GetReplyCb const& grcb , pid_t pid=0 ) { init(rcb,grcb,pid) ; }
	void init( ReportCb const& rcb , GetReplyCb const& grcb , pid_t pid=0 ) {
		real_path.init(s_lnk_support,pid) ;
		report_cb    = rcb  ;
		get_reply_cb = grcb ;
	}
	// statics
	// analyze flags in such a way that it works with all possible representations of O_RDONLY, O_WRITEONLY and O_RDWR : could be e.g. 0,1,2 or 1,2,3 or 1,2,4
	static bool s_do_read (int flags ) { return ( flags&(O_RDONLY|O_WRONLY|O_RDWR) ) != O_WRONLY && !(flags&O_TRUNC) ; }
	static bool s_do_write(int flags ) { return ( flags&(O_RDONLY|O_WRONLY|O_RDWR) ) != O_RDONLY                     ; }
	static bool s_no_file (int errno_) { return errno_==EISDIR || errno_==ENOENT || errno_==ENOTDIR                  ; }
	// static data
	static bool       s_auto_mkdir  ;
	static bool       s_ignore_stat ;
	static LnkSupport s_lnk_support ;
	static Fd         s_root_fd     ;
	// services
private :
	void _report( JobExecRpcReq const& jerr ) const ;
	void _report( Proc proc , bool sync=false , ::string const& comment={} ) const {
		if (proc==JobExecRpcProc::Tmp) {
			if      (!tmp_cache) tmp_cache = true ;
			else if (!sync     ) return ;
		}
		_report(JobExecRpcReq(proc,sync,comment)) ;
	}
	void _report_dep ( Proc proc , ::string&& f , DD d , DFs dfs , ::string const& c={} ) const {
		swear(!f.empty(),f) ;
		_report( JobExecRpcReq( proc , ::forward<string>(f) , d , HiddenDFlags|dfs , c ) ) ;
	}
	void _report_deps( Proc proc , ::vmap_s<DD>&& fs , DFs dfs , ::string const& c={} ) const {
		_report( JobExecRpcReq( proc , ::forward<vmap_s<DD>>(fs) , HiddenDFlags|dfs , c ) ) ;
	}
	void _report_dep ( Proc proc , ::string&& f , DFs dfs , ::string const& c={} ) const {
		_report_dep( proc , ::forward<string>(f), Disk::file_date(s_get_root_fd(),f) , dfs , c ) ;
	}
	void _report_deps( Proc proc , ::vector_s const& fs , DFs dfs , ::string const& c={} ) const {
		::vmap_s<DD> fds ;
		for( ::string const& f : fs ) fds.emplace_back( f , Disk::file_date(s_get_root_fd(),f) ) ;
		_report_deps( proc , ::move(fds) , dfs , c ) ;
	}
	//
	void _report_target ( Proc proc , ::string   const& f  , ::string const& c={} ) const { _report( JobExecRpcReq(proc,       f ,TFlags(),TFlags(),c) ) ; }
	void _report_target ( Proc proc , ::string       && f  , ::string const& c={} ) const { _report( JobExecRpcReq(proc,::move(f),TFlags(),TFlags(),c) ) ; }
	void _report_targets( Proc proc , ::vector_s const& fs , ::string const& c={} ) const { _report( JobExecRpcReq(proc,       fs,TFlags(),TFlags(),c) ) ; }
	//
	//
	::pair_s<bool/*in_tmp*/> _solve( int at , const char* file  , bool no_follow , ::string const& comment={} ) ;
	//
public :
	struct Chdir {
		Chdir() = default ;
		Chdir( bool /*active*/ , Record& , int /*at*/                   ) {}
		Chdir( bool   active   , Record& , int   at   , const char* dir ) ;
		int operator()( Record& , int rc , int pid=0 ) ;
	} ;
	struct Lnk {
		Lnk() = default ;
		Lnk( bool active , Record& , int oat , const char* ofile , int nat , const char* nfile , int flags=0 , ::string const& comment="lnk" ) ;
		int operator()( Record& , int rc , int errno_ ) ;
		::string old_real  ;
		::string new_real  ;
		bool     in_tmp    = false ;
		bool     no_follow = true  ;
		::string comment   ;
	} ;
	struct Open {
		Open() = default ;
		Open( bool active , Record& , int at , const char* file , int flags , ::string const& comment="open" ) ;
		int operator()( Record& , bool has_fd , int fd_rc , int errno_ ) ;
		::string real     ;
		bool     in_tmp   = false ;
		bool     do_read  = false ;
		bool     do_write = false ;
		DD       date     ;                                                    // if file is updated and did not exist, its date must be capture before the actual syscall
		::string comment  ;
	} ;
	struct ReadLnk {
		ReadLnk() = default ;
		ReadLnk( bool active , Record& , int at , const char* file                         , ::string const& comment="read_lnk" ) ; // for regular
		ReadLnk( bool active , Record& ,          const char* file , char* buf , size_t sz , ::string const& comment="backdoor" ) ; // for backdoor
		ssize_t operator()( Record& r , ssize_t len , int errno_ ) ;
		::string real    ;
		::string comment ;
	} ;
	struct Rename {
		Rename() = default ;
		Rename( bool active , Record& , int oat , const char* ofile , int nat , const char* nfile , unsigned int flags=0 , ::string const& comment="rename" ) ;
		int operator()( Record& , int rc , int errno_ ) ;
		int      old_at   ;
		int      new_at   ;
		::string old_file ;
		::string new_file ;
		::string old_real ;
		::string new_real ;
		bool     in_tmp   = false ;
		bool     exchange = false ;
		::string comment  ;
	} ;
	struct SymLnk {
		SymLnk() = default ;
		SymLnk( bool active , Record& , int at , const char* file , ::string const& comment="sym_lnk" ) ;
		int operator()( Record& , int rc ) ;
		::string real    ;
		::string comment ;
	} ;
	struct Unlink {
		Unlink() = default ;
		Unlink( bool active , Record& , int at , const char* file , bool remove_dir=false , ::string const& comment="unlink" ) ;
		int operator()( Record& , int rc ) ;
		::string real    ;
		::string comment ;
	} ;
	//
	void chdir(          const char* dir                                                          ) { swear(dir[0]=='/',dir) ; real_path.cwd_ = dir ; }
	void read ( int at , const char* file , bool no_follow=false , ::string const& comment="read" ) ;
	void exec ( int at , const char* file , bool no_follow=false , ::string const& comment="exec" ) ;
	void solve( int at , const char* file , bool no_follow=false , ::string const& comment={} ) {
		::string real = _solve(at,file,no_follow,comment).first ;
		if ( !s_ignore_stat && !real.empty() ) _report_dep( Proc::Deps , ::move(real) , DFlag::Stat , comment ) ;
	}
	JobExecRpcReply backdoor(JobExecRpcReq&& jerr) ;
	//
	// data
protected :
	ReportCb                                      report_cb    = nullptr ;
	GetReplyCb                                    get_reply_cb = nullptr ;
	Disk::RealPath                                real_path    ;
	mutable bool                                  tmp_cache    = false   ;     // record that tmp usage has been reported, no need to report any further
	mutable ::umap_s<pair<DFlags,JobExecRpcProc>> access_cache ;               // map file to access summary
} ;

struct RecordSock : Record {
	static void            _s_report   ( JobExecRpcReq const& jerr ) { OMsgBuf().send(t_get_report_fd(),jerr) ;                       }
	static JobExecRpcReply _s_get_reply(                           ) { return IMsgBuf().receive<JobExecRpcReply>(t_get_report_fd()) ; }
	static int t_get_report_fd() {
		if (!_t_report_fd) {
			// establish connection with server
			if (_s_service_is_file) _t_report_fd = Disk::open_write( *_s_service , true/*append*/ ) ;
			else                    _t_report_fd = ClientSockFd    ( *_s_service                  ) ;
			_t_report_fd.no_std() ;                                                                   // avoid poluting standard descriptors
			swear_prod(+_t_report_fd,"cannot connect to job_exec through ",*_s_service) ;
		}
		return _t_report_fd ;
	}
	static void s_init(AutodepEnv const& ade) {
		lib_init(ade.root_dir) ;
		_s_service = new ::string{ade.service} ;
		if (_s_service->back()==':') {
			*_s_service        = to_string(*g_root_dir,'/',_s_service->substr(0,_s_service->size()-1)) ; // for debugging purpose, log to a file
			_s_service_is_file = true                                                                  ;
		}
		Record::s_init(ade) ;
	}
	static void s_init() {
		if (!has_env("LMAKE_AUTODEP_ENV")) throw "dont know where to report deps"s ;
		s_init(get_env("LMAKE_AUTODEP_ENV")) ;
	}
	// static data
	static thread_local Fd _t_report_fd       ;
	static ::string        *_s_service        ;            // pointer to avoid init/fin order hazard
	static bool            _s_service_is_file ;
	// cxtors & casts
	RecordSock(pid_t p=0) : Record{ _s_report , _s_get_reply , p } {}
} ;
