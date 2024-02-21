// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"
#include "hash.hh"
#include "rpc_client.hh"
#include "serialize.hh"

#ifdef STRUCT_DECL

ENUM( CacheTag // PER_CACHE : add a tag for each cache method
,	None
,	Dir
)

ENUM( Color
,	None
,	HiddenNote
,	HiddenOk
,	Note
,	Ok
,	Warning
,	SpeculateErr
,	Err
)

ENUM( ConfigDiff
,	None         // configs are identical
,	Dynamic      // config can be updated while engine runs
,	Static       // config can be updated when engine is steady
,	Clean        // config cannot be updated (requires clean repo)
)

ENUM( EngineClosureKind
,	Global
,	Req
,	Job
)

ENUM( GlobalProc
,	None
,	Int
,	Wakeup
)

ENUM( JobEvent
,	Submit     // job is submitted
,	Add        // add a Req monitoring job
,	Start      // job execution starts
,	Done       // job done successfully
,	Steady     // job done successfully
,	Rerun      // job must rerun
,	Killed     // job has been killed
,	Del        // delete a Req monitoring job
,	Err        // job done in error
)

ENUM( NodeEvent
,	Done        // node was modified
,	Steady      // node was remade w/o modification
,	Uphill      // uphill dir was makable
)

ENUM( ReportBool
,	No
,	Yes
,	Reported
)

ENUM( StdRsrc
,	Cpu
,	Mem
,	Tmp
)

namespace Engine {

	struct Config           ;
	struct EngineClosure    ;
	struct EngineClosureReq ;
	struct EngineClosureJob ;

}

#endif
#ifdef STRUCT_DEF

namespace Engine {

	struct Version {
		static const Version Db ;
		bool operator==(Version const&) const = default ;
		size_t major = 0 ;
		size_t minor = 0 ;
	} ;
	constexpr Version Version::Db = {1,0} ;

	// changing these values require restarting from a clean base
	struct ConfigClean {
		// services
		bool operator==(ConfigClean const&) const = default ;
		// data
		Version    db_version           ;                    // must always stay first so it is always understood, by default, db version does not match
		Algo       hash_algo            = Algo::Xxh        ;
		LnkSupport lnk_support          = LnkSupport::Full ;
		::string   user_local_admin_dir ;
		::string   key                  ;                    // random key to differentiate repo from other repos
	} ;

	// changing these can only be done when lmake is not running
	struct ConfigStatic {

		struct Cache {
			friend ::ostream& operator<<( ::ostream& , Backend const& ) ;
			using Tag = CacheTag ;
			// cxtors & casts
			Cache() = default ;
			Cache( Py::Dict const& py_map ) ;
			// services
			bool operator==(Cache const&) const = default ;
			template<IsStream T> void serdes(T& s) {
				::serdes(s,tag) ;
				::serdes(s,dct) ;
			}
			// data
			Caches::Tag tag ;
			::vmap_ss   dct ;
		} ;

		struct TraceConfig {
			bool operator==(TraceConfig const&) const = default ;
			size_t   sz       = 100<<20      ;
			Channels channels = DfltChannels ;
			JobIdx   n_jobs   = 1000         ;
		} ;

		// services
		bool operator==(ConfigStatic const&) const = default ;
		// data
		Time::Delay    heartbeat      ;
		Time::Delay    heartbeat_tick ;
		DepDepth       max_dep_depth  = 1000 ; static_assert(DepDepth(1000)==1000) ; // ensure default value can be represented
		Time::Delay    network_delay  ;
		size_t         path_max       = -1   ;                                       // if -1 <=> unlimited
		::string       rules_module   ;
		::string       srcs_module    ;
		TraceConfig    trace          ;
		::map_s<Cache> caches         ;
	} ;

	// changing these can be made dynamically (i.e. while lmake is running)
	struct ConfigDynamic {

		struct Backend {
			friend ::ostream& operator<<( ::ostream& , Backend const& ) ;
			using Tag = BackendTag ;
			// cxtors & casts
			Backend() = default ;
			Backend(Py::Dict const& py_map) ;
			// services
			bool operator==(Backend const&) const = default ;
			template<IsStream T> void serdes(T& s) {
				::serdes(s,ifce      ) ;
				::serdes(s,dct       ) ;
				::serdes(s,configured) ;
			}
			// data
			::string  ifce       ;
			::vmap_ss dct        ;
			bool      configured = false ;
		} ;

		struct Console {
			bool operator==(Console const&) const = default ;
			uint8_t date_prec     = -1    ;                   // -1 means no date at all in console output
			uint8_t host_len      = 0     ;                   //  0 means no host at all in console output
			bool    has_exec_time = false ;
		} ;

		// services
		bool operator==(ConfigDynamic const&) const = default ;
		bool   errs_overflow(size_t n) const { return n>max_err_lines ;                                       }
		size_t n_errs       (size_t n) const { if (errs_overflow(n)) return max_err_lines-1 ; else return n ; }
		// data
		size_t                                                                  max_err_lines         = 0     ; // unlimited
		bool                                                                    reliable_dirs         = false ; // if true => dirs coherence is enforced when files are modified
		::string                                                                user_remote_admin_dir ;
		::string                                                                user_remote_tmp_dir   ;
		Console                                                                 console               ;
		::map_s<size_t>                                                         static_n_tokenss      ;
		::map_s<size_t>                                                         dyn_n_tokenss         ;
		::array<uint8_t,N<StdRsrc>>                                             rsrc_digits           = {}    ; // precision of standard resources
		::array<Backend,N<BackendTag>>                                          backends              ;         // backend may refuse dynamic modification
		::array<::array<::array<uint8_t,3/*RGB*/>,2/*reverse_video*/>,N<Color>> colors                = {}    ;
	} ;

	struct Config : ConfigClean , ConfigStatic , ConfigDynamic {
		friend ::ostream& operator<<( ::ostream& , Config const& ) ;
		// cxtors & casts
		Config(                      ) : booted{false} {}    // if config comes from nowhere, it is not booted
		Config(Py::Dict const& py_map) ;
		// services
		template<IsStream S> void serdes(S& s) {
			::serdes(s,static_cast<ConfigClean  &>(*this)) ; // must always stay first field to ensure db_version is always understood
			::serdes(s,static_cast<ConfigStatic &>(*this)) ;
			::serdes(s,static_cast<ConfigDynamic&>(*this)) ;
			if (::is_base_of_v<::istream,S>) booted = true ; // is config comes from disk, it is booted
		}
		::string pretty_str() const ;
		void open(bool dynamic) ;
		ConfigDiff diff(Config const& other) {
			if (!(ConfigClean  ::operator==(other))) return ConfigDiff::Clean   ;
			if (!(ConfigStatic ::operator==(other))) return ConfigDiff::Static  ;
			if (!(ConfigDynamic::operator==(other))) return ConfigDiff::Dynamic ;
			else                                     return ConfigDiff::None    ;
		}
		// data (derived info not saved on disk)
		bool     booted           = false ;                  // a marker to distinguish clean repository
		::string local_admin_dir  ;
		::string remote_admin_dir ;
		::string remote_tmp_dir   ;
	} ;

	// sep is put before the last indent level, useful for porcelaine output
	#define ROC ReqOptions const
	#define SC  ::string   const
	/**/          void audit( Fd out , ::ostream& log , ROC&    , Color   , SC&   , bool as_is=false , DepDepth  =0 , char sep=0 ) ;
	static inline void audit( Fd out ,                  ROC& ro , Color c , SC& t , bool a    =false , DepDepth l=0 , char sep=0 ) { static OFakeStream fs ; audit(out,fs ,ro,c          ,t,a,l,sep) ; }
	static inline void audit( Fd out , ::ostream& log , ROC& ro ,           SC& t , bool a    =false , DepDepth l=0 , char sep=0 ) {                         audit(out,log,ro,Color::None,t,a,l,sep) ; }
	static inline void audit( Fd out ,                  ROC& ro ,           SC& t , bool a    =false , DepDepth l=0 , char sep=0 ) { static OFakeStream fs ; audit(out,fs ,ro,Color::None,t,a,l,sep) ; }
	#undef SC
	#undef ROC

	template<class... A> static inline ::string title    ( ReqOptions const& , A&&... ) ;
	/**/                 static inline ::string color_pfx( ReqOptions const& , Color  ) ;
	/**/                 static inline ::string color_sfx( ReqOptions const& , Color  ) ;

}

#endif
#ifdef DATA_DEF

namespace Engine {

	struct EngineClosureReq {
		friend ::ostream& operator<<( ::ostream& , EngineClosureReq const& ) ;
		// accesses
		bool as_job() const {
			if (options.flags[ReqFlag::Job]) { SWEAR(files.size()==1,files) ; return true  ; }
			else                             {                                return false ; }
		}
		// services
		::vector<Node> targets(::string const& startup_dir_s={}) const ; // startup_dir_s for error reporting only
		Job            job    (::string const& startup_dir_s={}) const ; // .
		// data
		ReqProc    proc    = ReqProc::None ;
		Req        req     = {}            ;                             // if proc==Close
		Fd         in_fd   = {}            ;
		Fd         out_fd  = {}            ;
		::vector_s files   = {}            ;
		ReqOptions options = {}            ;
	} ;

	struct EngineClosureJob {
		friend ::ostream& operator<<( ::ostream& , EngineClosureJob const& ) ;
		JobProc        proc           = JobProc::None ;
		JobExec        exec           = {}            ;
		bool           report         = false         ; // if proc == Start | GiveUp
		::vector<Node> report_unlinks = {}            ; // if proc == Start
		::string       txt            = {}            ; // if proc == Start | LiveOut
		Req            req            = {}            ; // if proc == GiveUp
		::vmap_ss      rsrcs          = {}            ; // if proc == End
		JobDigest      digest         = {}            ; // if proc == End
		::string       backend_msg    = {}            ; // if proc == End
		Fd             reply_fd       = {}            ; // if proc == ChkDeps
	} ;

	struct EngineClosure {
		friend ::ostream& operator<<( ::ostream& , EngineClosure const& ) ;
		//
		using Kind = EngineClosureKind ;
		using Req_ = EngineClosureReq  ;
		using Job_ = EngineClosureJob  ;
		//
		using GP  = GlobalProc  ;
		using RP  = ReqProc     ;
		using J   = Engine::Job ;
		using JP  = JobProc     ;
		using JD  = JobDigest   ;
		using JE  = JobExec     ;
		using K   = Kind        ;
		using R   = Engine::Req ;
		using RO  = ReqOptions  ;
		using VS  = ::vector_s  ;
		//
		// cxtors & casts
		EngineClosure(GlobalProc p) : kind{Kind::Global} , global_proc{p} {}
		//
		EngineClosure(RP p,Fd ifd,Fd ofd,VS const& fs,RO const& ro) : kind{K::Req},req{.proc=p,.in_fd=ifd,.out_fd=ofd,.files=fs,.options=ro} { SWEAR(p>=RP::HasArgs) ; }
		EngineClosure(RP p,Fd ifd,Fd ofd                          ) : kind{K::Req},req{.proc=p,.in_fd=ifd,.out_fd=ofd                      } { SWEAR(p==RP::Kill   ) ; }
		EngineClosure(RP p,R r                                    ) : kind{K::Req},req{.proc=p,.req=r                                      } { SWEAR(p==RP::Close  ) ; }
		//
		EngineClosure( JP p , JE&& je , bool r , ::vector<Node>&& rus={} , ::string&& t={} , ::string&& bem={} ) :
			kind { K::Job                                                                                                           }
		,	job  { .proc=p , .exec=::move(je) , .report=r , .report_unlinks=::move(rus) , .txt=::move(t) , .backend_msg=::move(bem) }
		{ SWEAR(p==JP::Start) ; }
		//
		EngineClosure( JP p , JE&& je , ::string&& t    ) : kind{K::Job} , job{.proc=p,.exec=::move(je),.txt=::move(t)     } { SWEAR( p==JP::LiveOut                      ) ; }
		EngineClosure( JP p , JE&& je , R rq , bool rpt ) : kind{K::Job} , job{.proc=p,.exec=::move(je),.report=rpt,.req=rq} { SWEAR( p==JP::GiveUp                       ) ; }
		EngineClosure( JP p , JE&& je                   ) : kind{K::Job} , job{.proc=p,.exec=::move(je)                    } { SWEAR( p==JP::GiveUp || p==JP::ReportStart ) ; }
		//
		EngineClosure( JP p , JE&& je , ::vmap_ss&& r , JD&& jd , ::string&& bem={}  ) :
			kind { K::Job                                                                                       }
		,	job  { .proc=p , .exec=::move(je) , .rsrcs=::move(r) , .digest=::move(jd) ,.backend_msg=::move(bem) }
		{ SWEAR(p==JP::End) ; }
		//
		EngineClosure( JP p , JE&& je , ::vmap_s<DepDigest>&& dds , Fd rfd ) : kind{K::Job} , job{.proc=p,.exec=::move(je),.digest={.deps{::move(dds)}},.reply_fd=rfd} {
			SWEAR( p==JP::DepInfos || p==JP::ChkDeps ) ;
		}
		//
		EngineClosure(EngineClosure&& ec) : kind(ec.kind) {
			switch (ec.kind) {
				case K::Global : new(&global_proc) GlobalProc{::move(ec.global_proc)} ; break ;
				case K::Req    : new(&req        ) Req_      {::move(ec.req        )} ; break ;
				case K::Job    : new(&job        ) Job_      {::move(ec.job        )} ; break ;
			DF}
		}
		~EngineClosure() {
			switch (kind) {
				case K::Global : global_proc.~GlobalProc() ; break ;
				case K::Req    : req        .~Req_      () ; break ;
				case K::Job    : job        .~Job_      () ; break ;
			DF}
		}
		EngineClosure& operator=(EngineClosure const&  ec) = delete ;
		EngineClosure& operator=(EngineClosure      && ec) = delete ;
		// data
		Kind kind = K::Global ;
		union {
			GlobalProc global_proc = GP::None ; // if kind==Global
			Req_       req         ;
			Job_       job         ;
		} ;
	} ;

	extern ThreadQueue<EngineClosure> g_engine_queue ;

}

#endif
#ifdef IMPL

namespace Engine {

	static inline ::string reason_str(JobReason const& reason) {
		::string res = reason.msg() ;
		if (reason.node) append_to_string( res ,' ', Disk::mk_file(Node(reason.node)->name()) ) ;
		return res ;
	}

	template<class... A> static inline ::string title( ReqOptions const& ro , A&&... args ) {
		if (ro.reverse_video==Maybe) return {} ;
		return to_string( "\x1b]0;" , ::forward<A>(args)... , '\a' ) ;
	}

	static inline ::string color_pfx( ReqOptions const& ro , Color color ) {
		if ( color==Color::None || ro.reverse_video==Maybe || ro.flags[ReqFlag::Porcelaine] ) return {} ;
		::array<uint8_t,3/*RGB*/> const& colors = g_config.colors[+color][ro.reverse_video==Yes] ;
		return to_string( "\x1b[38;2;" , int(colors[0/*R*/]) ,';', int(colors[1/*G*/]) ,';', int(colors[2/*B*/]) , 'm' ) ;
	}

	static inline ::string color_sfx(ReqOptions const& ro , Color color ) {
		if ( color==Color::None || ro.reverse_video==Maybe || ro.flags[ReqFlag::Porcelaine] ) return {} ;
		return "\x1b[0m" ;
	}

}

#endif
