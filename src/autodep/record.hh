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

static constexpr Fd Backdoor = AT_FDCWD==-200 ? -300 : -200 ;                  // special value to pass commands to autodep, stay away from used values (AT_FDCWD & >=0)

struct Record {
	using Access     = Disk::Access                                      ;
	using Accesses   = Disk::Accesses                                    ;
	using DD         = Time::Ddate                                       ;
	using SolveReport= Disk::RealPath::SolveReport                       ;
	using Proc       = JobExecRpcProc                                    ;
	using GetReplyCb = ::function<JobExecRpcReply(                    )> ;
	using ReportCb   = ::function<void           (JobExecRpcReq const&)> ;
	// statics
	static Fd s_root_fd() {
		if (!_s_root_fd) {
			_s_root_fd = Disk::open_read(s_autodep_env().root_dir) ;
			SWEAR(+_s_root_fd) ;
			_s_root_fd.no_std() ;                                              // avoid poluting standard descriptors
		}
		return _s_root_fd ;
	}
	// analyze flags in such a way that it works with all possible representations of O_RDONLY, O_WRITEONLY and O_RDWR : could be e.g. 0,1,2 or 1,2,3 or 1,2,4
	static AutodepEnv const& s_autodep_env() {
		if (!_s_autodep_env) _s_autodep_env =new AutodepEnv{getenv("LMAKE_AUTODEP_ENV")} ;
		return *_s_autodep_env ;
	}
	static AutodepEnv const& s_autodep_env(AutodepEnv const& ade) {
		SWEAR(!_s_autodep_env) ;
		_s_autodep_env = new AutodepEnv{ade} ;
		return *_s_autodep_env ;
	}
	static void s_hide      ( int fd                ) { if ( _s_root_fd.fd==fd                        ) _s_root_fd.detach() ; } // guaranteed syscall free, so no need for caller to protect errno
	static void s_hide_range( int min , int max=~0u ) { if ( _s_root_fd.fd>=min && _s_root_fd.fd<=max ) _s_root_fd.detach() ; } // .

	// static data
private :
	static Fd          _s_root_fd     ;
	static AutodepEnv* _s_autodep_env ;
	// cxtors & casts
public :
	Record() = default ;
	//
	Record   ( ReportCb const& rcb , GetReplyCb const& grcb , pid_t pid=0 ) { init(rcb,grcb,pid) ; }
	void init( ReportCb const& rcb , GetReplyCb const& grcb , pid_t pid=0 ) {
		real_path.init( s_autodep_env() , pid ) ;
		report_cb    = rcb  ;
		get_reply_cb = grcb ;
	}
	// services
	bool is_simple(const char*) const ;
private :
	void _report_access( ::string&& f , DD d , Accesses a , bool update , ::string const& c={} ) const {
		_report_access( JobExecRpcReq( JobExecRpcProc::Access , {{::move(f),d}} , { .accesses=a , .write=update } , c ) ) ;
	}
	//
	void _report_access( JobExecRpcReq const& jerr ) const ;
	//
	void _report_dep   ( ::string&& f , DD dd , Accesses a , ::string const& c={} ) const { _report_access( ::move(f) , dd                             , a , false , c ) ; }
	void _report_dep   ( ::string&& f ,         Accesses a , ::string const& c={} ) const { _report_access( ::move(f) , Disk::file_date(s_root_fd(),f) , a , false , c ) ; }
	void _report_update( ::string&& f , DD dd , Accesses a , ::string const& c={} ) const { _report_access( ::move(f) , dd                             , a , true  , c ) ; }
	//
	void _report_deps( ::vmap_s<DD>&& fs , Accesses a , bool u , ::string const& c={} ) const {
		_report_access( JobExecRpcReq( JobExecRpcProc::Access , ::move(fs) , { .accesses=a , .unlink=u } , c ) ) ;
	}
	void _report_deps( ::vector_s const& fs , Accesses a , bool u , ::string const& c={} ) const {
		::vmap_s<DD> fds ;
		for( ::string const& f : fs ) fds.emplace_back( f , Disk::file_date(s_root_fd(),f) ) ;
		_report_deps( ::move(fds) , a , u , c ) ;
	}
	void _report_target ( ::string  && f  , ::string const& c={} ) const { _report_access( JobExecRpcReq( JobExecRpcProc::Access , {{::forward<string>(f),DD()}} , {.write =true} , c ) ) ; }
	void _report_unlink ( ::string  && f  , ::string const& c={} ) const { _report_access( JobExecRpcReq( JobExecRpcProc::Access , {{::forward<string>(f),DD()}} , {.unlink=true} , c ) ) ; }
	void _report_targets( ::vector_s&& fs , ::string const& c={} ) const {
		vmap_s<DD> mdd ;
		for( ::string& f : fs ) mdd.emplace_back(::move(f),DD()) ;
		_report_access( JobExecRpcReq( JobExecRpcProc::Access , ::move(mdd) , {.write =true} , c ) ) ;
	}
	void _report_tmp( bool sync=false , ::string const& comment={} ) const {
		if      (!tmp_cache) tmp_cache = true ;
		else if (!sync     ) return ;
		report_cb(JobExecRpcReq(JobExecRpcProc::Tmp,sync,comment)) ;
	}
public :
	JobExecRpcReply backdoor( JobExecRpcReq&& jerr                    ) ;
	ssize_t         backdoor( const char* msg , char* buf , size_t sz ) ;
	//
	struct Path {
		using Kind = Disk::Kind ;
		// cxtors & casts
		Path() = default ;
		Path(                  const char* p ) :                        file{p } {}
		Path(           Fd a                 ) : has_at{true} , at{a} , file{""} {}
		Path(           Fd a , const char* p ) : has_at{true} , at{a} , file{p } {}
		Path( bool ha , Fd a , const char* p ) : has_at{ha  } , at{a} , file{p } {}
		~Path() { if (allocated) delete[] file ; }
		// servicess
		void allocate(::string const& file_) {
			if (allocated) delete[] file ;
			char* data = new char[file_.size()+1] ;                            // +1 to account for terminating null
			::memcpy(data,file_.c_str(),file_.size()+1) ;
			file      = data    ;
			at        = Fd::Cwd ;
			allocated = true    ;
		}
		void share(const char* file_) {
			if (allocated) delete[] file ;
			file      = file_   ;
			at        = Fd::Cwd ;
			allocated = false   ;
		}
		void allocate( Fd at_ , ::string const& file_ ) { SWEAR(has_at) ; allocate(file_) ; at = at_ ; }
		void share   ( Fd at_ , const char*     file_ ) { SWEAR(has_at) ; share   (file_) ; at = at_ ; }
		// data
		bool        has_at    = false         ;            // if false => at is not managed and may not be substituted any non-default value
		bool        allocated = false         ;            // if true <=> file has been allocated and must be freed upon destruction
		Kind        kind      = Kind::Unknown ;            // updated when analysis is done
		Fd          at        = Fd::Cwd       ;            // at & file may be modified, but together, they always refer to the same file
		const char* file      = nullptr       ;            // .
	} ;
	struct Real : Path {
		// cxtors & casts
		Real() = default ;
		Real( Path const& p , ::string const& c={} ) : Path{p} , comment{c} {}
		// services
		template<class T> T operator()( Record& , T rc ) { return rc ; }
		// data
		::string real    = {} ;
		::string comment = {} ;
	} ;
	struct Solve : Real {
		// search (executable if asked so) file in path_var
		Solve()= default ;
		Solve( Record& r , Path const& path , bool no_follow , ::string comment_={} ) : Real{path,comment_} {
			real = r._solve( *this , no_follow , comment ).real ;
		}
	} ;
	struct Chdir : Solve {
		// cxtors & casts
		Chdir() = default ;
		Chdir( Record& , Path const& ) ;
		// services
		int operator()( Record& , int rc , pid_t pid=0 ) ;
	} ;
	struct Exec : Solve {
		// cxtors & casts
		Exec() = default ;
		Exec( Record& , Path const& , bool no_follow , ::string const& comment="exec" ) ;
	} ;
	struct Lnk {
		// cxtors & casts
		Lnk() = default ;
		Lnk( Record& , Path const& src , Path const& dst , int flags=0 , ::string const& comment="lnk" ) ;
		// services
		int operator()( Record& , int rc , bool no_file ) ;                    // no_file is only meaning full if rc is in error
		// data
		bool  no_follow = true ;
		Solve src       ;
		Solve dst       ;
	} ;
	struct Open : Solve {
		// cxtors & casts
		Open() = default ;
		Open( Record& , Path const& , int flags , ::string const& comment="open" ) ;
		// services
		int operator()( Record& , bool has_fd , int fd_rc , bool no_file=false ) ;   // no_file is only meaningful if rc is in error
		// data
		bool do_read  = false ;
		bool do_write = false ;
		DD   date     ;                // if file is updated and did not exist, its date must be capture before the actual syscall
	} ;
	struct Read : Solve {
		// cxtors & casts
		Read() = default ;
		Read( Record& , Path const& , bool no_follow , ::string const& comment="read" ) ;
	} ;
	struct ReadLnk : Solve {
		// cxtors & casts
		ReadLnk() = default ;
		// buf and sz are only used when mapping tmp or processing backdoor
		ReadLnk( Record&   , Path const&   , char* buf , size_t sz , ::string const& comment="read_lnk" ) ;
		ReadLnk( Record& r , Path const& p ,                         ::string const& comment="read_lnk" ) : ReadLnk{r,p,nullptr/*buf*/,0/*sz*/,comment} {
			SWEAR(p.at!=Backdoor) ;                                            // backdoor works in cxtor and need buf to put its result
		}
		// services
		ssize_t operator()( Record& r , ssize_t len=0 ) ;                      // len unused in case of backdoor
		// data
		char*  buf = nullptr/*garbage*/ ;
		size_t sz  = 0      /*garbage*/ ;
	} ;
	struct Rename {
		// cxtors & casts
		Rename() = default ;
		Rename( Record& , Path const& src , Path const& dst , unsigned int flags=0 , ::string const& comment="rename" ) ;
		// services
		int operator()( Record& , int rc , bool no_file ) ;                    // if file is updated and did not exist, its date must be capture before the actual syscall
		// data
		bool  exchange = false/*garbage*/ ;
		Solve src      ;
		Solve dst      ;
	} ;
	struct Search : Real {
		// search (executable if asked so) file in path_var
		Search() = default ;
		Search( Record& , Path const& , bool exec , const char* path_var , ::string const& comment="search" ) ;
	} ;
	struct Stat : Solve {
		// cxtors & casts
		Stat() = default ;
		Stat( Record& , Path const& , bool no_follow , ::string const& comment="stat" ) ;
		// services
		int operator()( Record& , int rc=0 , bool no_file=true ) ;             // by default, be pessimistic : success & if specified as error, consider we are missing the file
		template<class T> T* operator()( Record& r , T* res , bool no_file ) {
			(*this)( r , -!res , no_file ) ;                                   // map null (indicating error) to -a and non-null (indicating success) to 0
			return res ;
		}
	} ;
	struct SymLnk : Solve {
		// cxtors & casts
		SymLnk() = default ;
		SymLnk( Record& , Path const& , ::string const& comment="sym_lnk" ) ;
		// services
		int operator()( Record& , int rc ) ;
	} ;
	struct Unlink : Solve {
		// cxtors & casts
		Unlink() = default ;
		Unlink( Record& , Path const& , bool remove_dir=false , ::string const& comment="unlink" ) ;
		// services
		int operator()( Record& , int rc ) ;
	} ;
	//
	void chdir(const char* dir) { swear(Disk::is_abs(dir),"dir should be absolute : ",dir) ; real_path.cwd_ = dir ; }
	void solve( Path const& path , bool no_follow ) {
		if (path.at==Backdoor) return ;
		Path p = path ;
		_solve(p,no_follow) ;
	}
	//
protected :
	SolveReport _solve( Path& , bool no_follow , ::string const& comment={} ) ;
	//
	// data
protected :
	ReportCb                   report_cb    = nullptr ;
	GetReplyCb                 get_reply_cb = nullptr ;
	Disk::RealPath             real_path    ;
	mutable bool               tmp_cache    = false   ;    // record that tmp usage has been reported, no need to report any further
	mutable ::umap_s<Accesses> access_cache ;              // map file to read accesses
} ;

struct RecordSock : Record {
	// statics
	static Fd s_get_report_fd() {
		if (!_s_report_fd) {
			// establish connection with server
			::string const& service = s_autodep_env().service ;
			if (service.back()==':') _s_report_fd = Disk::open_write(service,true/*append*/) ;
			else                     _s_report_fd = ClientSockFd    (service               ) ;
			_s_report_fd.no_std() ;                                                            // avoid poluting standard descriptors
			swear_prod(+_s_report_fd,"cannot connect to job_exec through ",service) ;
		}
		return _s_report_fd ;
	}
	// these 2 functions are guaranteed syscall free, so there is no need for caller to protect errno
	static void s_hide      ( int fd                ) { Record::s_hide      (fd     ) ; if ( _s_report_fd.fd==fd                          ) _s_report_fd.detach() ; }
	static void s_hide_range( int min , int max=~0u ) { Record::s_hide_range(min,max) ; if ( _s_report_fd.fd>=min && _s_report_fd.fd<=max ) _s_report_fd.detach() ; }
private :
	static void            _s_report   ( JobExecRpcReq const& jerr ) { OMsgBuf().send(s_get_report_fd(),jerr) ;                       }
	static JobExecRpcReply _s_get_reply(                           ) { return IMsgBuf().receive<JobExecRpcReply>(s_get_report_fd()) ; }
	// static data
	static Fd _s_report_fd ;
	// cxtors & casts
public :
	RecordSock( pid_t p=0 ) : Record{ _s_report , _s_get_reply , p } {}
} ;
