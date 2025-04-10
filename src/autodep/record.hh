// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "disk.hh"
#include "gather.hh"
#include "rpc_job_exec.hh"
#include "time.hh"

ENUM( Sent
,	NotSent
,	Fast
,	Slow
,	Static
)

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
	static bool s_has_server() {
		return _s_autodep_env->has_server() ;
	}
	static Fd s_repo_root_fd() { return s_repo_root_fd(::getpid()) ; }
	static Fd s_repo_root_fd(pid_t pid) {
		if (!( +_s_repo_root_fd && _s_repo_root_pid==pid )) {
			_s_repo_root_fd  = _s_autodep_env->repo_root_fd() ;
			_s_repo_root_pid = pid                            ;
		}
		return _s_repo_root_fd ;
	}
	template<bool Fast> static Fd s_report_fd(         ) { return s_report_fd<Fast>(::getpid()) ; }
	template<bool Fast> static Fd s_report_fd(pid_t pid) {
		if (
			!( +_s_report_fd[Fast] && _s_report_pid[Fast]==pid )
		&&	+*_s_autodep_env
		) {
			_s_report_fd [Fast] = _s_autodep_env->report_fd<Fast>() ;
			_s_report_pid[Fast] = pid                               ;
		}
		return _s_report_fd[Fast] ;
	}
	static void s_close_reports() {
		_s_report_fd[0].close() ;
		_s_report_fd[1].close() ;
	}
	static AutodepEnv const& s_autodep_env() {
		SWEAR( +_s_autodep_env && +s_access_cache ) ;
		return *_s_autodep_env ;
	}
	static AutodepEnv const& s_autodep_env(AutodepEnv const& ade) {
		SWEAR( !s_access_cache && !_s_autodep_env ) ;
		_s_mk_autodep_env(new AutodepEnv{ade}) ;
		return *_s_autodep_env ;
	}
	static AutodepEnv const& s_autodep_env(NewType) {
		SWEAR( +s_access_cache == +_s_autodep_env ) ;
		if (!_s_autodep_env) _s_mk_autodep_env(new AutodepEnv{New}) ;
		return *_s_autodep_env ;
	}
	static void s_hide(int fd) {
		if (_s_repo_root_fd.fd==fd) _s_repo_root_fd  .detach() ;
		if (_s_report_fd[0].fd==fd) _s_report_fd[0].detach() ;
		if (_s_report_fd[1].fd==fd) _s_report_fd[1].detach() ;
	}
	static void s_hide( uint min , uint max ) {
		if ( _s_repo_root_fd.fd>=0 &&  uint(_s_repo_root_fd.fd)>=min && uint(_s_repo_root_fd.fd)<=max ) _s_repo_root_fd.detach() ;
		if ( _s_report_fd[0].fd>=0 &&  uint(_s_report_fd[0].fd)>=min && uint(_s_report_fd[0].fd)<=max ) _s_report_fd[0].detach() ;
		if ( _s_report_fd[1].fd>=0 &&  uint(_s_report_fd[1].fd)>=min && uint(_s_report_fd[1].fd)<=max ) _s_report_fd[1].detach() ;
	}
	// private
	static void _s_mk_autodep_env(AutodepEnv* ade) {
		_s_autodep_env = ade                                                       ;
		s_access_cache = new ::umap_s<pair<Accesses/*accessed*/,Accesses/*seen*/>> ;
	}
	// static data
public :
	static bool                                                                 s_static_report  ; // if true <=> report deps to s_deps instead of through s_report_fd() sockets
	static ::vmap_s<DepDigest>*                                                 s_deps           ;
	static ::string           *                                                 s_deps_err       ;
	static StaticUniqPtr<::umap_s<pair<Accesses/*accessed*/,Accesses/*seen*/>>> s_access_cache   ; // map file to read accesses
private :
	static StaticUniqPtr<AutodepEnv> _s_autodep_env           ;
	static Fd                        _s_repo_root_fd          ; // a file descriptor to repo root dir
	static pid_t                     _s_repo_root_pid         ; // pid in which _s_repo_root_fd is valid
	static Fd                        _s_report_fd [2/*Fast*/] ; // indexed by Fast, fast one is open to a pipe, faster than a socket, but short messages and local only
	static pid_t                     _s_report_pid[2/*Fast*/] ; // pid in which corresponding _s_report_fd is valid
	// cxtors & casts
public :
	Record(                                        ) = default ;
	Record( NewType ,                  pid_t pid   ) : Record( New , Maybe , pid ) {}
	Record( NewType , Bool3 en=Maybe , pid_t pid=0 ) : _real_path{s_autodep_env(New),pid} {
		if (en==Maybe) enable = s_autodep_env().enable ;
		else           enable = en==Yes                ;
	}
	// services
private :
	void _static_report(JobExecRpcReq&& jerr) const ;
	JobExecRpcReply _get_reply() const {
		if (s_static_report) return {}                                                               ;
		else                 return IMsgBuf().receive<JobExecRpcReply>(s_report_fd<false/*Fast*/>()) ;
	}
public :
	Sent            report_direct( JobExecRpcReq&& , bool force=false ) const ;                                                         // if force, emmit record even if recording is diabled
	Sent            report_cached( JobExecRpcReq&& , bool force=false ) const ;                                                         // .
	JobExecRpcReply report_sync  ( JobExecRpcReq&& , bool force=false ) const ;                                                         // .
	// for modifying accesses :
	// - if we report after  the access, it may be that job is interrupted inbetween and repo is modified without server being notified and we have a manual case
	// - if we report before the access, we may notify an access that will not occur if job is interrupted or if access is finally an error
	// so the choice is to manage Maybe :
	// - access is reported as Maybe before the access
	// - it is then confirmed (with an ok arg to manage errors) after the access
	// in job_exec, if an access is left Maybe, i.e. if job is interrupted between the Maybe reporting and the actual access, disk is interrogated to see if access did occur
	//
	::pair<Sent/*confirm*/,JobExecRpcReq::Id> report_access( FileLoc fl , JobExecRpcReq&& jerr , bool force=false ) const {
		using Jerr = JobExecRpcReq ;
		if (fl>FileLoc::Dep ) return { {}/*confirm*/ , 0 } ;
		if (fl>FileLoc::Repo) jerr.digest.write = No ;
		// auto-generated id must be different for all accesses (could be random)
		// if _real_path.pid is 0, ::this_thread::get_id() gives a good id, else we are ptracing, tid is mostly constant but dates are different
		static_assert(sizeof(::thread::id)<=sizeof(Jerr::Id)) ;                                                                         // else we have to think about it
		Time::Pdate now     ;
		Jerr::Id    id      = jerr.id                         ;
		bool        need_id = !id && jerr.digest.write==Maybe ;
		if      ( need_id && !_real_path.pid )               ::memcpy( &id , &ref(::this_thread::get_id()) , sizeof(::thread::id) ) ;
		if      ( need_id &&  _real_path.pid ) { now = New ; id = now.val() ;                                                         }
		else if ( !jerr.date                 )   now = New ;                                                                            // used for date
		//
		/**/                                            jerr.proc      = JobExecProc::Access          ;
		if ( !jerr.date                               ) jerr.date      = now                          ;
		if ( need_id                                  ) jerr.id        = id                           ;
		if ( !jerr.file_info && +jerr.digest.accesses ) jerr.file_info = {s_repo_root_fd(),jerr.file} ;
		//
		Sent sent = report_cached( ::move(jerr) , force ) ;
		if (jerr.digest.write==Maybe) return { sent/*confirm*/ , id      } ;
		else                          return { {}  /*confirm*/ , 0/*id*/ } ;
	}
	// if f0 is not empty, write is done to f0 rather than to jerr.file
	::pair<Sent/*confirm*/,JobExecRpcReq::Id> report_access( FileLoc fl , JobExecRpcReq&& jerr , FileLoc fl0 , ::string&& f0 , bool force=false ) const {
		if (+f0) {
			// read part
			JobExecRpcReq read_jerr = jerr ; read_jerr.digest.write = No ;
			report_access( fl , ::move(read_jerr) , force ) ;
			// write part
			fl        = fl0 ;
			jerr.file = f0  ;
		}
		return report_access( fl , ::move(jerr) , force ) ;
	}
	//
	#define FL FileLoc
	/**/         void report_guard(FL fl,::string&& f) const { if (fl<=FL::Repo)   report_direct({ .proc=Proc::Guard , .file=::move(f) }) ;                      }
	[[noreturn]] void report_panic(::string&& m      ) const {                     report_direct({ .proc=Proc::Panic , .file=::move(m) }) ; exit(Rc::Usage) ;    }
	/**/         void report_trace(::string&& m      ) const {                     report_direct({ .proc=Proc::Trace , .file=::move(m) }) ;                      }
	/**/         void report_tmp  (                  ) const { if (!_seen_tmp  ) { report_direct({ .proc=Proc::Tmp   , .date=New       }) ; _seen_tmp = true ; } }
	//
	void report_confirm( int rc , ::pair<Sent/*confirm*/,JobExecRpcReq::Id> const& confirm ) const {
		if (+confirm.first) {
			JobExecRpcReq jerr { .proc=Proc::Confirm , .digest={.write=No|(rc>=0)} , .id=confirm.second } ; if (confirm.first==Sent::Slow) jerr.sync = Maybe ;
			report_direct(::move(jerr)) ;
		}
	}
	#undef FL
	//
	template<bool Writable=false> struct _Path {                                                                    // if !Writable <=> file is is read-only
		using Char = ::conditional_t<Writable,char,const char> ;
		// cxtors & casts
		_Path(                          )                           {                       }
		_Path( Fd a                     ) :                   at{a} {                       }
		_Path(        Char*           f ) : file{f        }         {                       }
		_Path( Fd a , Char*           f ) : file{f        } , at{a} {                       }
		_Path(        ::string const& f ) : file{f.c_str()}         { _allocate(f.size()) ; }
		_Path( Fd a , ::string const& f ) : file{f.c_str()} , at{a} { _allocate(f.size()) ; }
		//
		_Path(_Path && p) { self = ::move(p) ; }
		_Path& operator=(_Path&& p) {
			_deallocate() ;
			at          = p.at        ;
			file        = p.file      ;
			allocated   = p.allocated ;
			p.file      = nullptr     ;                                                                             // safer to avoid dangling pointers
			p.allocated = false       ;                                                                             // we have clobbered allocation, so it is no more p's responsibility
			return self ;
		}
		~_Path() { _deallocate() ; }
		// accesses
		bool operator==(_Path const& p) const {
			return at==p.at && ::strcmp(file,p.file)==0 ;
		}
		// services
	private :
		void _deallocate() { if (allocated) delete[] file ; }
		//
		void _allocate(size_t sz) {
			char* buf = new char[sz+1] ;                                                                            // +1 to account for terminating null
			::memcpy( buf , file , sz+1 ) ;
			file      = buf  ;
			allocated = true ;
		}
		// data
	public :
		Char* file      = nullptr ;                                                                                 // at & file may be modified, but together, they always refer to the same file ...
		Fd    at        = Fd::Cwd ;                                                                                 // ... except in the case of mkstemp (& al.) that modifies its arg in place
		bool  allocated = false   ;                                                                                 // if true <=> file has been allocated and must be freed upon destruction
	} ; //!            Writable
	using Path  = _Path<false> ;
	using WPath = _Path<true > ;
	template<bool Writable=false,bool ChkSimple=false> struct _Solve : _Path<Writable> {
		using Base = _Path<Writable> ;
		using Base::at   ;
		using Base::file ;
		_Solve()= default ;
		_Solve( Record& r , Base&& path , bool no_follow , bool read , bool create , Comment c , CommentExts ces={} ) : Base{::move(path)} {
			using namespace Disk ;
			if (ChkSimple) { if ( s_is_simple(file) ) return ; }
			else           { if ( !file || !file[0] ) return ; }
			//
			SolveReport sr = r._real_path.solve(at,file,no_follow) ;
			//
			auto handle_dep = [&]( FileLoc fl , ::string&& file , Accesses a , bool store , CommentExt exts )->void {
				if ( ::vmap_s<::vector_s> const& views = s_autodep_env().views ; +views ) {
					Fd repo_root_fd = s_repo_root_fd() ;
					for( auto const& [view,phys] : s_autodep_env().views ) {
						if (!phys                                                                      ) continue ; // empty phys do not represent a view
						if (!( file.starts_with(view) && (is_dirname(view)||file.size()==view.size()) )) continue ;
						for( size_t i : iota(phys.size()) ) {
							bool     last  = i==phys.size()-1                                               ;
							::string f     = phys[i] + substr_view(file,view.size())                        ;
							FileInfo fi    = !last||+a ? FileInfo(repo_root_fd,f) : FileInfo(FileTag::None) ;
							bool     found = fi.exists() || !read                                           ;       // if not reading, assume file is found in upper
							fl = r._real_path.file_loc(f) ;                                                         // use f before ::move
							if (store) {
								if      (last ) { real  = f ; file_loc  = fl ; }
								else if (found) { real  = f ; file_loc  = fl ; }
								else if (i==0 ) { real0 = f ; file_loc0 = fl ; }                                    // real0 is only significative when not equal to real
							}
							if      (last ) { if (+a) r.report_access( fl , {.comment=c,.comment_exts=ces|exts,.digest={.accesses=a             } , .file=::move(f) , .file_info=fi } ) ; return ; }
							else if (found) {         r.report_access( fl , {.comment=c,.comment_exts=ces|exts,.digest={.accesses=a|Access::Stat} , .file=::move(f) , .file_info=fi } ) ; return ; }
							else                      r.report_access( fl , {.comment=c,.comment_exts=ces|exts,.digest={.accesses=  Access::Stat} , .file=::move(f) , .file_info=fi } ) ;
						}
						return ;
					}
				}
				// when no views match, process as if last
				if (store) {
					real     = +a ? ::copy(file) : ::move(file) ;
					file_loc = sr.file_loc                      ;
				}
				if (+a) r.report_access( fl , { .comment=c , .comment_exts=exts , .digest={.accesses=a} , .file=::move(file) } ) ;
			} ;
			//
			if (sr.file_accessed==Yes) accesses = Access::Lnk ;
			//                                                                                                                      accesses      store
			for( ::string& lnk : sr.lnks                               ) handle_dep( FileLoc::Dep , ::move(lnk)                   , Access::Lnk , false , CommentExt::Lnk  ) ;
			if ( !read  && sr.file_accessed==Maybe && has_dir(sr.real) ) handle_dep( sr.file_loc  , no_slash(dir_name_s(sr.real)) , Access::Lnk , false , CommentExt::Last ) ; // real dir is not ...
			/**/                                                         handle_dep( sr.file_loc  , ::move(sr.real)               , {}          , true  , CommentExt::File ) ; // ... protected by real
			//
			if ( create && sr.file_loc==FileLoc::Tmp ) r.report_tmp() ;
		}
		// services
		template<IsStream S> void serdes(S& s) {
			::serdes(s,real      ) ;
			::serdes(s,real0     ) ;
			::serdes(s,accesses  ) ;
			::serdes(s,file_loc  ) ;
			::serdes(s,file_loc0 ) ;
		}
		template<class T> T operator()( Record& , T rc ) { return rc ; }
		void report_dep( Record& r , Accesses a , Comment c , CommentExts ces={} , Time::Pdate date={} ) {
			r.report_access( file_loc , { .comment=c , .comment_exts=ces , .digest={.accesses=accesses|a} , .date{date} , .file=::move(real) } ) ;
		}
		//
		::string const& real_write() const { return real0 | real ; }
		::string      & real_write()       { return real0 | real ; }
		// data
		::string real      ;
		::string real0     ;                      // real in case reading and writing is to different files because of overlays
		Accesses accesses  ;                      // Access::Lnk if real was accessed as a sym link
		FileLoc  file_loc  = FileLoc::Unknown ;
		FileLoc  file_loc0 = FileLoc::Unknown ;
	} ; //!                Writable ChkSimple
	using Solve    = _Solve<false  ,false   > ;
	using WSolve   = _Solve<true   ,false   > ;
	using SolveCS  = _Solve<false  ,true    > ;
	using WSolveCS = _Solve<true   ,true    > ;
	struct SolveModify : Solve {
		// cxtors & casts
		using Solve::Solve ;
		// services
		template<IsStream S> void serdes(S& s) {
			/**/              Solve::serdes(s            ) ;
			/**/                   ::serdes(s,confirm_fds) ;
			if (+confirm_fds)      ::serdes(s,confirm_id ) ;
		}
		void report_update( Record& r , Accesses a , Comment c , CommentExts ces={} , Time::Pdate date={} ) {
			JobExecRpcReq                                jerr    { .comment=c , .comment_exts=ces , .digest={.write=Maybe,.accesses=accesses|a} , .id=confirm_id , .date{date} , .file=::move(real) } ;
			::pair<Sent/*confirm_fd*/,JobExecRpcReq::Id> confirm = r.report_access( file_loc , ::move(jerr) , file_loc0 , ::move(real0) )                                                             ;
			if (+confirm.first) {
				confirm_fds |= confirm.first  ;
				if (confirm_id)   SWEAR(confirm.second==confirm_id , confirm.second,confirm_id,jerr ) ;
				else            { SWEAR(confirm.second             ,                           jerr ) ; confirm_id = confirm.second ; }
			}
		}
		int operator()( Record& r , int rc ) {
			for( Sent s : iota(Sent(1),All<Sent>) ) if (confirm_fds[s]) r.report_confirm( rc , {s,confirm_id} ) ;
			return rc ;
		}
		// data
		BitMap<Sent>      confirm_fds ;           // with rename, which confirms several accesses, maybe they are not all on the same fd, hence we need a BitMap
		JobExecRpcReq::Id confirm_id  = 0 ;
	} ;
	struct Chdir : Solve {
		// cxtors & casts
		Chdir() = default ;
		Chdir( Record& , Path&& , Comment ) ;
		// services
		int operator()( Record& r , int rc ) {
			if (rc==0) r.chdir() ;
			return rc ;
		}
	} ;
	struct Chmod : SolveModify {
		// cxtors & casts
		Chmod() = default ;
		Chmod( Record& , Path&& , bool exe , bool no_follow , Comment ) ;
	} ;
	struct Hide {
		Hide( Record&          ) {              } // in case nothing to hide, just to ensure invariants
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
		Exec( Record& , Path&& , bool no_follow , Comment ) ;
	} ;
	struct Lnk {
		// cxtors & casts
		Lnk() = default ;
		Lnk( Record& , Path&& src , Path&& dst , bool no_follow , Comment ) ;
		// services
		int operator()( Record& r , int rc ) { return dst(r,rc) ; }
		// data
		Solve       src ;
		SolveModify dst ;
	} ;
	struct Mkdir : Solve {
		Mkdir() = default ;
		Mkdir( Record& , Path&& , Comment ) ;
	} ;
	struct Mount {
		Mount() = default ;
		Mount( Record& , Path&& src , Path&& dst , Comment ) ;
		int operator()( Record& , int rc ) { return rc ; }
		// data
		Solve src ;
		Solve dst ;
	} ;
	struct Open : SolveModify {
		// cxtors & casts
		Open() = default ;
		Open( Record& , Path&& , int flags , Comment ) ;
	} ;
	template<bool ChkSimple=false> struct _Read : _Solve<false/*Writable*/,ChkSimple> {
		using Base = _Solve<false/*Writable*/,ChkSimple> ;
		using Base::real     ;
		using Base::file_loc ;
		using Base::accesses ;
		_Read() = default ;
		_Read( Record& r , Path&& path , bool no_follow , bool keep_real , Comment c ) : Base{r,::move(path),no_follow,true/*read*/,false/*create*/,c} {
			if ( ChkSimple && !real ) return ;
			r.report_access( file_loc , { .comment=c , .digest={.accesses=accesses|Access::Reg} , .file=keep_real?(::copy(real)):(::move(real)) } ) ;
		}
	} ; //!             ChkSimple
	using Read   = _Read<false  > ;
	using ReadCS = _Read<true   > ;
	struct Readlink : Solve {
		// cxtors & casts
		Readlink() = default ;
		// buf and sz are only used when mapping tmp
		Readlink( Record& , Path&& , char* buf , size_t sz , Comment ) ;
		// services
		ssize_t operator()( Record& , ssize_t len=0 ) ;
		// data
		char*  buf      = nullptr ;
		size_t sz       = 0       ;
		bool   emulated = false   ;               // if true <=> backdoor was used
	} ;
	struct Rename {
		// cxtors & casts
		Rename() = default ;
		Rename( Record& , Path&& src , Path&& dst , bool exchange , bool no_replace , Comment ) ;
		// services
		int operator()( Record& r , int rc ) { return dst(r,rc) ; }
		// data
		SolveModify src ;                         // src may be modified in case of exchange
		SolveModify dst ;
	} ;
	struct Stat : Solve {
		// cxtors & casts
		Stat() = default ;
		Stat( Record& , Path&& , bool no_follow , Accesses , Comment ) ;
		// services
		/**/              void operator()( Record&           ) {                            }
		template<class T> T    operator()( Record& , T&& res ) { return ::forward<T>(res) ; }
	} ;
	struct Symlink : SolveModify {
		// cxtors & casts
		Symlink() = default ;
		Symlink( Record& r , Path&& p , Comment ) ;
	} ;
	struct Unlnk : SolveModify {
		// cxtors & casts
		Unlnk() = default ;
		Unlnk( Record& , Path&& , bool remove_dir , Comment ) ;
	} ;
	//
	void chdir() {
		seen_chdir = true ;
		_real_path.chdir() ;
	}
	// data
	bool seen_chdir = false ;
	bool enable     = false ;
private :
	Disk::RealPath _real_path ;
	mutable bool   _seen_tmp  = false ;           // record that tmp usage has been reported, no need to report any further
} ;

template<bool Writable=false> ::string& operator+=( ::string& os , Record::_Path<Writable> const& p ) {
	const char* sep = "" ;
	/**/                       os << "Path("     ;
	if ( p.at!=Fd::Cwd     ) { os <<      p.at   ; sep = "," ; }
	if ( p.file && *p.file )   os <<sep<< p.file ;
	return                     os <<')'          ;
}

template<bool Writable=false,bool ChkSimple=false> ::string& operator+=( ::string& os , Record::_Solve<Writable,ChkSimple> const& s ) {
	/**/            os << "Solve("<< s.real <<','<< s.file_loc <<','<< s.accesses ;
	if (+s.real0  ) os <<','<< s.real0 <<','<< s.file_loc0                        ;
	if (+s.confirm) os <<','<< s.confirm                                          ;
	return          os <<')'                                                      ;
}
