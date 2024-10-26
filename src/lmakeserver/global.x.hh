// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "serialize.hh"

#include "rpc_client.hh"

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
,	JobMngt
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
		Version    db_version             ;                    // must always stay first so it is always understood, by default, db version does not match
		LnkSupport lnk_support            = LnkSupport::Full ;
		::string   user_local_admin_dir_s ;
		::string   key                    ;                    // random key to differentiate repo from other repos
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
		Time::Delay    date_prec       ;                                              // precision of dates on disk
		Time::Delay    heartbeat       ;                                              // min time between successive heartbeat probes for any given job
		Time::Delay    heartbeat_tick  ;                                              // min time between successive heartbeat probes
		DepDepth       max_dep_depth   = 1000 ; static_assert(DepDepth(1000)==1000) ; // ensure default value can be represented
		Time::Delay    network_delay   ;
		size_t         path_max        = -1   ;                                       // if -1 <=> unlimited
		bool           has_split_rules ;                                              // if true <=> read independently of config
		bool           has_split_srcs  ;                                              // .
		TraceConfig    trace           ;
		::map_s<Cache> caches          ;
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
			bool    show_eta      = false ;
		} ;

		// services
		bool operator==(ConfigDynamic const&) const = default ;
		bool   errs_overflow(size_t n) const { return n>max_err_lines ;                                       }
		size_t n_errs       (size_t n) const { if (errs_overflow(n)) return max_err_lines-1 ; else return n ; }
		// data
		size_t                                                                  max_err_lines = 0     ; // unlimited
		bool                                                                    reliable_dirs = false ; // if true => dirs coherence is enforced when files are modified
		Console                                                                 console       ;
		::array<uint8_t,N<StdRsrc>>                                             rsrc_digits   = {}    ; // precision of standard resources
		::array<Backend,N<BackendTag>>                                          backends      ;         // backend may refuse dynamic modification
		::array<::array<::array<uint8_t,3/*RGB*/>,2/*reverse_video*/>,N<Color>> colors        = {}    ;
		::umap_ss                                                               dbg_tab       = {}    ; // maps debug keys to modules to import
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
		bool     booted            = false ;                 // a marker to distinguish clean repository
		::string local_admin_dir_s ;
	} ;

	// sep is put before the last indent level, useful for porcelaine output
	/**/   void _audit( Fd out , ::ostream* log , ReqOptions const&    , Color   , ::string const&   , bool as_is   , DepDepth     , char sep   ) ;
	inline void audit ( Fd out , ::ostream& log , ReqOptions const& ro , Color c , ::string const& t , bool a=false , DepDepth l=0 , char sep=0 ) { _audit(out,&log   ,ro,c          ,t,a,l,sep) ; }
	inline void audit ( Fd out ,                  ReqOptions const& ro , Color c , ::string const& t , bool a=false , DepDepth l=0 , char sep=0 ) { _audit(out,nullptr,ro,c          ,t,a,l,sep) ; }
	inline void audit ( Fd out , ::ostream& log , ReqOptions const& ro ,           ::string const& t , bool a=false , DepDepth l=0 , char sep=0 ) { _audit(out,&log   ,ro,Color::None,t,a,l,sep) ; }
	inline void audit ( Fd out ,                  ReqOptions const& ro ,           ::string const& t , bool a=false , DepDepth l=0 , char sep=0 ) { _audit(out,nullptr,ro,Color::None,t,a,l,sep) ; }
	//
	/**/   void audit_file( Fd out , ::string&& f ) ;
	//
	/**/   void _audit_status( Fd out , ::ostream* log , ReqOptions const&    , bool    ) ;
	inline void audit_status ( Fd out , ::ostream& log , ReqOptions const& ro , bool ok ) { _audit_status(out,&log   ,ro,ok) ; }
	inline void audit_status ( Fd out ,                  ReqOptions const& ro , bool ok ) { _audit_status(out,nullptr,ro,ok) ; }
	//
	/**/   void _audit_ctrl_c( Fd out , ::ostream* log , ReqOptions const&    ) ;
	inline void audit_ctrl_c ( Fd out , ::ostream& log , ReqOptions const& ro ) { _audit_ctrl_c(out,&log   ,ro) ; }
	inline void audit_ctrl_c ( Fd out ,                  ReqOptions const& ro ) { _audit_ctrl_c(out,nullptr,ro) ; }

	inline ::string title    ( ReqOptions const& , ::string const& ) ;
	inline ::string color_pfx( ReqOptions const& , Color           ) ;
	inline ::string color_sfx( ReqOptions const& , Color           ) ;

}

#endif
#ifdef DATA_DEF

namespace Engine {

	struct EngineClosureGlobal {
		GlobalProc proc = {} ;
	} ;

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
		Req        req     = {}            ;                             // if proc==Close | Kill | Make
		Fd         in_fd   = {}            ;                             // if proc!=Close
		Fd         out_fd  = {}            ;                             // .
		::vector_s files   = {}            ;                             // if proc>=HasHargs
		ReqOptions options = {}            ;                             // .
	} ;

	struct EngineClosureJobStart {
		friend ::ostream& operator<<( ::ostream& , EngineClosureJobStart const& ) ;
		JobInfoStart               start         = {}    ;
		bool                       report        = false ;
		::vmap<Node,FileActionTag> report_unlnks = {}    ;
		::string                   txt           = {}    ;
		::string                   msg           = {}    ;
	} ;

	struct EngineClosureJobEtc {
		friend ::ostream& operator<<( ::ostream& , EngineClosureJobEtc const& ) ;
		bool report = false ;
		Req  req    = {}    ;
	} ;

	struct EngineClosureJobEnd {
		friend ::ostream& operator<<( ::ostream& , EngineClosureJobEnd const& ) ;
		::vmap_ss  rsrcs = {} ;
		JobInfoEnd end   = {} ;
	} ;

	struct EngineClosureJob {
		friend ::ostream& operator<<( ::ostream& , EngineClosureJob const& ) ;
		// cxtors & casts
		EngineClosureJob( JobRpcProc p , JobExec const& je , EngineClosureJobStart&& ecjs ) : proc{p} , job_exec{je} , start{ecjs} {}
		EngineClosureJob( JobRpcProc p , JobExec const& je , EngineClosureJobEtc  && ecje ) : proc{p} , job_exec{je} , etc  {ecje} {}
		EngineClosureJob( JobRpcProc p , JobExec const& je , EngineClosureJobEnd  && ecje ) : proc{p} , job_exec{je} , end  {ecje} {}
		//
		EngineClosureJob(EngineClosureJob&& ecj) : proc{ecj.proc} , job_exec{::move(ecj.job_exec)} {
			switch (ecj.proc) {
				case JobRpcProc::Start       : new(&start) EngineClosureJobStart{::move(ecj.start)} ; break ;
				case JobRpcProc::ReportStart :
				case JobRpcProc::GiveUp      : new(&etc  ) EngineClosureJobEtc  {::move(ecj.etc  )} ; break ;
				case JobRpcProc::End         : new(&end  ) EngineClosureJobEnd  {::move(ecj.end  )} ; break ;
			DF}
		}
		~EngineClosureJob() {
			switch (proc) {
				case JobRpcProc::Start       : start.~EngineClosureJobStart() ; break ;
				case JobRpcProc::ReportStart :
				case JobRpcProc::GiveUp      : etc  .~EngineClosureJobEtc  () ; break ;
				case JobRpcProc::End         : end  .~EngineClosureJobEnd  () ; break ;
			DF}
		}
		EngineClosureJob& operator=(EngineClosureJob const&) = delete ;
		EngineClosureJob& operator=(EngineClosureJob     &&) = delete ;
		// data
		JobRpcProc proc     = {} ;
		JobExec    job_exec = {} ;
		union {
			EngineClosureJobStart start ;
			EngineClosureJobEtc   etc   ;
			EngineClosureJobEnd   end   ;
		} ;
	} ;

	struct EngineClosureJobMngt {
		friend ::ostream& operator<<( ::ostream& , EngineClosureJobMngt const& ) ;
		JobMngtProc         proc     = {} ;
		JobExec             job_exec = {} ;
		Fd                  fd       = {} ;
		::vmap_s<DepDigest> deps     = {} ; // proc==ChkDeps|DepsInfo
		::string            txt      = {} ; // proc==LiveOut
	} ;

	struct EngineClosure {
		friend ::ostream& operator<<( ::ostream& , EngineClosure const& ) ;
		//
		using Kind = EngineClosureKind     ;
		using ECG  = EngineClosureGlobal   ;
		using ECR  = EngineClosureReq      ;
		using ECJ  = EngineClosureJob      ;
		using ECJM = EngineClosureJobMngt  ;
		//
		using GP  = GlobalProc  ;
		using RP  = ReqProc     ;
		using JRP = JobRpcProc  ;
		using JMP = JobMngtProc ;
		using J   = Engine::Job ;
		using JD  = JobDigest   ;
		using JE  = JobExec     ;
		using K   = Kind        ;
		using R   = Engine::Req ;
		using RO  = ReqOptions  ;
		using VS  = ::vector_s  ;
		//
		// cxtors & casts
		// Global
		EngineClosure(GP p) : kind{Kind::Global} , ecg{.proc=p} {}
		// Req
		EngineClosure(RP p,R r,Fd ifd,Fd ofd,VS const& fs,RO const& ro) : kind{K::Req},ecr{.proc=p,.req=r,.in_fd=ifd,.out_fd=ofd,.files=fs,.options=ro} { SWEAR(p==RP::Make                ) ; }
		EngineClosure(RP p,    Fd ifd,Fd ofd,VS const& fs,RO const& ro) : kind{K::Req},ecr{.proc=p,       .in_fd=ifd,.out_fd=ofd,.files=fs,.options=ro} { SWEAR(p!=RP::Make&&p>=RP::HasArgs) ; }
		EngineClosure(RP p,R r,Fd ifd,Fd ofd                          ) : kind{K::Req},ecr{.proc=p,.req=r,.in_fd=ifd,.out_fd=ofd                      } { SWEAR(p==RP::Kill                ) ; }
		EngineClosure(RP p,R r                                        ) : kind{K::Req},ecr{.proc=p,.req=r                                             } { SWEAR(p==RP::Close               ) ; }
		// Job
		EngineClosure( JRP p , JE&& je , JobInfoStart&& jis , bool r , ::vmap<Node,FileActionTag>&& rus={} , ::string&& t={} , ::string&& m={} ) :
			kind { K::Job                                                                                                       }
		,	ecj  { p , ::move(je) , EngineClosureJobStart{.start=jis,.report=r,.report_unlnks=::move(rus),.txt=::move(t),.msg=::move(m)} }
		{ SWEAR(p==JRP::Start) ; }
		//
		EngineClosure( JRP p , JE&& je , R rq , bool rpt ) : kind{K::Job} , ecj{p,::move(je),EngineClosureJobEtc{.report=rpt,.req=rq}} { SWEAR( p==JRP::GiveUp                        ) ; }
		EngineClosure( JRP p , JE&& je                   ) : kind{K::Job} , ecj{p,::move(je),EngineClosureJobEtc{                   }} { SWEAR( p==JRP::GiveUp || p==JRP::ReportStart ) ; }
		//
		EngineClosure( JRP p , JE&& je , JobRpcReq&& jrr , ::vmap_ss&& r ) :
			kind { K::Job                                                               }
		,	ecj  { p , ::move(je) , EngineClosureJobEnd{.rsrcs=::move(r),.end={::move(jrr)}} }
		{ SWEAR(p==JRP::End) ; }
		// JobMngt
		EngineClosure( JMP p , JE&& je , ::string&& t                       ) : kind{K::JobMngt} , ecjm{.proc=p,.job_exec=::move(je),.txt=::move(t)             } { SWEAR(p==JMP::LiveOut) ; }
		EngineClosure( JMP p , JE&& je , Fd fd_ , ::vmap_s<DepDigest>&& dds ) : kind{K::JobMngt} , ecjm{.proc=p,.job_exec=::move(je),.fd{fd_},.deps{::move(dds)}} {
			SWEAR( p==JMP::DepVerbose || p==JMP::ChkDeps ) ;
		}
		//
		EngineClosure(EngineClosure&& ec) : kind(ec.kind) {
			switch (ec.kind) {
				case K::Global  : new(&ecg ) ECG {::move(ec.ecg )} ; break ;
				case K::Req     : new(&ecr ) ECR {::move(ec.ecr )} ; break ;
				case K::Job     : new(&ecj ) ECJ {::move(ec.ecj )} ; break ;
				case K::JobMngt : new(&ecjm) ECJM{::move(ec.ecjm)} ; break ;
			DF}
		}
		~EngineClosure() {
			switch (kind) {
				case K::Global  : ecg .~ECG () ; break ;
				case K::Req     : ecr .~ECR () ; break ;
				case K::Job     : ecj .~ECJ () ; break ;
				case K::JobMngt : ecjm.~ECJM() ; break ;
			DF}
		}
		EngineClosure& operator=(EngineClosure const&) = delete ;
		EngineClosure& operator=(EngineClosure     &&) = delete ;
		// data
		Kind kind = K::Global ;
		union {
			ECG  ecg  ;
			ECR  ecr  ;
			ECJ  ecj  ;
			ECJM ecjm ;
		} ;
	} ;

	extern ThreadDeque<EngineClosure,true/*Flush*/> g_engine_queue ;

}

#endif
#ifdef IMPL

namespace Engine {

	inline ::string reason_str(JobReason const& reason) {
		::string res = reason.msg() ;
		if ( Node n{reason.node} ; +n ) {
			/**/                                          res <<" : "               << Disk::mk_file(n->name()               )        ;
			if (reason.tag==JobReasonTag::PollutedTarget) res <<" (polluting job : "<< Disk::mk_file(n->polluting_job->name()) <<" )" ;
		}
		return res ;
	}

	inline ::string title( ReqOptions const& ro , ::string const& s ) {
		if (ro.reverse_video==Maybe) return {} ;
		return "\x1b]0;" + s + '\a' ;
	}

	inline ::string color_pfx( ReqOptions const& ro , Color color ) {
		if ( color==Color::None || ro.reverse_video==Maybe || ro.flags[ReqFlag::Porcelaine] ) return {} ;
		::array<uint8_t,3/*RGB*/> const& colors = g_config->colors[+color][ro.reverse_video==Yes] ;
		return "\x1b[38;2;"s + int(colors[0/*R*/]) +';'+ int(colors[1/*G*/]) +';'+ int(colors[2/*B*/]) + 'm' ;
	}

	inline ::string color_sfx(ReqOptions const& ro , Color color ) {
		if ( color==Color::None || ro.reverse_video==Maybe || ro.flags[ReqFlag::Porcelaine] ) return {} ;
		return "\x1b[0m" ;
	}

}

#endif
