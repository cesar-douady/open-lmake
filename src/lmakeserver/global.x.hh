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
		// services
		::string pretty_str() const ;
		// data
		size_t n_aborted_job_creation = 0 ;
		size_t n_job_make             = 0 ;
		size_t n_node_make            = 0 ;
		size_t n_job_set_pressure     = 0 ;
		size_t n_node_set_pressure    = 0 ;
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
		JobInfoStart               start         = {}    ;
		bool                       report        = false ;
		::vmap<Node,FileActionTag> report_unlnks = {}    ;
		::string                   txt           = {}    ;
		::string                   msg           = {}    ;
	} ;

	struct EngineClosureJobEtc {
		friend ::string& operator+=( ::string& , EngineClosureJobEtc const& ) ;
		bool report = false ;
		Req  req    = {}    ;
	} ;

	struct EngineClosureJob {
		friend ::string& operator+=( ::string& , EngineClosureJob const& ) ;
		// cxtors & casts
		EngineClosureJob( JobRpcProc p , JobExec const& je , EngineClosureJobStart&& ecjs ) : proc{p} , job_exec{je} , start{ecjs} {}
		EngineClosureJob( JobRpcProc p , JobExec const& je , EngineClosureJobEtc  && ecje ) : proc{p} , job_exec{je} , etc  {ecje} {}
		EngineClosureJob( JobRpcProc p , JobExec const& je , JobEndRpcReq         && jerr ) : proc{p} , job_exec{je} , end  {jerr} {}
		//
		EngineClosureJob(EngineClosureJob&& ecj) : proc{ecj.proc} , job_exec{::move(ecj.job_exec)} {
			switch (ecj.proc) {
				case JobRpcProc::Start       : new(&start) EngineClosureJobStart{::move(ecj.start)} ; break ;
				case JobRpcProc::ReportStart :
				case JobRpcProc::GiveUp      : new(&etc  ) EngineClosureJobEtc  {::move(ecj.etc  )} ; break ;
				case JobRpcProc::End         : new(&end  ) JobEndRpcReq         {::move(ecj.end  )} ; break ;
			DF}
		}
		~EngineClosureJob() {
			switch (proc) {
				case JobRpcProc::Start       : start.~EngineClosureJobStart() ; break ;
				case JobRpcProc::ReportStart :
				case JobRpcProc::GiveUp      : etc  .~EngineClosureJobEtc  () ; break ;
				case JobRpcProc::End         : end  .~JobEndRpcReq         () ; break ;
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
			JobEndRpcReq          end   ;
		} ;
	} ;

	struct EngineClosureJobMngt {
		friend ::string& operator+=( ::string& , EngineClosureJobMngt const& ) ;
		JobMngtProc         proc     = {} ;
		JobExec             job_exec = {} ;
		Fd                  fd       = {} ;
		::vmap_s<DepDigest> deps     = {} ; // proc==ChkDeps|DepsInfo
		::string            txt      = {} ; // proc==LiveOut
	} ;

	struct EngineClosure {
		friend ::string& operator+=( ::string& , EngineClosure const& ) ;
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
		EngineClosure(RP p,R r,Fd ifd,Fd ofd,VS const& fs,RO const& ro) : kind{K::Req},ecr{.proc=p,.req=r,.in_fd=ifd,.out_fd=ofd,.files=fs,.options=ro} { SWEAR( p==RP::Make                 ) ; }
		EngineClosure(RP p,    Fd ifd,Fd ofd,VS const& fs,RO const& ro) : kind{K::Req},ecr{.proc=p,       .in_fd=ifd,.out_fd=ofd,.files=fs,.options=ro} { SWEAR( p!=RP::Make&&p>=RP::HasArgs ) ; }
		EngineClosure(RP p,R r,Fd ifd,Fd ofd                          ) : kind{K::Req},ecr{.proc=p,.req=r,.in_fd=ifd,.out_fd=ofd                      } { SWEAR( p==RP::Kill || p==RP::None  ) ; }
		EngineClosure(RP p,R r                                        ) : kind{K::Req},ecr{.proc=p,.req=r                                             } { SWEAR( p==RP::Close                ) ; }
		// Job
		EngineClosure( JRP p , JE&& je , JobInfoStart&& jis , bool r , ::vmap<Node,FileActionTag>&& rus={} , ::string&& t={} , ::string&& m={} ) :
			kind { K::Job                                                                                                       }
		,	ecj  { p , ::move(je) , EngineClosureJobStart{.start=jis,.report=r,.report_unlnks=::move(rus),.txt=::move(t),.msg=::move(m)} }
		{ SWEAR(p==JRP::Start) ; }
		//
		EngineClosure( JRP p , JE&& je , R rq , bool rpt     ) : kind{K::Job} , ecj{p,::move(je),EngineClosureJobEtc{.report=rpt,.req=rq}} { SWEAR( p==JRP::GiveUp                        ) ; }
		EngineClosure( JRP p , JE&& je                       ) : kind{K::Job} , ecj{p,::move(je),EngineClosureJobEtc{                   }} { SWEAR( p==JRP::GiveUp || p==JRP::ReportStart ) ; }
		EngineClosure( JRP p , JE&& je , JobEndRpcReq&& jerr ) : kind{K::Job} , ecj{p,::move(je),JobEndRpcReq       {::move(jerr)       }} { SWEAR( p==JRP::End                           ) ; }
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
