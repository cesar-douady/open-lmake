// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
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
	static bool s_is_simple   (const char*) ;
	static bool s_has_tmp_view(           ) { return !s_autodep_env().tmp_view.empty() ; }
	//
	static Fd s_root_fd() {
		if (!_s_root_fd) {
			_s_root_fd = Disk::open_read(s_autodep_env().root_dir) ; _s_root_fd.no_std() ; // avoid poluting standard descriptors
			SWEAR(+_s_root_fd) ;
		}
		return _s_root_fd ;
	}
	static Fd s_report_fd() {
		if (!_s_report_fd) {
			// establish connection with server
			::string const& service = s_autodep_env().service ;
			if (service.back()==':') _s_report_fd = Disk::open_write( service.substr(0,service.size()-1) , true/*append*/ ) ;
			else                     _s_report_fd = ClientSockFd(service) ;
			_s_report_fd.no_std() ;                                                   // avoid poluting standard descriptors
			swear_prod(+_s_report_fd,"cannot connect to job_exec through ",service) ;
		}
		return _s_report_fd ;
	}
	// analyze flags in such a way that it works with all possible representations of O_RDONLY, O_WRITEONLY and O_RDWR : could be e.g. 0,1,2 or 1,2,3 or 1,2,4
	static AutodepEnv const& s_autodep_env() {
		if (!_s_autodep_env) _s_autodep_env =new AutodepEnv{getenv("LMAKE_AUTODEP_ENV")} ;
		return *_s_autodep_env ;
	}
	static AutodepEnv const& s_autodep_env(AutodepEnv const& ade) {
		SWEAR( !_s_autodep_env , _s_autodep_env ) ;
		_s_autodep_env = new AutodepEnv{ade} ;
		return *_s_autodep_env ;
	}
	static void s_hide(int fd) {                                               // guaranteed syscall free, so no need for caller to protect errno
		if (_s_root_fd  .fd==fd) _s_root_fd  .detach() ;
		if (_s_report_fd.fd==fd) _s_report_fd.detach() ;
	}
	static void s_hide_range( int min , int max=~0u ) {                             // guaranteed syscall free, so no need for caller to protect errno
		if ( _s_root_fd  .fd>=min && _s_root_fd  .fd<=max ) _s_root_fd  .detach() ;
		if ( _s_report_fd.fd>=min && _s_report_fd.fd<=max ) _s_report_fd.detach() ;
	}
private :
	static void            _s_report   ( JobExecRpcReq const& jerr ) { OMsgBuf().send(s_report_fd(),jerr) ;                       }
	static JobExecRpcReply _s_get_reply(                           ) { return IMsgBuf().receive<JobExecRpcReply>(s_report_fd()) ; }
	// static data
	static Fd          _s_root_fd     ;
	static Fd          _s_report_fd   ;
	static AutodepEnv* _s_autodep_env ;
	// cxtors & casts
public :
	Record(pid_t pid=0) : real_path{s_autodep_env(),pid} {}
	// services
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
	void _report_target ( ::string  && f  , ::string const& c={} ) const { _report_access( JobExecRpcReq( JobExecRpcProc::Access , {{::move(f),DD()}} , {.write =true} , c ) ) ; }
	void _report_unlink ( ::string  && f  , ::string const& c={} ) const { _report_access( JobExecRpcReq( JobExecRpcProc::Access , {{::move(f),DD()}} , {.unlink=true} , c ) ) ; }
	void _report_targets( ::vector_s&& fs , ::string const& c={} ) const {
		vmap_s<DD> mdd ;
		for( ::string& f : fs ) mdd.emplace_back(::move(f),DD()) ;
		_report_access( JobExecRpcReq( JobExecRpcProc::Access , ::move(mdd) , {.write =true} , c ) ) ;
	}
	void _report_tmp( bool sync=false , ::string const& comment={} ) const {
		if      (!tmp_cache) tmp_cache = true ;
		else if (!sync     ) return ;
		_s_report(JobExecRpcReq(JobExecRpcProc::Tmp,sync,comment)) ;
	}
public :
	template<class... A> void report_trace(A const&... args) {
		_s_report( JobExecRpcReq(JobExecRpcProc::Trace,to_string(args...)) ) ;
	}
	JobExecRpcReply backdoor( JobExecRpcReq&& jerr                    ) ;
	ssize_t         backdoor( const char* msg , char* buf , size_t sz ) ;
	//
	struct Path {
		using Kind = Disk::Kind ;
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
			kind        = p.kind      ;
			at          = p.at        ;
			file        = p.file      ;
			allocated   = p.allocated ;
			p.allocated = false       ; // we have clobbered allocation, so it is no more p's responsibility
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
			char* buf = new char[sz+1] ;                                       // +1 to account for terminating null
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
		bool        has_at    = false         ;            // if false => at is not managed and may not be substituted any non-default value
		bool        allocated = false         ;            // if true <=> file has been allocated and must be freed upon destruction
		Kind        kind      = Kind::Unknown ;            // updated when analysis is done
		Fd          at        = Fd::Cwd       ;            // at & file may be modified, but together, they always refer to the same file
		const char* file      = ""            ;            // .
	} ;
	struct Real : Path {
		// cxtors & casts
		Real() = default ;
		Real( Path&& path , ::string const& comment_={} ) : Path{::move(path)} , comment{comment_} {}
		// services
		template<class T> T operator()( Record& , T rc ) { return rc ; }
		// data
		::string real    = {} ;
		::string comment = {} ;
	} ;
	struct Solve : Real {
		// search (executable if asked so) file in path_var
		Solve()= default ;
		Solve( Record& r , Path&& path , bool no_follow , ::string const& comment_={} ) : Real{::move(path),comment_} {
			real = r._solve( *this , no_follow , comment ).real ;
		}
	} ;
	struct ChDir : Solve {
		// cxtors & casts
		ChDir() = default ;
		ChDir( Record& , Path&& ) ;
		// services
		int operator()( Record& , int rc , pid_t pid=0 ) ;
	} ;
	struct Chmod : Solve {
		// cxtors & casts
		Chmod() = default ;
		Chmod( Record& , Path&& , bool exe , bool no_follow , ::string const& comment="write" ) ;
		// services
		int operator()( Record& , int rc , bool no_file=true ) ;
		// data
		DD date ;  // if file is updated, its date may have to be captured before the actual syscall
	} ;
	struct Exec : Solve {
		// cxtors & casts
		Exec() = default ;
		Exec( Record& , Path&& , bool no_follow , ::string const& comment="exec" ) ;
	} ;
	struct Lnk {
		// cxtors & casts
		Lnk() = default ;
		Lnk( Record& , Path&& src , Path&& dst , int flags=0 , ::string const& comment="lnk" ) ;
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
		Open( Record& , Path&& , int flags , ::string const& comment="open" ) ;
		// services
		int operator()( Record& , bool has_fd , int fd_rc , bool no_file=false ) ;   // no_file is only meaningful if rc is in error
		// data
		bool do_read  = false ;
		bool do_write = false ;
		DD   date     ;                // if file is updated and did not exist, its date must be captured before the actual syscall
	} ;
	struct Read : Solve {
		// cxtors & casts
		Read() = default ;
		Read( Record& , Path&& , bool no_follow , ::string const& comment="read" ) ;
	} ;
	struct ReadLnk : Solve {
		// cxtors & casts
		ReadLnk() = default ;
		// buf and sz are only used when mapping tmp or processing backdoor
		ReadLnk( Record&   , Path&&   , char* buf , size_t sz , ::string const& comment="read_lnk" ) ;
		ReadLnk( Record& r , Path&& p ,                         ::string const& comment="read_lnk" ) : ReadLnk{r,::move(p),nullptr/*buf*/,0/*sz*/,comment} {
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
		Rename( Record& , Path&& src , Path&& dst , uint flags=0 , ::string const& comment="rename" ) ;
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
		Stat( Record& , Path&& , bool no_follow , ::string const& comment="stat" ) ;
		// services
		int operator()( Record& , int rc=0 , bool no_file=true ) ;             // by default, be pessimistic : success & if specified as error, consider we are missing the file
		template<class T> T* operator()( Record& r , T* res , bool no_file ) {
			(*this)( r , -!res , no_file ) ;                                   // map null (indicating error) to -a and non-null (indicating success) to 0
			return res ;
		}
	} ;
	struct Symlnk : Solve {
		// cxtors & casts
		Symlnk() = default ;
		Symlnk( Record& r , Path&& p , ::string const& c="write" ) : Solve{r,::move(p),true/*no_follow*/,c} {}
		// services
		int operator()( Record& , int rc ) ;
		// data
	} ;
	struct Unlink : Solve {
		// cxtors & casts
		Unlink() = default ;
		Unlink( Record& , Path&& , bool remove_dir=false , ::string const& comment="unlink" ) ;
		// services
		int operator()( Record& , int rc ) ;
	} ;
	//
	void chdir(const char* dir) { swear(Disk::is_abs(dir),"dir should be absolute : ",dir) ; real_path.cwd_ = dir ; }
	//
protected :
	SolveReport _solve( Path& , bool no_follow , ::string const& comment={} ) ;
	//
	// data
protected :
	Disk::RealPath             real_path    ;
	mutable bool               tmp_cache    = false   ;    // record that tmp usage has been reported, no need to report any further
	mutable ::umap_s<Accesses> access_cache ;              // map file to read accesses
} ;
