// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "disk.hh"
#include "gather.hh"
#include "rpc_job.hh"
#include "time.hh"

struct Record {
	using Crc         = Hash::Crc                                         ;
	using Ddate       = Time::Ddate                                       ;
	using FileInfo    = Disk::FileInfo                                    ;
	using SolveReport = Disk::RealPath::SolveReport                       ;
	using Proc        = JobExecProc                                       ;
	using GetReplyCb  = ::function<JobExecRpcReply(                    )> ;
	using ReportCb    = ::function<void           (JobExecRpcReq const&)> ;
	// statics
	static bool s_is_simple   (const char*) ;
	static bool s_has_tmp_view(           ) { return +s_autodep_env().tmp_view ;                      }
	static void s_set_enable  (bool e     ) { SWEAR(_s_autodep_env) ; _s_autodep_env->disabled = !e ; }
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
		SWEAR( _s_autodep_env && s_access_cache ) ;
		return *_s_autodep_env ;
	}
	static AutodepEnv const& s_autodep_env(AutodepEnv const& ade) {
		SWEAR( !s_access_cache && !_s_autodep_env ) ;
		_s_autodep_env = new AutodepEnv                                            { ade } ;
		s_access_cache = new ::umap_s<pair<Accesses/*accessed*/,Accesses/*seen*/>> ;
		return *_s_autodep_env ;
	}
	static AutodepEnv const& s_autodep_env(NewType) {
		SWEAR(bool(s_access_cache)==bool(_s_autodep_env)) ;
		if (!_s_autodep_env) {
			_s_autodep_env = new AutodepEnv{New} ;
			s_access_cache = new ::umap_s<pair<Accesses/*accessed*/,Accesses/*seen*/>> ;
		}
		return *_s_autodep_env ;
	}
	// static data
public :
	static bool                                                   s_static_report  ;                                        // if true <=> report deps to s_deps instead of through report_fd() socket
	static ::vmap_s<DepDigest>                                  * s_deps           ;
	static ::string                                             * s_deps_err       ;
	static ::umap_s<pair<Accesses/*accessed*/,Accesses/*seen*/>>* s_access_cache   ;                                        // map file to read accesses
private :
	static AutodepEnv* _s_autodep_env ;
	static Fd          _s_root_fd     ;                                                                                     // a file descriptor to repo root dir
	// cxtors & casts
public :
	Record(                                      ) = default ;
	Record( NewType ,                pid_t pid=0 ) : Record(New,Maybe,pid) {}
	Record( NewType , Bool3 enable , pid_t pid=0 ) : _real_path{s_autodep_env(New),pid} {                                   // avoid using bool as 2nd arg as this could be easily confused with pid_t
		if (enable!=Maybe) s_set_enable(enable==Yes) ;
	}
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
	void hide(int fd) const {
		if (_s_root_fd.fd==fd) _s_root_fd.detach() ;
		if (_report_fd.fd==fd) _report_fd.detach() ;
	}
	void hide( uint min , uint max ) const {
		if ( _s_root_fd.fd>=0 &&  uint(_s_root_fd.fd)>=min && uint(_s_root_fd.fd)<=max ) _s_root_fd.detach() ;
		if ( _report_fd.fd>=0 &&  uint(_report_fd.fd)>=min && uint(_report_fd.fd)<=max ) _report_fd.detach() ;
	}
private :
	void _static_report(JobExecRpcReq&& jerr) const ;
	void _report       (JobExecRpcReq&& jerr) const {
		if (s_autodep_env().disabled) return ;
		if (s_static_report         ) _static_report(::move(jerr))     ;
		else                          OMsgBuf().send(report_fd(),jerr) ;
	}
	JobExecRpcReply _get_reply() const {
		if (s_static_report) return {}                                              ;
		else                 return IMsgBuf().receive<JobExecRpcReply>(report_fd()) ;
	}
	//
	void _report_access( JobExecRpcReq&& jerr                                                   ) const ;
	void _report_access( ::string&& f , FileInfo fi , Accesses a , bool write , ::string&& c={} ) const {
		_report_access({ Proc::Access , {{::move(f),fi}} , {.write=Maybe&write,.accesses=a} , ::move(c) }) ;
	}
	// for modifying accesses (_report_update, _report_target, _report_unlnk, _report_targets) :
	// - if we report after  the access, it may be that job is interrupted inbetween and repo is modified without server being notified and we have a manual case
	// - if we report before the access, we may notify an access that will not occur if job is interrupted or if access is finally an error
	// so the choice is to manage Maybe :
	// - access is reported as Maybe before the access
	// - it is then confirmed (with an ok arg to manage errors) after the access
	// in job_exec, if an access is left Maybe, i.e. if job is interrupted between the Maybe reporting and the actual access, disk is interrogated to see if access did occur
	//
	//                                                                                                                                     write
	void _report_dep   ( ::string&& f , FileInfo fi , Accesses a , ::string&& c={} ) const { if (+a) _report_access( ::move(f) , fi , a  , false , ::move(c) ) ; }
	void _report_update( ::string&& f , FileInfo fi , Accesses a , ::string&& c={} ) const {         _report_access( ::move(f) , fi , a  , true  , ::move(c) ) ; }
	void _report_target( ::string&& f ,                            ::string&& c={} ) const {         _report_access( ::move(f) , {} , {} , true  , ::move(c) ) ; }
	void _report_unlnk ( ::string&& f ,                            ::string&& c={} ) const {         _report_access( ::move(f) , {} , {} , true  , ::move(c) ) ; }
	//
	void _report_update( ::string&& f , Accesses a , ::string&& c={} ) const { _report_update( ::move(f) , +a?FileInfo(s_root_fd(),f):FileInfo() , a , ::move(c) ) ; }
	void _report_dep   ( ::string&& f , Accesses a , ::string&& c={} ) const { _report_dep   ( ::move(f) , +a?FileInfo(s_root_fd(),f):FileInfo() , a , ::move(c) ) ; }
	//
	void _report_deps( ::vector_s const& fs , Accesses a , bool u , ::string&& c={} ) const {
		::vmap_s<FileInfo> files ;
		for( ::string const& f : fs ) files.emplace_back( f , FileInfo(s_root_fd(),f) ) ;
		_report_access({ Proc::Access , ::move(files) , {.write=Maybe&u,.accesses=a} , ::move(c) }) ;
	}
	void _report_targets( ::vector_s&& fs , ::string&& c={} ) const {
		vmap_s<FileInfo> files ;
		for( ::string& f : fs ) files.emplace_back(::move(f),FileInfo()) ;
		_report_access({ Proc::Access , ::move(files) , {.write=Maybe} , ::move(c) }) ;
	}
	void _report_tmp( bool sync=false , ::string&& c={} ) const {
		if      (!_tmp_cache) _tmp_cache = true ;
		else if (!sync     ) return ;
		_report({Proc::Tmp,sync,::move(c)}) ;
	}
	void _report_confirm( FileLoc fl , bool ok ) const {
		if (fl==FileLoc::Repo) _report({ Proc::Confirm , ok }) ;
	}
	void _report_guard( ::string&& f , ::string&& c={} ) const {
		_report({ Proc::Guard , {::move(f)} , ::move(c) }) ;
	}
public :
	template<class... A> [[noreturn]] void report_panic(A const&... args) { _report({Proc::Panic,to_string(args...)}) ; exit(Rc::Usage) ; } // continuing is meaningless
	template<class... A>              void report_trace(A const&... args) { _report({Proc::Trace,to_string(args...)}) ;                   }
	JobExecRpcReply direct( JobExecRpcReq&& jerr) ;
	//
	template<bool Writable=false> struct _Path {
		using Char = ::conditional_t<Writable,char,const char> ;
		// cxtors & casts
		_Path(                                             )                                          {                                  }
		_Path( Fd a                                        ) : has_at{true} , at{a}                   {                                  }
		_Path(        Char*           f , bool steal=true  ) :                        file{f        } { if (!steal) allocate(        ) ; }
		_Path( Fd a , Char*           f , bool steal=true  ) : has_at{true} , at{a} , file{f        } { if (!steal) allocate(        ) ; }
		_Path(        ::string const& f , bool steal=false ) :                        file{f.c_str()} { if (!steal) allocate(f.size()) ; }
		_Path( Fd a , ::string const& f , bool steal=false ) : has_at{true} , at{a} , file{f.c_str()} { if (!steal) allocate(f.size()) ; }
		//
		_Path(_Path && p) { *this = ::move(p) ; }
		_Path& operator=(_Path&& p) {
			deallocate() ;
			has_at      = p.has_at    ;
			file_loc    = p.file_loc  ;
			at          = p.at        ;
			file        = p.file      ;
			allocated   = p.allocated ;
			p.file      = nullptr     ;                          // safer to avoid dangling pointers
			p.allocated = false       ;                          // we have clobbered allocation, so it is no more p's responsibility
			return *this ;
		}
		//
		~_Path() { deallocate() ; }
		// services
		void deallocate() { if (allocated) delete[] file ; }
		//
		void allocate(                            ) { if (!allocated) allocate( at      , file      , strlen(file) ) ; }
		void allocate(          size_t sz         ) { if (!allocated) allocate( at      , file      , sz           ) ; }
		void allocate(          ::string const& f ) {                 allocate( Fd::Cwd , f.c_str() , f.size()     ) ; }
		void allocate( Fd a   , ::string const& f ) {                 allocate( a       , f.c_str() , f.size()     ) ; }
		void allocate( Fd at_ , const char* file_ , size_t sz ) {
			SWEAR( has_at || at_==Fd::Cwd , has_at ,' ', at_ ) ;
			char* buf = new char[sz+1] ;                         // +1 to account for terminating null
			::memcpy(buf,file_,sz+1) ;
			deallocate() ;                                       // safer to deallocate after memcpy in case file_ points into file
			file      = buf  ;
			at        = at_  ;
			allocated = true ;
		}
		// data
		bool    has_at    = false            ;                   // if false => at is not managed and may not be substituted any non-default value
		bool    allocated = false            ;                   // if true <=> file has been allocated and must be freed upon destruction
		FileLoc file_loc  = FileLoc::Unknown ;                   // updated when analysis is done
		Fd      at        = Fd::Cwd          ;                   // at & file may be modified, but together, they always refer to the same file ...
		Char*   file      = nullptr          ;                   // ... except in the case of mkstemp (& al.) that modifies its arg in place
	} ;
	using Path  = _Path<false/*Writable*/> ;
	using WPath = _Path<true /*Writable*/> ;
	template<bool Writable=false> struct _Solve : _Path<Writable> {
		using Base = _Path<Writable> ;
		using Base::allocate ;
		using Base::has_at   ;
		using Base::file_loc ;
		using Base::at       ;
		using Base::file     ;
		// search (executable if asked so) file in path_var
		_Solve()= default ;
		_Solve( Record& r , Base&& path , bool no_follow , bool read , bool allow_tmp_map , ::string const& c={} ) : Base{::move(path)} {
			if ( !file || !file[0] ) return ;
			//
			SolveReport sr = r._real_path.solve(at,file,no_follow) ;
			if (sr.file_accessed==Yes) accesses = Access::Lnk     ;
			/**/                       file_loc = sr.file_loc     ;
			/**/                       real     = ::move(sr.real) ;
			//
			for( ::string& lnk : sr.lnks )            r._report_dep( ::move(lnk)          ,              Access::Lnk , c+".lnk"  ) ;
			if ( !read && sr.file_accessed==Maybe   ) r._report_dep( Disk::dir_name(real) , FileInfo() , Access::Lnk , c+".last" ) ; // real dir is not protected by real
			if ( !read && sr.file_loc==FileLoc::Tmp ) r._report_tmp(                                                             ) ;
			//
			if (!sr.mapped) return ;
			//
			if      (!allow_tmp_map    ) r.report_panic("cannot use tmp mapping to map ",file," to ",real) ;
			else if (Disk::is_abs(real)) allocate( +real?real:"/"s                              ) ;                                  // dont share real with file as real may be moved
			else if (has_at            ) allocate( s_root_fd() , real                           ) ;
			else                         allocate( to_string(s_autodep_env().root_dir,'/',real) ) ;
		}
		// services
		template<class T> T operator()( Record& , T rc ) { return rc ; }
		// data
		::string real     ;
		Accesses accesses ;                                                                                                          // Access::Lnk if real was accessed as a sym link
	} ;
	using Solve  = _Solve<false/*Writable*/> ;
	using WSolve = _Solve<true /*Writable*/> ;
	struct Chdir : Solve {
		// cxtors & casts
		Chdir() = default ;
		Chdir( Record& , Path&& , ::string&& comment ) ;
		// services
		int operator()( Record& , int rc , pid_t=0 ) ;
	} ;
	struct Chmod : Solve {
		// cxtors & casts
		Chmod() = default ;
		Chmod( Record& , Path&& , bool exe , bool no_follow , ::string&& comment ) ;
		// services
		int operator()( Record& r , int rc ) { r._report_confirm(file_loc,rc>=0) ; return rc ; }
	} ;
	struct Hide {
		Hide( Record&            ) {              }                                                                                  // in case nothing to hide, just to ensure invariants
		Hide( Record& r , int fd ) { r.hide(fd) ; }
		#if HAS_CLOSE_RANGE
			#ifdef CLOSE_RANGE_CLOEXEC
				Hide( Record& r , uint fd1 , uint fd2 , int flgs ) { if (!(flgs&CLOSE_RANGE_CLOEXEC)) r.hide(int(fd1),int(fd2)) ; }
			#else
				Hide( Record& r , uint fd1 , uint fd2 , int      ) {                                  r.hide(int(fd1),int(fd2)) ; }
			#endif
		#endif
		template<class T> T operator()( Record& , T rc ) { return rc ; }
	} ;
	struct Exec : Solve {
		// cxtors & casts
		Exec() = default ;
		Exec( Record& , Path&& , bool no_follow , ::string&& comment ) ;
	} ;
	struct Lnk {
		// cxtors & casts
		Lnk() = default ;
		Lnk( Record& , Path&& src , Path&& dst , bool no_follow , ::string&& comment ) ;
		// services
		int operator()( Record& r , int rc ) { r._report_confirm(dst.file_loc,rc>=0) ; return rc ; }
		// data
		Solve src ;
		Solve dst ;
	} ;
	struct Mkdir : Solve {
		Mkdir() = default ;
		Mkdir( Record& , Path&& , ::string&& comment ) ;
	} ;
	struct Open : Solve {
		// cxtors & casts
		Open() = default ;
		Open( Record& , Path&& , int flags , ::string&& comment ) ;
		// services
		int operator()( Record& r , int rc ) { { if (confirm) r._report_confirm(file_loc,rc>=0) ; } return rc ; }
		// data
		bool confirm = false ;
	} ;
	struct Read : Solve {
		Read() = default ;
		Read( Record& , Path&& , bool no_follow , bool keep_real , bool allow_tmp_map , ::string&& comment ) ;
	} ;
	struct Readlink : Solve {
		// cxtors & casts
		Readlink() = default ;
		// buf and sz are only used when mapping tmp
		Readlink( Record& , Path&& , char* buf , size_t sz , ::string&& comment ) ;
		// services
		ssize_t operator()( Record& , ssize_t len=0 ) ;
		// data
		char*  buf      = nullptr ;
		size_t sz       = 0       ;
		bool   emulated = false   ;                                                                                                  // if true <=> backdoor was used
	} ;
	struct Rename {
		// cxtors & casts
		Rename() = default ;
		Rename( Record& , Path&& src , Path&& dst , bool exchange , bool no_replace , ::string&& comment ) ;
		// services
		int operator()( Record& r , int rc ) { r._report_confirm( src.file_loc&dst.file_loc , rc>=0 ) ; return rc ; }
		// data
		Solve src        ;
		Solve dst        ;
		bool  has_unlnks ;
		bool  has_writes ;
	} ;
	struct Stat : Solve {
		// cxtors & casts
		Stat() = default ;
		Stat( Record& , Path&& , bool no_follow , ::string&& comment ) ;
		// services
		/**/              void operator()( Record&           ) {                            }
		template<class T> T    operator()( Record& , T&& res ) { return ::forward<T>(res) ; }
	} ;
	struct Symlnk : Solve {
		// cxtors & casts
		Symlnk() = default ;
		Symlnk( Record& r , Path&& p , ::string&& comment ) ;
		// services
		int operator()( Record& r , int rc ) { r._report_confirm(file_loc,rc>=0) ; return rc ; }
		// data
	} ;
	struct Unlnk : Solve {
		// cxtors & casts
		Unlnk() = default ;
		Unlnk( Record& , Path&& , bool remove_dir , ::string&& comment ) ;
		// services
		int operator()( Record& r , int rc ) { r._report_confirm(file_loc,rc>=0) ; return rc ; }
	} ;
	//
	void chdir(const char* dir) {
		seen_chdir = true ;
		_real_path.chdir(dir) ;
	}
	//
	// data
	bool seen_chdir = false ;
private :
	Disk::RealPath      _real_path ;
	mutable AutoCloseFd _report_fd ;
	mutable bool        _tmp_cache = false ; // record that tmp usage has been reported, no need to report any further
} ;

template<bool Writable=false> ::ostream& operator<<( ::ostream& os , Record::_Path<Writable> const& p ) {
	/**/               os << "Path("      ;
	if (p.at!=Fd::Cwd) os << p.at   <<',' ;
	return             os << p.file <<')' ;
}
