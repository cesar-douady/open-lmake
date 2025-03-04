// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh" // must be first to include Python.h first

using namespace Disk ;
using namespace Hash ;
using namespace Py   ;
using namespace Time ;

namespace Engine {

	ThreadDeque<EngineClosure> g_engine_queue ;
	bool                       g_writable     = false   ;
	Kpi                        g_kpi          ;

	static ::string _audit_indent( ::string&& t , DepDepth l , char sep=0 ) {
		if (!l) {
			SWEAR(!sep) ;      // cannot have a sep if we have no room to put it
			return ::move(t) ;
		}
		::string res = indent<' ',2>(t,l) ;
		if (sep) res[2*(l-1)] = sep ;
		return res ;
	}

	void _audit( Fd out , Fd log , ReqOptions const& ro , Color c , ::string const& txt , bool as_is , DepDepth lvl , char sep , bool err ) {
		if (!txt) return ;
		//
		::string   report_txt  = color_pfx(ro,c)                              ;
		if (as_is) report_txt += ensure_no_nl(         txt                  ) ;
		else       report_txt += ensure_no_nl(localize(txt,ro.startup_dir_s)) ; // ensure color suffix is not at start-of-line to avoid indent adding space at end of report
		/**/       report_txt += color_sfx(ro,c)                              ;
		/**/       report_txt += '\n'                                         ;
		//
		ReqRpcReplyProc proc = err ? ReqRpcReplyProc::Stderr : ReqRpcReplyProc::Stdout ;
		try                       { OMsgBuf().send( out , ReqRpcReply(proc,_audit_indent(::move(report_txt),lvl,sep)) ) ; } // if we lose connection, there is nothing much we ...
		catch (::string const& e) { Trace("audit","lost_client",e) ;                                                      } // ... can do about it (hoping that we can still trace)
		if (+log)
			try                       { log.write(_audit_indent(ensure_nl(as_is?txt:localize(txt,{})),lvl,sep)) ; }         // .
			catch (::string const& e) { Trace("audit","lost_log",e) ;                                             }
	}

	void audit_file( Fd out , ::string&& file ) {
		try                       { OMsgBuf().send( out , ReqRpcReply(ReqRpcReplyProc::File,::move(file)) ) ; } // if we lose connection, there is nothing much we ...
		catch (::string const& e) { Trace("audit_file","lost_client",e) ;                                     } // ... can do about it (hoping that we can still trace)
	}

	void audit_status( Fd out , Fd log , ReqOptions const& , bool ok ) {
		try                       { OMsgBuf().send( out , ReqRpcReply(ReqRpcReplyProc::Status,ok) ) ; } // if we lose connection, there is nothing much we ...
		catch (::string const& e) { Trace("audit_status","lost_client",e) ;                           } // ... can do about it (hoping that we can still trace)
		if (+log)
			try                       { log.write("status : "s+(ok?"ok":"failed")+'\n') ; }             // .
			catch (::string const& e) { Trace("audit_status","lost_log",e) ;              }
	}

	void audit_ctrl_c( Fd out, Fd log , ReqOptions const& ro ) {
		// lmake echos a \n as soon as it sees ^C (and it does that much faster than we could), no need to do it here
		::string msg ;
		if (g_config->console.date_prec!=uint8_t(-1)) msg << Pdate(New).str(g_config->console.date_prec,true/*in_day*/) <<' ' ;
		/**/                                          msg << "kill"                                                           ;
		::string report_txt  = color_pfx(ro,Color::Note) + msg + color_sfx(ro,Color::Note) +'\n' ;
		//
		try                       { OMsgBuf().send( out, ReqRpcReply(ReqRpcReplyProc::Stdout,::move(report_txt)) ) ; } // if we lose connection, there is nothing much we ...
		catch (::string const& e) { Trace("audit_ctrl_c","lost_client",e) ;                                          } // ... can do about it (hoping that we can still trace)
		if (+log)
			try                       { log.write("^C\n"+msg+'\n') ;         }                                         // .
			catch (::string const& e) { Trace("audit_ctrl_c","lost_log",e) ; }
	}

	//
	// EngineClosure
	//

	::string& operator+=( ::string& os , EngineClosureGlobal const& ecg ) {
		return os << "Glb(" << ecg.proc <<')' ;
	}

	::string& operator+=( ::string& os , EngineClosureReq const& ecr ) {
		os << "Ecr(" << ecr.proc <<',' ;
		switch (ecr.proc) {
			case ReqProc::Debug  : // PER_CMD : format for tracing
			case ReqProc::Forget :
			case ReqProc::Mark   :
			case ReqProc::Show   : os <<                 ecr.in_fd  <<','<< ecr.out_fd <<','<< ecr.options <<','<< ecr.files ; break ;
			case ReqProc::Make   : os << ecr.req <<','<< ecr.in_fd  <<','<< ecr.out_fd <<','<< ecr.options <<','<< ecr.files ; break ;
			case ReqProc::Kill   :
			case ReqProc::None   : os << ecr.req <<','<< ecr.in_fd  <<','<< ecr.out_fd                                       ; break ;
			case ReqProc::Close  : os << ecr.req                                                                             ; break ;
		DF}
		return os <<')' ;
	}

	::string& operator+=( ::string& os , EngineClosureJobStart const& ecjs ) {
		/**/                     os << "Ecjs(" << ecjs.start   ;
		if (ecjs.report        ) os <<",report"                ;
		if (+ecjs.report_unlnks) os <<','<< ecjs.report_unlnks ;
		if (+ecjs.txt          ) os <<','<< ecjs.txt           ;
		if (+ecjs.msg          ) os <<','<< ecjs.msg           ;
		return                   os <<')'                      ;
	}

	::string& operator+=( ::string& os , EngineClosureJobEtc const& ecje ) {
		const char* sep = "" ;
		/**/                os << "Ecje("       ;
		if ( ecje.report) { os <<      "report" ; sep = "," ; }
		if (+ecje.req   )   os <<sep<< ecje.req ;
		return              os <<')'            ;
	}

	::string& operator+=( ::string& os , EngineClosureJob const& ecj ) {
		/**/                               os << "(" << ecj.proc <<','<< ecj.job_exec ;
		switch (ecj.proc) {
			case JobRpcProc::Start       : os << ecj.start ; break ;
			case JobRpcProc::ReportStart :
			case JobRpcProc::GiveUp      : os << ecj.etc   ; break ;
			case JobRpcProc::End         : os << ecj.end   ; break ;
		DF}
		return                             os <<')' ;
	}

	::string& operator+=( ::string& os , EngineClosureJobMngt const& ecjm ) {
		/**/                               os << "JobMngt(" << ecjm.proc <<','<< ecjm.job_exec ;
		switch (ecjm.proc) {
			case JobMngtProc::LiveOut    : os <<','<< ecjm.txt.size() ; break ;
			case JobMngtProc::DepVerbose : os <<','<< ecjm.deps       ; break ;
			case JobMngtProc::ChkDeps    : os <<','<< ecjm.deps       ; break ;
		DF}
		return                             os << ')' ;
	}

	::string& operator+=( ::string& os , EngineClosure const& ec ) {
		/**/                                    os << "EngineClosure(" << ec.kind <<',' ;
		switch (ec.kind) {
			case EngineClosure::Kind::Global  : os << ec.ecg  ; break ;
			case EngineClosure::Kind::Req     : os << ec.ecr  ; break ;
			case EngineClosure::Kind::Job     : os << ec.ecj  ; break ;
			case EngineClosure::Kind::JobMngt : os << ec.ecjm ; break ;
		DF}
		return                                  os << ')' ;
	}

	::vector<Node> EngineClosureReq::targets(::string const& startup_dir_s) const {
		SWEAR(!as_job()) ;
		RealPathEnv    rpe       { .lnk_support=g_config->lnk_support , .repo_root_s=*g_repo_root_s } ;
		RealPath       real_path { rpe                                                              } ;
		::vector<Node> targets   ; targets.reserve(files.size()) ;                                      // typically, there is no bads
		::string       err_str   ;
		for( ::string const& target : files ) {
			RealPath::SolveReport rp = real_path.solve(target,true/*no_follow*/) ;                      // we may refer to a symbolic link
			if (rp.file_loc==FileLoc::Repo) targets.emplace_back(rp.real) ;
			else                            err_str << _audit_indent(mk_rel(target,startup_dir_s),1) << '\n' ;
		}
		//
		throw_unless( !err_str , "files are outside repo :\n",err_str ) ;
		return targets ;
	}

	Job EngineClosureReq::job(::string const& startup_dir_s) const {
		SWEAR(as_job()) ;
		if (options.flags[ReqFlag::Rule]) {
			::string const& rule_name = options.flag_args[+ReqFlag::Rule] ;
			auto            it        = Rule::s_by_name.find(rule_name)   ; throw_unless( it!=Rule::s_by_name.end() , "cannot find rule ",rule_name                                              ) ;
			Job             j         { it->second , files[0] }           ; throw_unless( +j                        , "cannot find job ",mk_rel(files[0],startup_dir_s)," using rule ",rule_name ) ;
			return j ;
		}
		::vector<Job> candidates ;
		for( Rule r : Persistent::rule_lst() ) {
			if ( Job j{r,files[0]} ; +j ) candidates.push_back(j) ;
		}
		if      (candidates.size()==1) return candidates[0]                                    ;
		else if (!candidates         ) throw "cannot find job "+mk_rel(files[0],startup_dir_s) ;
		else {
			::string err_str = "several rules match, consider :\n" ;
			for( Job j : candidates ) err_str << _audit_indent( "lmake -R "+mk_shell_str(j->rule()->full_name())+" -J "+files[0] ,1) << '\n' ;
			throw err_str ;
		}
	}

	//
	// Kpi
	//

	::string& operator+=( ::string& os , Kpi const& kpi ) {
		os << "Kpi(" ;
		if ( kpi.n_aborted_job_creation) os <<",AJC:" << kpi.n_aborted_job_creation ;
		if ( kpi.n_job_make            ) os <<",JM:"  << kpi.n_job_make             ;
		if ( kpi.n_node_make           ) os <<",NM:"  << kpi.n_node_make            ;
		if ( kpi.n_job_set_pressure    ) os <<",JSP:" << kpi.n_job_set_pressure     ;
		if ( kpi.n_node_set_pressure   ) os <<",NSP:" << kpi.n_node_set_pressure    ;
		if (+kpi.py_exec_time          ) os <<",ET:"  << kpi.py_exec_time           ;
		if (+kpi.reqs                  ) os <<",Reqs:"<< kpi.reqs.size()            ;
		return os << ")" ;
	}

	::string Kpi::pretty_str() const {
		::string res ;
		if (n_aborted_job_creation) res <<"n_aborted_job_creation : "<< n_aborted_job_creation   <<'\n' ;
		if (n_job_make            ) res <<"n_job_make             : "<< n_job_make               <<'\n' ;
		if (n_node_make           ) res <<"n_node_make            : "<< n_node_make              <<'\n' ;
		if (n_job_set_pressure    ) res <<"n_job_set_pressure     : "<< n_job_set_pressure       <<'\n' ;
		if (n_node_set_pressure   ) res <<"n_node_set_pressure    : "<< n_node_set_pressure      <<'\n' ;
		if (+py_exec_time         ) res <<"python_exec_time       : "<< py_exec_time.short_str() <<'\n' ;
		for ( ReqEntry const& re : reqs ) {
			res <<"\tn_job_req_info  : " << re.n_job_req_info <<'\n' ;
			res <<"\tn_job_node_info : " << re.n_job_req_info <<'\n' ;
		}
		return res ;
	}

}
