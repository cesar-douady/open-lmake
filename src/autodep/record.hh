// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "disk.hh"
#include "gather.hh"
#include "rpc_job_exec.hh"
#include "time.hh"

struct Record ;

struct Record {
	using Crc         = Hash::Crc                                         ;
	using Ddate       = Time::Ddate                                       ;
	using FileInfo    = Disk::FileInfo                                    ;
	using SolveReport = Disk::RealPath::SolveReport                       ;
	using Proc        = JobExecProc                                       ;
	using GetReplyCb  = ::function<JobExecRpcReply(                    )> ;
	using ReportCb    = ::function<void           (JobExecRpcReq const&)> ;
	// statics
	static bool s_is_simple (const char*) ;
	//
	static Fd s_root_fd() {
		SWEAR(_s_autodep_env) ;
		if (!_s_root_fd) {
			_s_root_fd = { Disk::open_read(_s_autodep_env->root_dir) , true/*no_std*/ } ;                                     // avoid poluting standard descriptors
			SWEAR(+_s_root_fd) ;
		}
		return _s_root_fd ;
	}
	static Fd s_report_fd() {
		if ( !_s_report_fd && +_s_autodep_env->service ) {
			// establish connection with server
			::string const& service = _s_autodep_env->service ;
			if (service.back()==':') _s_report_fd = Disk::open_write( service.substr(0,service.size()-1) , true/*append*/ ) ;
			else                     _s_report_fd = ClientSockFd(service).detach()                                          ;
			_s_report_fd.no_std() ;                                                                                           // avoid poluting standard descriptors
			swear_prod(+_s_report_fd,"cannot connect to job_exec through ",service) ;
		}
		return _s_report_fd ;
	}
	static void s_close_report() {
		_s_report_fd.close() ;
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
	static void s_hide(int fd) {
		if (_s_root_fd  .fd==fd) _s_root_fd  .detach() ;
		if (_s_report_fd.fd==fd) _s_report_fd.detach() ;
	}
	static void s_hide( uint min , uint max ) {
		if ( _s_root_fd  .fd>=0 &&  uint(_s_root_fd  .fd)>=min && uint(_s_root_fd  .fd)<=max ) _s_root_fd  .detach() ;
		if ( _s_report_fd.fd>=0 &&  uint(_s_report_fd.fd)>=min && uint(_s_report_fd.fd)<=max ) _s_report_fd.detach() ;
	}
	// static data
public :
	static bool                                                   s_static_report  ;                                      // if true <=> report deps to s_deps instead of through s_report_fd() socket
	static ::vmap_s<DepDigest>                                  * s_deps           ;
	static ::string                                             * s_deps_err       ;
	static ::umap_s<pair<Accesses/*accessed*/,Accesses/*seen*/>>* s_access_cache   ;                                      // map file to read accesses
private :
	static AutodepEnv* _s_autodep_env ;
	static Fd          _s_root_fd     ;                                                                                   // a file descriptor to repo root dir
	static Fd          _s_report_fd   ;
	// cxtors & casts
public :
	Record(                                            ) = default ;
	Record( NewType ,                      pid_t pid   ) : Record( New , Maybe , pid ) {}
	Record( NewType , Bool3 enable=Maybe , pid_t pid=0 ) : _real_path{s_autodep_env(New),pid} {
		if (enable==Maybe) disabled = s_autodep_env().disabled ;
		else               disabled = enable==No               ;
	}
	// services
private :
	void _static_report(JobExecRpcReq&& jerr) const ;
	JobExecRpcReply _get_reply() const {
		if (s_static_report) return {}                                                ;
		else                 return IMsgBuf().receive<JobExecRpcReply>(s_report_fd()) ;
	}
	//
	void report_access( ::string&& f , FileInfo fi , Accesses a , bool write , ::string&& c={} , bool force=false ) const {
		report_access( { Proc::Access , {{::move(f),fi}} , {.write=Maybe&write,.accesses=a} , ::move(c) } , force ) ;
	}
	// for modifying accesses (_report_update, _report_target, _report_unlnk, _report_targets) :
	// - if we report after  the access, it may be that job is interrupted inbetween and repo is modified without server being notified and we have a manual case
	// - if we report before the access, we may notify an access that will not occur if job is interrupted or if access is finally an error
	// so the choice is to manage Maybe :
	// - access is reported as Maybe before the access
	// - it is then confirmed (with an ok arg to manage errors) after the access
	// in job_exec, if an access is left Maybe, i.e. if job is interrupted between the Maybe reporting and the actual access, disk is interrogated to see if access did occur
	//
	//                                                                                                                                                                        write
	void _report_dep   ( FileLoc fl , ::string&& f , FileInfo fi , Accesses a , ::string&& c={} ) const { if ( fl<=FileLoc::Dep  && +a ) report_access( ::move(f) , fi , a  , false , ::move(c) ) ; }
	void _report_target( FileLoc fl , ::string&& f ,                            ::string&& c={} ) const { if ( fl<=FileLoc::Repo       ) report_access( ::move(f) , {} , {} , true  , ::move(c) ) ; }
	void _report_unlnk ( FileLoc fl , ::string&& f ,                            ::string&& c={} ) const { if ( fl<=FileLoc::Repo       ) report_access( ::move(f) , {} , {} , true  , ::move(c) ) ; }
	//
	void _report_dep   ( FileLoc fl , ::string&& f , Accesses a , ::string&& c={} ) const { if (+a) _report_dep( fl , ::move(f) , FileInfo(s_root_fd(),f) , a , ::move(c) ) ; }
	//
	void _report_update( FileLoc fl , ::string&& f , ::string&& f0 , FileInfo fi , Accesses a , ::string&& c={} ) const { // f0 is the file to which we write if non-empty
		if (!f0) { //!                                                                write
			if      ( fl<=FileLoc::Repo       ) report_access( ::move(f ) , fi , a  , true  , ::move(c) ) ;
			else if ( fl<=FileLoc::Dep  && +a ) report_access( ::move(f ) , fi , a  , false , ::move(c) ) ;
		} else {
			if      ( fl<=FileLoc::Dep  && +a ) report_access( ::move(f ) , fi , a  , false , ::move(c) ) ;
			if      ( fl<=FileLoc::Repo       ) report_access( ::move(f0) , fi , {} , true  , ::move(c) ) ;
		}
	}
	void _report_update( FileLoc fl , ::string&& f , ::string&& f0 , Accesses a , ::string&& c={} ) const {
		_report_update( fl , ::move(f) , ::move(f0) , +a?FileInfo(s_root_fd(),f):FileInfo() , a , ::move(c) ) ;
	}
	//
	void _report_deps( ::vector_s const& fs , Accesses a , bool u , ::string&& c={} ) const {
		::vmap_s<FileInfo> files ;
		for( ::string const& f : fs ) files.emplace_back( f , FileInfo(s_root_fd(),f) ) ;
		report_access({ Proc::Access , ::move(files) , {.write=Maybe&u,.accesses=a} , ::move(c) }) ;
	}
	void _report_targets( ::vector_s&& fs , ::string&& c={} ) const {
		vmap_s<FileInfo> files ;
		for( ::string& f : fs ) files.emplace_back(::move(f),FileInfo()) ;
		report_access({ Proc::Access , ::move(files) , {.write=Maybe} , ::move(c) }) ;
	}
	void _report_tmp( bool sync=false , ::string&& c={} ) const {
		if      (!_tmp_cache) _tmp_cache = true ;
		else if (!sync     ) return ;
		report_direct({Proc::Tmp,sync,::move(c)}) ;
	}
	void _report_confirm( FileLoc fl , bool ok ) const {
		if (fl==FileLoc::Repo) report_direct({ Proc::Confirm , ok }) ;
	}
	void _report_guard( FileLoc fl , ::string&& f , ::string&& c={} ) const {
		if (fl<=FileLoc::Repo) report_direct({ Proc::Guard , ::move(f) , ::move(c) }) ;
	}
public :
	bool/*sent*/ report_direct( JobExecRpcReq&& jerr , bool force=false ) const {
		//                                                                       sent
		if ( !force && disabled        )                                  return false ;
		if ( s_static_report           ) { _static_report(::move(jerr)) ; return true  ; }
		if ( Fd fd=s_report_fd() ; +fd ) { OMsgBuf().send(fd,jerr)      ; return true  ; }
		/**/                                                              return false ;
	}
	JobExecRpcReply report_sync_direct( JobExecRpcReq&& , bool force=false ) const ;
	bool            report_access     ( JobExecRpcReq&& , bool force=false ) const ;
	JobExecRpcReply report_sync_access( JobExecRpcReq&& , bool force=false ) const ;
	//
	[[noreturn]] void report_panic(::string&& s) const { report_direct({Proc::Panic,::move(s)}) ; exit(Rc::Usage) ; } // continuing is meaningless
	/**/         void report_trace(::string&& s) const { report_direct({Proc::Trace,::move(s)}) ;                   }
	//
	template<bool Writable=false> struct _Path {                                                                      // if !Writable <=> file is is read-only
		using Char = ::conditional_t<Writable,char,const char> ;
		// cxtors & casts
		_Path(                          )                           {                       }
		_Path( Fd a                     ) : at{a}                   {                       }
		_Path(        Char*           f ) :         file{f        } {                       }
		_Path( Fd a , Char*           f ) : at{a} , file{f        } {                       }
		_Path(        ::string const& f ) :         file{f.c_str()} { _allocate(f.size()) ; }
		_Path( Fd a , ::string const& f ) : at{a} , file{f.c_str()} { _allocate(f.size()) ; }
		//
		_Path(_Path && p) { *this = ::move(p) ; }
		_Path& operator=(_Path&& p) {
			_deallocate() ;
			file_loc    = p.file_loc  ;
			at          = p.at        ;
			file        = p.file      ;
			allocated   = p.allocated ;
			p.file      = nullptr     ;                                                                               // safer to avoid dangling pointers
			p.allocated = false       ;                                                                               // we have clobbered allocation, so it is no more p's responsibility
			return *this ;
		}
		//
		~_Path() { _deallocate() ; }
		// services
	private :
		void _deallocate() { if (allocated) delete[] file ; }
		//
		void _allocate(size_t sz) {
			char* buf = new char[sz+1] ;                                                                              // +1 to account for terminating null
			::memcpy(buf,file,sz+1) ;
			file      = buf  ;
			allocated = true ;
		}
		// data
	public :
		bool    allocated = false            ;                                                                        // if true <=> file has been allocated and must be freed upon destruction
		FileLoc file_loc  = FileLoc::Unknown ;                                                                        // updated when analysis is done
		Fd      at        = Fd::Cwd          ;                                                                        // at & file may be modified, but together, they always refer to the same file ...
		Char*   file      = nullptr          ;                                                                        // ... except in the case of mkstemp (& al.) that modifies its arg in place
	} ; //!            Writable
	using Path  = _Path<false > ;
	using WPath = _Path<true  > ;
	template<bool Writable=false,bool ChkSimple=false> struct _Solve : _Path<Writable> {
		using Base = _Path<Writable> ;
		using Base::file_loc ;
		using Base::at       ;
		using Base::file     ;
		_Solve()= default ;
		_Solve( Record& r , Base&& path , bool no_follow , bool read , bool create , ::string const& c={} ) : Base{::move(path)} {
			if (ChkSimple) { if ( s_is_simple(file) ) return ; }
			else           { if ( !file || !file[0] ) return ; }
			//
			SolveReport sr = r._real_path.solve(at,file,no_follow) ;
			//
			auto report_dep = [&]( FileLoc file_loc , ::string&& file , Accesses a , bool store , const char* key )->void {
				::string ck = c+'.'+key ;
				for( auto const& [view,phys] : s_autodep_env().views ) {
					if (!( file.starts_with(view) && (view.back()=='/'||file.size()==view.size()) )) continue ;
					for( size_t i=0 ; i<phys.size() ; i++ ) {
						bool     last  = i==phys.size()-1                                 ;
						::string f     = phys[i] + file.substr(view.size())               ;
						FileInfo fi    = !last||+a ? FileInfo(s_root_fd(),f) : FileInfo() ;
						bool     found = fi.tag()!=FileTag::None                          ;
						if (store) {
							if      (last ) real  = f ;
							else if (found) real  = f ;
							else if (i==0 ) real0 = f ;                                                               // real0 is only significative when not equal to real
						}
						if      (last ) { if (+a) r._report_dep( r._real_path.file_loc(f) , ::move(f) , fi , a              , ck+i ) ; return ; }
						else if (found) {         r._report_dep( r._real_path.file_loc(f) , ::move(f) , fi , a|Access::Stat , ck+i ) ; return ; }
						else                      r._report_dep( r._real_path.file_loc(f) , ::move(f) , fi ,   Access::Stat , ck+i ) ;
					}
					return ;
				}
				// when no views match, process as if last
				if (+a) { if (store) real = ::move(file) ;                                                             }
				else    { if (store) real =        file  ; r._report_dep( file_loc , ::move(file) , a , ::move(ck) ) ; }
			} ;
			//
			if (sr.file_accessed==Yes) accesses = Access::Lnk ;
			/**/                       file_loc = sr.file_loc ;
			//                                                                                                                      accesses      store
			for( ::string& lnk : sr.lnks                                     ) report_dep( FileLoc::Dep , ::move(lnk)             , Access::Lnk , false , "lnk"  ) ;
			if ( !read  && sr.file_accessed==Maybe && Disk::has_dir(sr.real) ) report_dep( file_loc     , Disk::dir_name(sr.real) , Access::Lnk , false , "last" ) ; // real dir is not protected ...
			/**/                                                               report_dep( file_loc     , ::move(sr.real)         , {}          , true  , "file" ) ; // ... by real
			//
			if ( create && sr.file_loc==FileLoc::Tmp ) r._report_tmp() ;
		}
		// services
		template<class T> T operator()( Record& , T rc ) { return rc ; }
		//
		::string const& real_write() const { return +real0 ? real0 : real ; }
		::string      & real_write()       { return +real0 ? real0 : real ; }
		// data
		::string real     ;
		::string real0    ;                                                // real in case reading and writing is to different files because of overlays
		Accesses accesses ;                                                // Access::Lnk if real was accessed as a sym link
	} ; //!              Writable,ChkSimple
	using Solve    = _Solve<false  ,false   > ;
	using WSolve   = _Solve<true   ,false   > ;
	using SolveCS  = _Solve<false  ,true    > ;
	using WSolveCS = _Solve<true   ,true    > ;
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
		Hide( Record&          ) {              }                        // in case nothing to hide, just to ensure invariants
		Hide( Record& , int fd ) { s_hide(fd) ; }
		#if HAS_CLOSE_RANGE
			#ifdef CLOSE_RANGE_CLOEXEC
				Hide( Record& , uint fd1 , uint fd2 , int flgs ) { if (!(flgs&CLOSE_RANGE_CLOEXEC)) s_hide(int(fd1),int(fd2)) ; }
			#else
				Hide( Record& , uint fd1 , uint fd2 , int      ) {                                  s_hide(int(fd1),int(fd2)) ; }
			#endif
		#endif
		template<class T> T operator()( Record& , T rc ) { return rc ; }
	} ;
	struct Exec : SolveCS {
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
	struct Mount {
		Mount() = default ;
		Mount( Record& , Path&& src , Path&& dst , ::string&& comment ) ;
		int operator()( Record& , int rc ) { return rc ; }
		// data
		Solve src ;
		Solve dst ;
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
	template<bool ChkSimple=false> struct _Read : _Solve<false/*Writable*/,ChkSimple> {
		using Base = _Solve<false/*Writable*/,ChkSimple> ;
		using Base::real     ;
		using Base::file_loc ;
		using Base::accesses ;
		_Read() = default ;
		_Read( Record& r , Path&& path , bool no_follow , bool keep_real , ::string&& c ) : Base{r,::move(path),no_follow,true/*read*/,false/*create*/,c} {
			if ( ChkSimple && !real ) return ;
			r._report_dep( file_loc , keep_real?(::copy(real)):(::move(real)) , accesses|Access::Reg , ::move(c) ) ;
		}
	} ; //!             ChkSimple
	using Read   = _Read<false  > ;
	using ReadCS = _Read<true   > ;
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
		bool   emulated = false   ;                                        // if true <=> backdoor was used
	} ;
	struct Rename {
		// cxtors & casts
		Rename() = default ;
		Rename( Record& , Path&& src , Path&& dst , bool exchange , bool no_replace , ::string&& comment ) ;
		// services
		int operator()( Record& r , int rc ) {
			FileLoc fl = dst.file_loc ; if (exchange) fl &= src.file_loc ; // if exchange, we confirm if either src or dst is in repo
			r._report_confirm( fl , rc>=0 ) ;
			return rc ;
		}
		// data
		Solve src      ;
		Solve dst      ;
		bool  exchange ;
	} ;
	struct Stat : Solve {
		// cxtors & casts
		Stat() = default ;
		Stat( Record& , Path&& , bool no_follow , ::string&& comment ) ;
		// services
		/**/              void operator()( Record&           ) {                            }
		template<class T> T    operator()( Record& , T&& res ) { return ::forward<T>(res) ; }
	} ;
	struct Symlink : Solve {
		// cxtors & casts
		Symlink() = default ;
		Symlink( Record& r , Path&& p , ::string&& comment ) ;
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
	bool disabled   = false ;
private :
	Disk::RealPath _real_path ;
	mutable bool   _tmp_cache = false ;                                    // record that tmp usage has been reported, no need to report any further
} ;

template<bool Writable=false> ::ostream& operator<<( ::ostream& os , Record::_Path<Writable> const& p ) {
	const char* sep = "" ;
	/**/                       os << "Path("     ;
	if ( p.at!=Fd::Cwd     ) { os <<      p.at   ; sep = "," ; }
	if ( p.file && *p.file )   os <<sep<< p.file ;
	return                     os <<')'          ;
}
