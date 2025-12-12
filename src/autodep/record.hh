// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "disk.hh"
#include "gather.hh"
#include "real_path.hh"
#include "rpc_job_exec.hh"
#include "time.hh"

enum class Sent : uint8_t {
	NotSent
,	Fast
,	Slow
,	Static
} ;

struct Record {
	using Crc         = Hash::Crc                                         ;
	using Ddate       = Time::Ddate                                       ;
	using FileInfo    = Disk::FileInfo                                    ;
	using SolveReport = RealPath::SolveReport                             ;
	using Proc        = JobExecProc                                       ;
	using GetReplyCb  = ::function<JobExecRpcReply(                    )> ;
	using ReportCb    = ::function<void           (JobExecRpcReq const&)> ;
	//
	struct CacheEntry {
		struct Acc {
			Acc& operator|=(Acc a) {
				accesses |= a.accesses ;
				read_dir |= a.read_dir ;
				return self ;
			}
			Acc operator~() const {
				return { ~accesses , !read_dir } ;
			}
			bool operator>=(Acc a) const {
				return accesses>=a.accesses && read_dir>=a.read_dir ;
			}
			Accesses accesses ;
			bool     read_dir = false ;
		} ;
		CacheEntry operator~() const {
			return { ~accessed , ~seen , ~flags } ;
		}
		Acc        accessed ;
		Acc        seen     ;
		MatchFlags flags    ;
	} ;
	//
	// statics
	static bool s_is_simple( const char*          , bool empty_is_simple=true ) ;
	static bool s_is_simple( ::string const& path , bool empty_is_simple=true ) { return s_is_simple( path.c_str() , empty_is_simple ) ; }
	//
	static bool s_has_server  (         ) { return +*_s_autodep_env           ; }
	static Fd   s_repo_root_fd(         ) { return s_repo_root_fd(::getpid()) ; }
	static Fd   s_repo_root_fd(pid_t pid) {
		if (!( +_s_repo_root_fd && _s_repo_root_pid==pid )) {
			_s_repo_root_fd  = _s_autodep_env->repo_root_fd() ;
			_s_repo_root_pid = pid                            ;
		}
		return _s_repo_root_fd ;
	}
	static void s_close_reports() {
		if (_s_report_fd[0]==_s_report_fd[1])   _s_report_fd[0].close() ;                             // if both are identical we must only close one
		else                                  { _s_report_fd[0].close() ; _s_report_fd[1].close() ; }
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
		/**/                            if (_s_repo_root_fd.fd   ==fd) _s_repo_root_fd   .detach() ;
		for( bool fast : {false,true} ) if (_s_report_fd[fast].fd==fd) _s_report_fd[fast].detach() ;
	}
	static void s_hide( uint min , uint max ) {
		/**/                            if ( +_s_repo_root_fd    &&  uint(_s_repo_root_fd   .fd)>=min && uint(_s_repo_root_fd   .fd)<=max ) _s_repo_root_fd   .detach() ;
		for( bool fast : {false,true} ) if ( +_s_report_fd[fast] &&  uint(_s_report_fd[fast].fd)>=min && uint(_s_report_fd[fast].fd)<=max ) _s_report_fd[fast].detach() ;
	}
private :
	static void _s_mk_autodep_env(AutodepEnv* ade) {
		_s_autodep_env = ade                      ;
		s_access_cache = new ::umap_s<CacheEntry> ;
	}
	// static data
public :
	static bool                                s_static_report       ;              // if true <=> report deps to s_deps instead of through slow/fast_report_fd() sockets
	static bool                                s_enable_was_modified ;              // if true <=  the enable bit has been manipulated through the backdoor
	static ::vmap_s<DepDigest>*                s_deps                ;
	static ::string           *                s_deps_err            ;
	static StaticUniqPtr<::umap_s<CacheEntry>> s_access_cache        ;              // map file to read accesses
	static Mutex<MutexLvl::Record>             s_mutex               ;
private :
	static StaticUniqPtr<AutodepEnv> _s_autodep_env           ;
	static Fd                        _s_repo_root_fd          ;                     // a file descriptor to repo root dir
	static pid_t                     _s_repo_root_pid         ;                     // pid in which _s_repo_root_fd is valid
	static SockFd::Key               _s_report_key[2/*fast*/] ;                     // if not 0, key to send before first message, only useful for slow
	static Fd                        _s_report_fd [2/*fast*/] ;                     // indexed by Fast, fast one is open to a pipe, faster than a socket, but short messages and local only
	static pid_t                     _s_report_pid[2/*fast*/] ;                     // pid in which corresponding _s_report_fd is valid
	// cxtors & casts
public :
	Record( NewType ,                  pid_t pid   ) : Record( New , Maybe , pid ) {}
	Record( NewType , Bool3 en=Maybe , pid_t pid=0 ) : _real_path{s_autodep_env(New),pid} {
		if (en==Maybe) enable = s_autodep_env().enable ;
		else           enable = en==Yes                ;
	}
	// services
	Fd report_fd( bool fast , pid_t pid=::getpid()) {
		if ( +*_s_autodep_env && !( +_s_report_fd[fast] && _s_report_pid[fast]==pid ) ) {
			if (fast) { AcFd         fd = _s_autodep_env->fast_report_fd() ; _s_report_fd[fast] = fd.detach() ;                                }
			else      { ClientSockFd fd = _s_autodep_env->slow_report_fd() ; _s_report_fd[fast] = fd.detach() ; _s_report_key[fast] = fd.key ; }
			_s_report_pid[fast] = pid ;
		}
		return _s_report_fd[fast] ;
	}
private :
	void            _static_report (JobExecRpcReq&& jerr) const ;
	Sent            _do_send_report(pid_t               )       ;
	JobExecRpcReply _get_reply     (                    )       {
		if (s_static_report) return {}                                                                                       ;
		else                 return IMsgBuf().receive<JobExecRpcReply>( report_fd(false/*fast*/) , Yes/*once*/ , {}/*key*/ ) ;
	}
public :
	Sent send_report() {                                                            // XXX/ : reports must be bufferized as doing several back-to-back writes may incur severe perf penalty (seen ~40ms)
		if (                        s_static_report ) return Sent::Static         ;
		if (                        !_buf           ) return Sent::NotSent        ;
		if ( pid_t pid=::getpid() ;  _buf_pid!=pid  ) return Sent::NotSent        ;
		else                                          return _do_send_report(pid) ;
	}
	void              report_direct(           JobExecRpcReq&& ,                               bool force=false ) ; // if force, report even if disabled
	void              report_cached(           JobExecRpcReq&& ,                               bool force=false ) ; // .
	JobExecRpcReq::Id report_access(           JobExecRpcReq&& ,                               bool force=false ) ; // .
	JobExecRpcReq::Id report_access( FileLoc , JobExecRpcReq&& ,                               bool force=false ) ; // .
	JobExecRpcReq::Id report_access( FileLoc , JobExecRpcReq&& , FileLoc fl0 , ::string&& f0 , bool force=false ) ; // .
	JobExecRpcReply   report_sync  (           JobExecRpcReq&&                                                  ) ; // always force
	//
	void report_guard( ::vmap_s<FileInfo>&& fs      ) {                          report_direct({ .proc=Proc::Guard , .date=New , .files=::move(fs)       }) ;                             }
	void report_guard( FileLoc fl , ::string&& f    ) { if (fl<=FileLoc::Repo)   report_guard ({ {::move(f),{}}                                          }) ;                             }
	void report_panic( ::string&& m , bool die=true ) {                          report_sync  ({ .proc=Proc::Panic , .date=New , .files={{::move(m),{}}} }) ; if (die) exit(Rc::System) ; }
	void report_trace( ::string&& m                 ) {                          report_sync  ({ .proc=Proc::Trace , .date=New , .files={{::move(m),{}}} }) ;                             }
	void report_tmp  (                              ) { if (!_seen_tmp       ) { report_direct({ .proc=Proc::Tmp   , .date=New                           }) ; _seen_tmp = true ; }        }
	//
	void report_confirm( int rc , Sent fd , JobExecRpcReq::Id const& id ) {
		if ( +fd && +id ) report_direct({ .proc=Proc::Confirm , .sync=fd==Sent::Slow?Maybe:No , .digest={.write=No|(rc>=0)} , .id=id }) ;
	}
	//
	template<bool Writable=false> struct _Path {                                                                    // if !Writable <=> file is is read-only
		using Char = ::conditional_t<Writable,char,const char> ;
		// cxtors & casts
		_Path(                           )                           {                       }
		_Path( Fd  a                     ) :                   at{a} {                       }
		_Path( int a                     ) :                   at{a} {                       }                      // avoid confusion with Char*
		_Path(         Char*           f ) : file{f        }         {                       }
		_Path( Fd  a , Char*           f ) : file{f        } , at{a} {                       }
		_Path(         ::string const& f ) : file{f.c_str()}         { _allocate(f.size()) ; }
		_Path( Fd  a , ::string const& f ) : file{f.c_str()} , at{a} { _allocate(f.size()) ; }
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
	using Path  = _Path<false > ;
	using WPath = _Path<true  > ;
	template<bool Send=false,bool Writable=false,bool ChkSimple=false> struct Solve : _Path<Writable> {
		using Base = _Path<Writable> ;
		using Base::at   ;
		using Base::file ;
		static const size_t MaxSz ;
		// cxtors & casts
		Solve()= default ;
		Solve( Record& r , Base&& path , bool no_follow , bool read , bool create , Comment c , CommentExts ces={} ) : Base{::move(path)} {
			using namespace Disk ;
			if ( ChkSimple && s_is_simple(file) ) return ;
			//
			SolveReport sr = r._real_path.solve( { at , file?(::string_view(file)):(::string_view()) } , no_follow ) ;
			//
			auto handle_dep = [&]( FileLoc fl , ::string&& file_ , Accesses a , bool store , CommentExt exts ) {
				if ( ::vmap_s<::vector_s> const& views_s = s_autodep_env().views_s ; +views_s ) {
					Fd repo_root_fd = s_repo_root_fd() ;
					for( auto const& [view_s,phys_s] : s_autodep_env().views_s ) {
						if (!phys_s                   ) continue ;                                                  // empty phys do not represent a view
						if (!lies_within(file_,view_s)) continue ;
						for( size_t i : iota(phys_s.size()) ) {
							bool     last  = i==phys_s.size()-1                                               ;
							::string f     = phys_s[i] + substr_view(file_,view_s.size())                     ;
							FileInfo fi    = !last||+a ? FileInfo({repo_root_fd,f}) : FileInfo(FileTag::None) ;
							bool     found = fi.exists() || !read                                             ;     // if not reading, assume file_ is found in upper
							fl = r._real_path.file_loc(f) ;                                                         // use f before ::move
							if (store) {
								if      (last ) { real  = f ; file_loc  = fl ; }
								else if (found) { real  = f ; file_loc  = fl ; }
								else if (i==0 ) { real0 = f ; file_loc0 = fl ; }                                    // real0 is only significative when not equal to real
							}
							if      (last ) { if (+a) r.report_access( fl , {.comment=c,.comment_exts=ces|exts,.digest={.accesses=a             } , .files={{::move(f),fi}} } ) ; return ; }
							else if (found) {         r.report_access( fl , {.comment=c,.comment_exts=ces|exts,.digest={.accesses=a|Access::Stat} , .files={{::move(f),fi}} } ) ; return ; }
							else                      r.report_access( fl , {.comment=c,.comment_exts=ces|exts,.digest={.accesses=  Access::Stat} , .files={{::move(f),fi}} } ) ;
						}
						return ;
					}
				}
				// when no views match, process as if last
				if (store) {
					real     = +a ? ::copy(file_) : ::move(file_) ;
					file_loc = sr.file_loc                        ;
				}
				if (+a) r.report_access( fl , { .comment=c , .comment_exts=exts , .digest={.accesses=a} , .files={{::move(file_),{}}} } ) ;
			} ;
			//
			if (sr.file_accessed==Yes) accesses = Access::Lnk ;
			//                                                                                                                                   store
			for( ::string& lnk : sr.lnks )                              handle_dep( FileLoc::Dep , ::move(lnk)                   , Access::Lnk , false , CommentExt::Lnk  ) ;
			if ( !read && sr.file_accessed==Maybe && has_dir(sr.real) ) handle_dep( sr.file_loc  , no_slash(dir_name_s(sr.real)) , Access::Lnk , false , CommentExt::Last ) ; // real dir is not ...
			/**/                                                        handle_dep( sr.file_loc  , ::move(sr.real)               , {}          , true  , CommentExt::File ) ; // ... protected by real
			//
			if ( create && sr.file_loc==FileLoc::Tmp ) r.report_tmp () ;
			if ( Send                                ) r.send_report() ;
		}
		// services
		template<IsStream S> void serdes(S& s) {
			::serdes( s , real    ,real0     ) ;
			::serdes( s , accesses           ) ;
			::serdes( s , file_loc,file_loc0 ) ;
		}
		template<class T> T operator()( Record& , T rc ) { return rc ; }
		void report_dep( Record& r , Accesses a , Comment c , CommentExts ces={} , Time::Pdate date={} ) {
			r.report_access( file_loc , { .comment=c , .comment_exts=ces , .digest={.accesses=accesses|a} , .date{date} , .files={{::move(real),{}}} } ) ;
		}
		void send_report(Record& r) { r.send_report() ; }
		//
		::string const& real_write() const { return real0 | real ; }
		::string      & real_write()       { return real0 | real ; }
		// data
		::string real      ;
		::string real0     ;                                                                // real in case reading and writing is to different files because of overlays
		Accesses accesses  ;                                                                // Access::Lnk if real was accessed as a sym link
		FileLoc  file_loc  = FileLoc::Unknown ;
		FileLoc  file_loc0 = FileLoc::Unknown ;
	} ;
	using Mkstemp = Solve<true/*Send*/,true/*Writable*/> ;
	struct SolveModify : Solve<> {
		// cxtors & casts
		using Solve<>::Solve ;
		// services
		template<IsStream S> void serdes(S& s) {
			/**/             Solve<>::serdes(s           ) ;
			/**/                    ::serdes(s,confirm_fd) ;
			if (+confirm_fd)        ::serdes(s,confirm_id) ;
		}
		void report_update( Record& r , Accesses a , Comment c , CommentExts ces={} , Time::Pdate date={} ) {
			JobExecRpcReq     jerr { .comment=c , .comment_exts=ces , .digest={.write=Maybe,.accesses=accesses|a} , .id=confirm_id , .date{date} , .files={{::move(real),{}}} } ;
			JobExecRpcReq::Id id   = r.report_access( file_loc , ::move(jerr) , file_loc0 , ::move(real0) )                                                                     ;
			if (id) {
				if (confirm_id) SWEAR( id==confirm_id , id,confirm_id ) ;
				else            confirm_id = id ;
			}
		}
		void send_report(Record& r) { confirm_fd = r.send_report() ; }
		int operator()( Record& r , int rc , bool send=true ) {
			r.report_confirm( rc , confirm_fd , confirm_id ) ;
			if (send) r.send_report() ;
			return rc ;
		}
		// data
		Sent              confirm_fd = {} ;
		JobExecRpcReq::Id confirm_id = 0  ;
	} ;
	struct Chdir : Solve<> {
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
	struct Chroot : Solve<> {
		Chroot() = default ;
		Chroot( Record& , Path&& , Comment ) ;
	} ;
	struct Hide {
		Hide( Record&          ) {              }                                           // in case nothing to hide, just to ensure invariants
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
	template<bool Send,bool ChkSimple> struct Exec : Solve<false/*Send*/,false/*Writable*/,ChkSimple> {
		using Base = Solve<false/*Send*/,false/*Writable*/,ChkSimple> ;
		using Base::file_loc    ;
		using Base::real        ;
		using Base::send_report ;
		// cxtors & casts
		Exec() = default ;
		Exec( Record& r , Path&& path , bool no_follow , Comment c ) : Base{r,::move(path),no_follow,true/*read*/,false/*create*/,c} {
			if ( ChkSimple && !real ) return ;
			SolveReport sr {.real=real,.file_loc=file_loc} ;
			try {
				for( auto&& [file,a] : r._real_path.exec(::move(sr)) )
					r.report_access( FileLoc::Dep , { .comment=c , .digest={.accesses=a} , .files={{::move(file),{}}} } ) ;
			} catch (::string& e) { r.report_panic(::move(e)) ; }
			if (Send) send_report(r) ;
		}
	} ;
	struct Glob {
		Glob() = default ;
		Glob( Record& , const char* pat , int flags , Comment ) ;
		int operator()( Record& , int rc ) { return rc ; }
	} ;
	struct Lnk {
		// cxtors & casts
		Lnk() = default ;
		Lnk( Record& , Path&& src , Path&& dst , bool no_follow , Comment ) ;
		// services
		int operator()( Record& r , int rc ) { return dst( r , rc , true/*send*/ ) ; }
		// data
		Solve<>     src ;
		SolveModify dst ;
	} ;
	struct Mkdir : Solve<> {
		Mkdir() = default ;
		Mkdir( Record& , Path&& , Comment ) ;
	} ;
	struct Mount {
		Mount() = default ;
		Mount( Record& , Path&& src , Path&& dst , Comment ) ;
		int operator()( Record& , int rc ) { return rc ; }
		// data
		Solve<> src ;
		Solve<> dst ;
	} ;
	struct Open : SolveModify {
		// cxtors & casts
		Open() = default ;
		Open( Record& , Path&& , int flags , Comment ) ;
	} ;
	template<bool Send,bool ChkSimple=false> struct Read : Solve<false/*Send*/,false/*Writable*/,ChkSimple> {
		using Base = Solve<false/*Send*/,false/*Writable*/,ChkSimple> ;
		using Base::real        ;
		using Base::file_loc    ;
		using Base::accesses    ;
		using Base::send_report ;
		Read() = default ;
		Read( Record& r , Path&& path , bool no_follow , bool keep_real , Comment c , CommentExts ces={} ) : Base{r,::move(path),no_follow,true/*read*/,false/*create*/,c,ces} {
			if ( ChkSimple && !real ) return ;
			r.report_access( file_loc , { .comment=c , .comment_exts=ces , .digest={.accesses=accesses|Access::Reg} , .files={{keep_real?(::copy(real)):(::move(real)),{}}} } ) ;
			if (Send) send_report(r) ;
		}
	} ;
	struct ReadDir : Solve<true/*Send*/> {
		using Base = Solve<true/*Send*/> ;
		using Base::real     ;
		using Base::file_loc ;
		// cxtors & casts
		ReadDir() = default ;
		ReadDir( Record& r , Path&& path , Comment c ) : Base{r,::move(path),false/*no_follow*/,false/*read*/,false/*create*/,c} , comment{c} {}
		// services
		template<class T> T operator()( Record& r , T rc ) {
			bool ok ;
			static_assert( ::is_integral_v<T> || ::is_pointer_v <T> , "unexpected type" ) ; // cannot put an else clause with static_assert(false) with gcc-11
			if      constexpr (::is_integral_v<T>) ok = rc<0 ;
			else if constexpr (::is_pointer_v <T>) ok = rc   ;
			//
			if ( ok && !s_autodep_env().readdir_ok ) {
				if (file_loc==FileLoc::RepoRoot) r.report_access( FileLoc::Repo , { .comment=comment , .digest={.read_dir=true} , .files={{"."         ,{}}} } ) ; // repo root must be analyzed ...
				else                             r.report_access( file_loc      , { .comment=comment , .digest={.read_dir=true} , .files={{::move(real),{}}} } ) ; // ... whenreading it
			}
			send_report(r) ;
			//
			return rc ;
		}
		// data
		Comment comment = {} ;
	} ;
	struct Readlink : Solve<> {
		// cxtors & casts
		Readlink() = default ;
		// buf and sz are only used when mapping tmp
		Readlink( Record& , Path&& , char* buf , size_t sz , Comment ) ;
		// services
		ssize_t operator() ( Record& , ssize_t len=0 ) ;
		// data
		char*  buf   = nullptr ;
		size_t sz    = 0       ;
		bool   magic = false   ;            // if true <=> backdoor was used
	} ;
	struct Rename {
		// cxtors & casts
		Rename() = default ;
		Rename( Record& , Path&& src , Path&& dst , bool exchange , bool no_replace , Comment ) ;
		// services
		int operator()( Record& r , int rc ) {
			r.report_confirm( rc , confirm_fd , confirm_id ) ;
			r.send_report() ;
			return rc ;
		}
		// data
		Solve<>           src        ;      // modifications are managed independently of SolveModiy
		Solve<>           dst        ;      //.
		Sent              confirm_fd = {} ;
		JobExecRpcReq::Id confirm_id = 0  ;
	} ;
	struct Stat : Solve<> {
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
	RealPath     _real_path ;
	mutable bool _seen_tmp  = false ;       // record that tmp usage has been reported, no need to report any further
	OMsgBuf      _buf       ;               // buffer that accumulate messages to send
	::pid_t      _buf_pid   = 0     ;       // valid when +_buf, pid for which _buf is valid (ignore buf is wrong pid)
	Bool3        _is_slow   = No    ;       // valid when +_buf, if Yes => must be sent over slow connection, if Maybe => used connection must be known
} ;

template<bool Send,bool Writable,bool ChkSimple> constexpr size_t Record::Solve<Send,Writable,ChkSimple>::MaxSz = 2*PATH_MAX+sizeof(Solve<Send,Writable,ChkSimple>) ;

template<bool Writable=false> ::string& operator+=( ::string& os , Record::_Path<Writable> const& p ) { // START_OF_NO_COV
	const char* sep = "" ;
	/**/                       os << "Path("     ;
	if ( p.at!=Fd::Cwd     ) { os <<      p.at   ; sep = "," ; }
	if ( p.file && *p.file )   os <<sep<< p.file ;
	return                     os <<')'          ;
}                                                                                                       // END_OF_NO_COV

template<bool Send,bool Writable=false,bool ChkSimple=false> ::string& operator+=( ::string& os , Record::Solve<Send,Writable,ChkSimple> const& s ) { // START_OF_NO_COV
	/**/          os << "Solve("<< s.real <<','<< s.file_loc <<','<< s.accesses ;
	if (+s.real0) os <<','<< s.real0 <<','<< s.file_loc0                        ;
	return        os <<')'                                                      ;
}                                                                                                                                                     // END_OF_NO_COV
