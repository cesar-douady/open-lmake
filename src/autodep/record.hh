// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "disk.hh"
#include "gather_deps.hh"
#include "rpc_job.hh"
#include "time.hh"

struct Record {
	using Crc         = Hash::Crc                                         ;
	using Ddate       = Time::Ddate                                       ;
	using SolveReport = Disk::RealPath::SolveReport                       ;
	using Proc        = JobExecRpcProc                                    ;
	using GetReplyCb  = ::function<JobExecRpcReply(                    )> ;
	using ReportCb    = ::function<void           (JobExecRpcReq const&)> ;
	//
	struct Lock {
		// static data
	private :
		static ::mutex _s_mutex ;
		// cxtors & casts
	public :
		Lock (                                ) = default ;
		Lock (::vmap_s<Accesses>* deps=nullptr) : lock{_s_mutex} , sav{_s_autodep_env->active,true} {
			SWEAR( !s_deps && !s_deps_err ) ;
			s_deps     = deps ;
			s_deps_err = &err ;
		}
		~Lock() {
			s_deps     = nullptr ;
			s_deps_err = nullptr ;
		}
		// data
		::unique_lock<::mutex> lock ;
		Save<bool>             sav  ;
		::string               err  ;
	} ;
	// statics
	static bool s_active           (           ) { return _s_autodep_env && _s_autodep_env->active ;          }
	static bool s_is_simple        (const char*) ;
	static bool s_has_tmp_view     (           ) { SWEAR(_s_autodep_env) ; return +_s_autodep_env->tmp_view ; }
	//
	static Fd s_root_fd() {
		SWEAR(_s_autodep_env) ;
		if (!_s_root_fd) {
			_s_root_fd = Disk::open_read(_s_autodep_env->root_dir) ; _s_root_fd.no_std() ;                                  // avoid poluting standard descriptors
			SWEAR(+_s_root_fd) ;
		}
		return _s_root_fd ;
	}
	static AutodepEnv const& s_autodep_env() {
		SWEAR(_s_autodep_env) ;
		return *_s_autodep_env ;
	}
	static AutodepEnv const& s_autodep_env(NewType) {
		if (!_s_autodep_env) _s_autodep_env = new AutodepEnv{New} ;
		return *_s_autodep_env ;
	}
	static AutodepEnv const& s_autodep_env( AutodepEnv const& ade ) {
		SWEAR(!_s_autodep_env) ;
		_s_autodep_env = new AutodepEnv{ade} ;
		return *_s_autodep_env ;
	}
	// static data
	static bool                s_static_report ;                                                                            // if true <=> report deps to s_deps instead of through report_fd() socket
	static ::vmap_s<Accesses>* s_deps          ;
	static ::string          * s_deps_err      ;
private :
	static AutodepEnv* _s_autodep_env ;
	static Fd          _s_root_fd     ;                                                                                     // a file descriptor to repo root directory
	// cxtors & casts
public :
	Record(                       ) = default ;
	Record( NewType , pid_t pid=0 ) : _real_path{s_autodep_env(New),pid} {}
	// accesses
	// services
	Fd report_fd() const {
		if (!_report_fd) {
			// establish connection with server
			::string const& service = _s_autodep_env->service ;
			if (service.back()==':') _report_fd = Disk::open_write( service.substr(0,service.size()-1) , true/*append*/ ) ;
			else                     _report_fd = ClientSockFd(service)                                                   ;
			_report_fd.no_std() ;                                                                                           // avoid poluting standard descriptors
			swear_prod(+_report_fd,"cannot connect to job_exec through ",service) ;
		}
		return _report_fd ;
	}
	void hide(int fd) const {                                                                                               // guaranteed syscall free, so no need for caller to protect errno
		if (_s_root_fd.fd==fd) _s_root_fd.detach() ;
		if (_report_fd.fd==fd) _report_fd.detach() ;
	}
	void hide_range( int min , int max=~0u ) const {                                                                        // guaranteed syscall free, so no need for caller to protect errno
		if ( _s_root_fd  .fd>=min && _s_root_fd  .fd<=max ) _s_root_fd  .detach() ;
		if ( _report_fd.fd>=min && _report_fd.fd<=max ) _report_fd.detach() ;
	}
private :
	void            _static_report(JobExecRpcReq&& jerr) const ;
	void            _report       (JobExecRpcReq&& jerr) const { if (s_static_report) _static_report(::move(jerr)) ; else        OMsgBuf().send                    (report_fd(),jerr) ; }
	JobExecRpcReply _get_reply    (                    ) const { if (s_static_report) return {}                    ; else return IMsgBuf().receive<JobExecRpcReply>(report_fd()     ) ; }
	//
	void _report_access( JobExecRpcReq&& jerr                                                             ) const ;
	void _report_access( ::string&& f , Ddate d , Accesses a , bool write , bool unlnk , ::string&& c={} ) const {
		AccessDigest ad { a } ;
		ad.write = write ;
		ad.unlnk = unlnk ;
		_report_access(JobExecRpcReq( JobExecRpcProc::Access , {{::move(f),d}} , ad , ::move(c) )) ;
	}
	void _report_guard( ::string&& f , ::string&& c={} ) const {
		_report(JobExecRpcReq( JobExecRpcProc::Guard , {::move(f)} , ::move(c) )) ;
	}
	// for modifying accesses (_report_update, _report_target, _report_unlnk, _report_targets) :
	// - if we report after  the access, it may be that job is interrupted inbetween and repo is modified without server being notified and we have a manual case
	// - if we report before the access, we may notify an access that will not occur if job is interrupted or if access is finally an error
	// so the choice is to manage Maybe :
	// - access is reported as Maybe before the access
	// - it is then confirmed (with an ok arg to manage errors) after the access
	// in job_exec, if an access is left Maybe, i.e. if job is interrupted between the Maybe reporting and the actual access, disk is interrogated to see if access did occur
	//
	//                                                                                                                         write   unlnk
	void _report_update( ::string&& f , Ddate dd , Accesses a , ::string&& c={} ) const { _report_access( ::move(f) , dd , a , true  , false , ::move(c) ) ; }
	void _report_dep   ( ::string&& f , Ddate dd , Accesses a , ::string&& c={} ) const { _report_access( ::move(f) , dd , a , false , false , ::move(c) ) ; }
	// for user code, do not seggregate between access kind as long as we cannot guarantee errno has not been looked at (keep Access for future evolution)
	void _report_update( ::string&& f , Accesses a , ::string&& c={} ) const { _report_update( ::move(f) , Disk::file_date(s_root_fd(),f) , StrictUserAccesses?Accesses::All:a , ::move(c) ) ; }
	void _report_dep   ( ::string&& f , Accesses a , ::string&& c={} ) const { _report_dep   ( ::move(f) , Disk::file_date(s_root_fd(),f) , StrictUserAccesses?Accesses::All:a , ::move(c) ) ; }
	//
	void _report_confirm( bool unlnk , bool ok ) const { _report(JobExecRpcReq( JobExecRpcProc::Confirm , unlnk , ok )) ; }
	//
	void _report_deps( ::vmap_s<Ddate>&& fs , Accesses a , bool u , ::string&& c={} ) const {
		AccessDigest ad { a } ;
		ad.unlnk = u ;
		_report_access(JobExecRpcReq( JobExecRpcProc::Access , ::move(fs) , ad , ::move(c) )) ;
	}
	void _report_deps( ::vector_s const& fs , Accesses a , bool u , ::string&& c={} ) const {
		::vmap_s<Ddate> fds ;
		for( ::string const& f : fs ) fds.emplace_back( f , Disk::file_date(s_root_fd(),f) ) ;
		_report_deps( ::move(fds) , a , u , ::move(c) ) ;
	}
	//                                                                                           Ddate Accesses  write   unlnk
	void _report_target ( ::string  && f  , ::string&& c={} ) const { _report_access( ::move(f) , {}  , {}     , true  , false , ::move(c) ) ; }
	void _report_unlnk  ( ::string  && f  , ::string&& c={} ) const { _report_access( ::move(f) , {}  , {}     , false , true  , ::move(c) ) ; }
	void _report_targets( ::vector_s&& fs , ::string&& c={} ) const {
		AccessDigest  ad  ;
		vmap_s<Ddate> mdd ;
		ad.write = true ;
		for( ::string& f : fs ) mdd.emplace_back(::move(f),Ddate()) ;
		_report_access(JobExecRpcReq( JobExecRpcProc::Access , ::move(mdd) , ad , ::move(c) )) ;
	}
	void _report_tmp( bool sync=false , ::string&& c={} ) const {
		if      (!_tmp_cache) _tmp_cache = true ;
		else if (!sync     ) return ;
		_report(JobExecRpcReq(JobExecRpcProc::Tmp,sync,::move(c))) ;
	}
public :
	template<class... A> [[noreturn]] void report_panic(A const&... args) { _report( JobExecRpcReq(JobExecRpcProc::Panic,to_string(args...)) ) ; exit(2) ; } // continuing is meaningless
	template<class... A>              void report_trace(A const&... args) { _report( JobExecRpcReq(JobExecRpcProc::Trace,to_string(args...)) ) ;           }
	JobExecRpcReply direct( JobExecRpcReq&& jerr) ;
	//
	struct Path {
		friend ::ostream& operator<<( ::ostream& , Path const& ) ;
		// cxtors & casts
		Path(                                             )                                          {                                  }
		Path( Fd a                                        ) : has_at{true} , at{a}                   {                                  }
		Path(        const char*     f , bool steal=true  ) :                        file{f        } { if (!steal) allocate(        ) ; }
		Path( Fd a , const char*     f , bool steal=true  ) : has_at{true} , at{a} , file{f        } { if (!steal) allocate(        ) ; }
		Path(        ::string const& f , bool steal=false ) :                        file{f.c_str()} { if (!steal) allocate(f.size()) ; }
		Path( Fd a , ::string const& f , bool steal=false ) : has_at{true} , at{a} , file{f.c_str()} { if (!steal) allocate(f.size()) ; }
		//
		Path(Path && p) { *this = ::move(p) ; }
		Path& operator=(Path&& p) {
			deallocate() ;
			has_at      = p.has_at    ;
			file_loc    = p.file_loc  ;
			at          = p.at        ;
			file        = p.file      ;
			allocated   = p.allocated ;
			p.allocated = false       ;                                                   // we have clobbered allocation, so it is no more p's responsibility
			return *this ;
		}
		//
		~Path() { deallocate() ; }
		// services
		void deallocate() { if (allocated) delete[] file ; }
		//
		void allocate(                          ) { if (!allocated) allocate( at      , file      , strlen(file) ) ; }
		void allocate(        size_t sz         ) { if (!allocated) allocate( at      , file      , sz           ) ; }
		void allocate(        ::string const& f ) {                 allocate( Fd::Cwd , f.c_str() , f.size()     ) ; }
		void allocate( Fd a , ::string const& f ) {                 allocate( a       , f.c_str() , f.size()     ) ; }
		void allocate( Fd at_ , const char* file_ , size_t sz ) {
			SWEAR( has_at || at_==Fd::Cwd , has_at ,' ', at_ ) ;
			deallocate() ;
			char* buf = new char[sz+1] ;                                                  // +1 to account for terminating null
			::memcpy(buf,file_,sz+1) ;
			file      = buf  ;
			at        = at_  ;
			allocated = true ;
		}
		void share(const char* file_) { share(Fd::Cwd,file_) ; }
		void share( Fd at_ , const char* file_ ) {
			SWEAR( has_at || at_==Fd::Cwd , has_at ,' ', at_ ) ;
			deallocate() ;
			file      = file_ ;
			at        = at_   ;
			allocated = false ;
		}
		// data
		bool        has_at    = false            ;                                        // if false => at is not managed and may not be substituted any non-default value
		bool        allocated = false            ;                                        // if true <=> file has been allocated and must be freed upon destruction
		FileLoc     file_loc  = FileLoc::Unknown ;                                        // updated when analysis is done
		Fd          at        = Fd::Cwd          ;                                        // at & file may be modified, but together, they always refer to the same file
		const char* file      = ""               ;                                        // .
	} ;
	struct Solve : Path {
		// search (executable if asked so) file in path_var
		Solve()= default ;
		Solve( Record& r , Path&& path , bool no_follow , bool read , bool allow_tmp_map , ::string const& c={} ) : Path{::move(path)} {
			SolveReport sr = r._solve( *this , no_follow , read , c ) ;
			if ( sr.mapped && !allow_tmp_map ) r.report_panic("cannot use tmp mapping to map ",file," to ",sr.real) ;
			real     = ::move(sr.real)  ;
			accesses = sr.last_accesses ;
		}
		// services
		template<class T> T operator()( Record& , T rc ) { return rc ; }
		// data
		::string real     ;
		Accesses accesses ;
	} ;
	struct Chdir : Solve {
		// cxtors & casts
		Chdir() = default ;
		Chdir( Record& , Path&& , ::string&& comment={}) ;
		// services
		int operator()( Record& , int rc , pid_t pid=0 ) ;
	} ;
	struct Chmod : Solve {
		// cxtors & casts
		Chmod() = default ;
		Chmod( Record& , Path&& , bool exe , bool no_follow , ::string&& comment="chmod" ) ;
		// services
		int operator()( Record& , int rc ) ;
	} ;
	struct Exec : Solve {
		// cxtors & casts
		Exec() = default ;
		Exec( Record& , Path&& , bool no_follow , ::string&& comment="exec" ) ;
	} ;
	struct Lnk {
		// cxtors & casts
		Lnk() = default ;
		Lnk( Record& , Path&& src , Path&& dst , bool no_follow , ::string&& comment="lnk" ) ;
		// services
		int operator()( Record& , int rc ) ;
		// data
		Solve src ;
		Solve dst ;
	} ;
	struct Mkdir : Solve {
		Mkdir() = default ;
		Mkdir( Record& , Path&& , ::string&& comment="mkdir" ) ;
	} ;
	struct Open : Solve {
		// cxtors & casts
		Open() = default ;
		Open( Record& , Path&& , int flags , ::string&& comment="open" ) ;
		// services
		int operator()( Record& , int rc ) ;
		// data
		bool do_write = false ;
	} ;
	struct Read : Solve {
		Read() = default ;
		Read( Record& , Path&& , bool no_follow , bool keep_real , bool allow_tmp_map , ::string&& comment="read" ) ;
	} ;
	struct Readlnk : Solve {
		// cxtors & casts
		Readlnk() = default ;
		// buf and sz are only used when mapping tmp
		Readlnk( Record&   , Path&&   , char* buf , size_t sz , ::string&& comment="read_lnk" ) ;
		Readlnk( Record& r , Path&& p ,                         ::string&& c      ="read_lnk" ) : Readlnk{r,::move(p),nullptr/*buf*/,0/*sz*/,::move(c)} {}
		// services
		ssize_t operator()( Record& r , ssize_t len=0 ) ;
		// data
		char*  buf = nullptr/*garbage*/ ;
		size_t sz  = 0      /*garbage*/ ;
	} ;
	struct Rename {
		// cxtors & casts
		Rename() = default ;
		Rename( Record& , Path&& src , Path&& dst , bool exchange , ::string&& comment="rename" ) ;
		// services
		int operator()( Record& , int rc ) ;                                              // if file is updated and did not exist, its date must be capture before the actual syscall
		// data
		Solve src        ;
		Solve dst        ;
		bool  has_unlnks ;
		bool  has_writes ;
	} ;
	struct Stat : Solve {
		// cxtors & casts
		Stat() = default ;
		Stat( Record& , Path&& , bool no_follow , ::string&& comment="stat" ) ;
		// services
		/**/              void operator()( Record&           ) {                            }
		template<class T> T    operator()( Record& , T&& res ) { return ::forward<T>(res) ; }
	} ;
	struct Symlnk : Solve {
		// cxtors & casts
		Symlnk() = default ;
		Symlnk( Record& r , Path&& p , ::string&& comment="symlink" ) ;
		// services
		int operator()( Record& , int rc ) ;
		// data
	} ;
	struct Unlnk : Solve {
		// cxtors & casts
		Unlnk() = default ;
		Unlnk( Record& , Path&& , bool remove_dir=false , ::string&& comment="unlnk" ) ;
		// services
		int operator()( Record& , int rc ) ;
	} ;
	//
	void chdir(const char* dir) { _real_path.chdir(dir) ; }
	//
private :
	SolveReport _solve( Path& , bool no_follow , bool read , ::string const& comment={} ) ;
	// data
	Disk::RealPath                                                _real_path    ;
	mutable Fd                                                    _report_fd    ;
	mutable bool                                                  _tmp_cache    = false ; // record that tmp usage has been reported, no need to report any further
	mutable ::umap_s<pair<Accesses/*accessed*/,Accesses/*seen*/>> _access_cache ;         // map file to read accesses
} ;
