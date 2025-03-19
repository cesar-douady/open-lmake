// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "serialize.hh"

#include "rpc_client.hh"

#ifdef STRUCT_DECL

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

namespace Engine {

	struct EngineClosure    ;
	struct EngineClosureReq ;
	struct EngineClosureJob ;

}

#endif
#ifdef STRUCT_DEF

namespace Engine {

	// sep is put before the last indent level, useful for porcelaine output
	/**/   void _audit( Fd out , Fd log , ReqOptions const& , Color , ::string const& txt , bool as_is , DepDepth , char sep , bool err ) ;
	//                                                                                             as_is              lvl                                                          err
	inline void audit( Fd out , Fd log , ReqOptions const& ro , Color c , ::string const& t , bool a=false , DepDepth l=0 , char sep=0 ) { _audit(out,log,ro,c          ,t,a,l,sep,false) ; }
	inline void audit( Fd out ,          ReqOptions const& ro , Color c , ::string const& t , bool a=false , DepDepth l=0 , char sep=0 ) { _audit(out,{} ,ro,c          ,t,a,l,sep,false) ; }
	inline void audit( Fd out , Fd log , ReqOptions const& ro ,           ::string const& t , bool a=false , DepDepth l=0 , char sep=0 ) { _audit(out,log,ro,Color::None,t,a,l,sep,false) ; }
	inline void audit( Fd out ,          ReqOptions const& ro ,           ::string const& t , bool a=false , DepDepth l=0 , char sep=0 ) { _audit(out,{} ,ro,Color::None,t,a,l,sep,false) ; }
	//
	inline void audit_err( Fd out , ReqOptions const& ro , Color c , ::string const& t , bool as_is=false ) { _audit(out,{},ro,c          ,t,as_is,0/*lvl*/,0/*sep*/,true/*err*/) ; }
	inline void audit_err( Fd out , ReqOptions const& ro ,           ::string const& t , bool as_is=false ) { _audit(out,{},ro,Color::None,t,as_is,0/*lvl*/,0/*sep*/,true/*err*/) ; }
	//
	/**/   void audit_file( Fd out , ::string&& f ) ;
	//
	/**/   void audit_status( Fd out , Fd log , ReqOptions const& ro , bool ok ) ;
	inline void audit_status( Fd out ,          ReqOptions const& ro , bool ok ) { audit_status(out,{},ro,ok) ; }
	//
	/**/   void audit_ctrl_c( Fd out , Fd log , ReqOptions const& ro ) ;
	inline void audit_ctrl_c( Fd out ,          ReqOptions const& ro ) { audit_ctrl_c(out,{},ro) ; }

	inline ::string title    ( ReqOptions const& , ::string const& ) ;
	inline ::string color_pfx( ReqOptions const& , Color           ) ;
	inline ::string color_sfx( ReqOptions const& , Color           ) ;

	struct Kpi {
		friend ::string& operator+=( ::string& , Kpi const& ) ;
		struct ReqEntry {
			size_t n_job_req_info  = 0 ;
			size_t n_node_req_info = 0 ;
		} ;
		// services
		::string pretty_str() const ;
		// data
		size_t             n_aborted_job_creation = 0 ;
		size_t             n_job_make             = 0 ;
		size_t             n_node_make            = 0 ;
		size_t             n_job_set_pressure     = 0 ;
		size_t             n_node_set_pressure    = 0 ;
		Time::Delay        py_exec_time           ;
		::vector<ReqEntry> reqs                   ;
	} ;

}

#endif
#ifdef DATA_DEF

namespace Engine {

	extern Kpi g_kpi ;

	struct EngineClosureGlobal {
		GlobalProc proc = {} ;
	} ;

	struct EngineClosureReq {
		friend ::string& operator+=( ::string& , EngineClosureReq const& ) ;
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
		friend ::string& operator+=( ::string& , EngineClosureJobStart const& ) ;
		bool                       report        = false ;
		::vmap<Node,FileActionTag> report_unlnks = {}    ;
		::vector_s                 txts          = {}    ; // msg+stderr
	} ;

	struct EngineClosureJobReportStart {
		friend ::string& operator+=( ::string& , EngineClosureJobReportStart const& ) ;
	} ;

	struct EngineClosureJobGiveUp {
		friend ::string& operator+=( ::string& , EngineClosureJobGiveUp const& ) ;
		Req  req    = {}    ;
		bool report = false ;
	} ;

	struct EngineClosureJob
	:	             ::variant< Void/*None*/ , EngineClosureJobStart/*Start*/ , EngineClosureJobReportStart/*ReportStart*/ , EngineClosureJobGiveUp/*GiveUp*/ , JobDigest<Node>/*End*/ >
	{	using Base = ::variant< Void/*None*/ , EngineClosureJobStart/*Start*/ , EngineClosureJobReportStart/*ReportStart*/ , EngineClosureJobGiveUp/*GiveUp*/ , JobDigest<Node>/*End*/ > ;
		//
		friend ::string& operator+=( ::string& , EngineClosureJob const& ) ;
		//
		using Proc = JobRpcProc ;
		// cxtors & casts
		EngineClosureJob( JobExec const& je , EngineClosureJobStart      && ecjs  ) : Base{::move(ecjs )} , job_exec{je} {}
		EngineClosureJob( JobExec const& je , EngineClosureJobReportStart&& ecjrs ) : Base{::move(ecjrs)} , job_exec{je} {}
		EngineClosureJob( JobExec const& je , EngineClosureJobGiveUp     && ecjgu ) : Base{::move(ecjgu)} , job_exec{je} {}
		EngineClosureJob( JobExec const& je , JobDigest<Node>            && jd    ) : Base{::move(jd   )} , job_exec{je} {}
		// accesses
		/**/             Proc proc() const { return Proc(index()) ; }
		template<Proc P> bool is_a() const { return index()==+P   ; }
		//
		EngineClosureJobStart       const& start       () const { return ::get<EngineClosureJobStart      >(self) ; }
		EngineClosureJobStart            & start       ()       { return ::get<EngineClosureJobStart      >(self) ; }
		EngineClosureJobReportStart const& report_start() const { return ::get<EngineClosureJobReportStart>(self) ; }
		EngineClosureJobReportStart      & report_start()       { return ::get<EngineClosureJobReportStart>(self) ; }
		EngineClosureJobGiveUp      const& give_up     () const { return ::get<EngineClosureJobGiveUp     >(self) ; }
		EngineClosureJobGiveUp           & give_up     ()       { return ::get<EngineClosureJobGiveUp     >(self) ; }
		JobDigest<Node>             const& end         () const { return ::get<JobDigest<Node>            >(self) ; }
		JobDigest<Node>                  & end         ()       { return ::get<JobDigest<Node>            >(self) ; }
		// data
		JobExec job_exec = {} ;
	} ;

	struct EngineClosureJobMngt {
		friend ::string& operator+=( ::string& , EngineClosureJobMngt const& ) ;
		JobMngtProc   proc     = {} ;
		Fd            fd       = {} ;
		JobExec       job_exec = {} ;
		::vector<Dep> deps     = {} ; // proc==ChkDeps|DepsInfo
		::string      txt      = {} ; // proc==LiveOut
	} ;

	struct EngineClosure
	:	             ::variant< EngineClosureGlobal , EngineClosureReq , EngineClosureJob , EngineClosureJobMngt >
	{	using Base = ::variant< EngineClosureGlobal , EngineClosureReq , EngineClosureJob , EngineClosureJobMngt > ;
		//
		friend ::string& operator+=( ::string& , EngineClosure const& ) ;
		//
		using Kind = EngineClosureKind    ;
		using ECG  = EngineClosureGlobal  ;
		using ECR  = EngineClosureReq     ;
		using ECJ  = EngineClosureJob     ;
		using ECJM = EngineClosureJobMngt ;
		//
		using GP  = GlobalProc      ;
		using RP  = ReqProc         ;
		using JRP = JobRpcProc      ;
		using JMP = JobMngtProc     ;
		using J   = Engine::Job     ;
		using JD  = JobDigest<Node> ;
		using JE  = JobExec         ;
		using K   = Kind            ;
		using R   = Engine::Req     ;
		using RO  = ReqOptions      ;
		using VS  = ::vector_s      ;
		//
		// cxtors & casts
		// Global
		EngineClosure(GP p=GP::None) : Base{ECG{.proc=p}} {}
		// Req
		EngineClosure(RP p,R r,Fd ifd,Fd ofd,VS const& fs,RO const& ro) : Base{ECR{.proc=p,.req=r,.in_fd=ifd,.out_fd=ofd,.files=fs,.options=ro}} { SWEAR( p==RP::Make                 ) ; }
		EngineClosure(RP p,    Fd ifd,Fd ofd,VS const& fs,RO const& ro) : Base{ECR{.proc=p,       .in_fd=ifd,.out_fd=ofd,.files=fs,.options=ro}} { SWEAR( p!=RP::Make&&p>=RP::HasArgs ) ; }
		EngineClosure(RP p,R r,Fd ifd,Fd ofd                          ) : Base{ECR{.proc=p,.req=r,.in_fd=ifd,.out_fd=ofd                      }} { SWEAR( p==RP::Kill || p==RP::None  ) ; }
		EngineClosure(RP p,R r                                        ) : Base{ECR{.proc=p,.req=r                                             }} { SWEAR( p==RP::Close                ) ; }
		// Job
		EngineClosure( JRP p , JE&& je , bool r , ::vmap<Node,FileActionTag>&& rus={} , ::vector_s&& txts={} ) :
			Base{ECJ{ ::move(je) , EngineClosureJobStart{.report=r,.report_unlnks=::move(rus),.txts=::move(txts)} }}
		{ (void)p ; SWEAR(p==JRP::Start) ; }
		//
		EngineClosure( JRP p , JE&& je , R rq , bool rpt ) : Base{ECJ{::move(je),EngineClosureJobGiveUp     {.req=rq,.report=rpt}}} { SWEAR(p==JRP::GiveUp     ) ; }
		EngineClosure( JRP p , JE&& je                   ) : Base{ECJ{::move(je),EngineClosureJobReportStart{                   }}} { SWEAR(p==JRP::ReportStart) ; }
		EngineClosure( JRP p , JE&& je , JD&& jd         ) : Base{ECJ{::move(je),::move(jd)                                      }} { SWEAR(p==JRP::End        ) ; }
		// JobMngt
		EngineClosure( JMP p , JE&& je , ::string&& t                 ) : Base{ECJM{.proc=p,         .job_exec=::move(je),.txt=::move(t)    }} { SWEAR(p==JMP::LiveOut) ; }
		EngineClosure( JMP p , JE&& je , Fd fd_ , ::vector<Dep>&& dds ) : Base{ECJM{.proc=p,.fd{fd_},.job_exec=::move(je),.deps{::move(dds)}}} {
			SWEAR( p==JMP::DepVerbose || p==JMP::ChkDeps ) ;
		}
		// accesses
		/**/             Kind kind() const { return Kind(index()) ; }
		template<Kind K> bool is_a() const { return index()==+K   ; }
		//
		ECG  const& ecg () const { return ::get<ECG >(self) ; }
		ECG       & ecg ()       { return ::get<ECG >(self) ; }
		ECR  const& ecr () const { return ::get<ECR >(self) ; }
		ECR       & ecr ()       { return ::get<ECR >(self) ; }
		ECJ  const& ecj () const { return ::get<ECJ >(self) ; }
		ECJ       & ecj ()       { return ::get<ECJ >(self) ; }
		ECJM const& ecjm() const { return ::get<ECJM>(self) ; }
		ECJM      & ecjm()       { return ::get<ECJM>(self) ; }
	} ;

	extern ThreadDeque<EngineClosure,true/*Flush*/> g_engine_queue ;
	extern bool                                     g_writable     ;


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
