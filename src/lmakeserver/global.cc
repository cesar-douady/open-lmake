// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh" // /!\ must be first to include Python.h first

using namespace Disk ;
using namespace Hash ;
using namespace Py   ;
using namespace Time ;

namespace Engine {

	ThreadQueue<EngineClosure,true/*Flush*/,true/*Urgent*/> g_engine_queue ;
	bool                                                    g_writable     = false ;
	Kpi                                                     g_kpi          ;

	static Mutex<MutexLvl::Audit> _g_audit_mutex ;

	::string _audit_indent( ::string&& t , DepDepth l , char sep ) {
		if (!l) {
			SWEAR( !sep || sep=='\t' ) ; // cannot have a sep if we have no room to put it
			return ::move(t) ;
		}
		if (sep=='\t') {
			return indent<'\t',1>(t,l) ;
		} else {
			::string res = indent<' ',2>(t,l) ;
			if (sep) res[2*(l-1)] = sep ;
			return res ;
		}
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
		Lock            lock { _g_audit_mutex }                                        ;
		try                       { OMsgBuf().send( out , ReqRpcReply( proc , _audit_indent(::move(report_txt),lvl,sep) ) ) ; } // if we lose connection, there is nothing much we ...
		catch (::string const& e) { Trace("audit","lost_client",e) ;                                                          } // ... can do about it (hoping that we can still trace)
		if (+log)
			try                       { log.write(_audit_indent(ensure_nl(as_is?txt:localize(txt,{})),lvl,sep)) ; }             // .
			catch (::string const& e) { Trace("audit","lost_log",e) ;                                             }             // NO_COV defensive programming
	}

	void audit_file( Fd out , ::string&& file ) {
		Lock lock { _g_audit_mutex } ;
		try                       { OMsgBuf().send( out , ReqRpcReply( ReqRpcReplyProc::File , ::move(file) ) ) ; } // if we lose connection, there is nothing much we ...
		catch (::string const& e) { Trace("audit_file","lost_client",e) ;                                         } // ... can do about it (hoping that we can still trace)
	}

	void audit_status( Fd out , Fd log , ReqOptions const& , Rc rc ) {
		Lock lock { _g_audit_mutex } ;
		try                       { OMsgBuf().send( out , ReqRpcReply( ReqRpcReplyProc::Status , rc ) ) ; } // if we lose connection, there is nothing much we ...
		catch (::string const& e) { Trace("audit_status","lost_client",e) ;                               } // ... can do about it (hoping that we can still trace)
		if (+log)
			try                       { log.write(cat("status : ",rc,'\n')) ; }                             // .
			catch (::string const& e) { Trace("audit_status","lost_log",e) ;  }                             // NO_COV defensive programming
	}

	void audit_ctrl_c( Fd out, Fd log , ReqOptions const& ro ) {
		// lmake echos a \n as soon as it sees ^C (and it does that much faster than we could), no need to do it here
		::string msg ;
		if (g_config->console.date_prec!=uint8_t(-1)) msg << Pdate(New).str(g_config->console.date_prec,true/*in_day*/) <<' ' ;
		/**/                                          msg << "kill"                                                           ;
		::string report_txt  = color_pfx(ro,Color::Note) + msg + color_sfx(ro,Color::Note) +'\n' ;
		//
		Lock lock { _g_audit_mutex } ;
		try                       { OMsgBuf().send( out, ReqRpcReply( ReqRpcReplyProc::Stdout , ::move(report_txt) ) ) ; } // if we lose connection, there is nothing much we ...
		catch (::string const& e) { Trace("audit_ctrl_c","lost_client",e) ;                                              } // ... can do about it (hoping that we can still trace)
		if (+log)
			try                       { log.write("^C\n"+msg+'\n') ;         }                                             // .
			catch (::string const& e) { Trace("audit_ctrl_c","lost_log",e) ; }                                             // NO_COV defensive programming
	}

	// str has the same syntax as python f-strings
	// cb_fixed is called on each fixed part found
	// cb_stem  is called on each stem       found
	// stems are of the form {<identifier>\*?} or {<identifier>?\*?:.*} (where .* after : must have matching {})
	// cb_stem is called with :
	// - the <identifier>
	// - true if <identifier> is followed by a *
	// - the regular expression that follows the : or nullptr for the first case
	// /!\ : this function is also implemented in read_makefiles.py:add_stems, both must stay in sync
	void parse_py( ::string const& str , size_t* unnamed_star_idx , ParsePyFuncStem const& cb_stem , ParsePyFuncFixed const& cb_fixed ) {
		enum State { Literal , SeenStart , Key , Re , SeenStop } ;
		State    state       = Literal ;
		::string fixed       ;
		::string key         ;
		::string re          ;
		size_t   unnamed_idx = 1       ;
		size_t   depth       = 0       ;
		bool     has_pfx     = false   ;
		for( char c : str ) {
			bool with_re = false ;
			switch (state) {
				case Literal :
					if      (c=='{') state = SeenStart ;
					else if (c=='}') state = SeenStop  ;
					else             fixed.push_back(c) ;
				break ;
				case SeenStop :
					if (c!='}') goto End ;
					state = Literal ;
					fixed.push_back(c) ;                                                                             // }} are transformed into }
				break ;
				case SeenStart :
					if (c=='{') {
						state = Literal ;
						fixed.push_back(c) ;                                                                         // {{ are transformed into {
						break ;
					}
					if (+fixed) { cb_fixed(fixed,has_pfx,true/*has_sfx*/) ; fixed.clear() ; }
					state = Key ;
				[[fallthrough]] ;
				case Key :
					if (c!='}') {
						if (c==':') state = Re ;
						else        key.push_back(c) ;
						break ;
					}
					goto Call ;
				break ;
				case Re :
					if (!( c=='}' && depth==0 )) {
						if      (c=='{') depth++ ;
						else if (c=='}') depth-- ;
						re.push_back(c) ;
						break ;
					}
					with_re = true ;
				Call :
					// trim key
					size_t start = 0          ; while( start<key.size() && is_space(key[start]) ) start++ ;
					size_t end   = key.size() ; while( end>start        && is_space(key[end-1]) ) end  -- ;
					if (end<=start) key.clear() ;
					else            key = key.substr(start,end-start) ;
					bool star = +key && key.back()=='*' ;
					if (star) key.pop_back() ;
					bool unnamed = !key ;
					if (unnamed) {
						throw_unless( unnamed_star_idx , "no auto-stem allowed in ",str ) ;
						if (star) { throw_unless( with_re , "unnamed star stems must be defined in ",str ) ; key = cat("<star_stem",(*unnamed_star_idx)++,'>') ; }
						else                                                                                 key = cat("<stem"     ,  unnamed_idx      ++,'>') ;
					} else {
						throw_unless( is_identifier(key) , "bad key ",key," must be empty or an identifier" ) ;
					}
					if (with_re) { cb_stem(key,star,unnamed,&re    ) ; re.clear() ; }
					else         { cb_stem(key,star,unnamed,nullptr) ;              }
					key.clear() ;
					state   = Literal ;
					has_pfx = true    ;
				break ;
			}
		}
	End :
		switch (state) {
			case Literal   : { if (+fixed) cb_fixed(fixed,has_pfx,false/*has_pfx*/) ; } break                      ; // trailing fixed
			case SeenStop  :                                                            throw "spurious } in "+str ;
			case SeenStart :
			case Key       :
			case Re        :                                                            throw "spurious { in "+str ;
		DF}                                                                                                          // NO_COV
	}

	// star stems are represented by a StemMrkr followed by the stem idx
	// return str with stems substituted with the return values of cb_stem and fixed parts replaced by the values of cb_fixed
	::string subst_target( ::string const& str , SubstTargetFuncStem const& cb_stem , SubstTargetFuncFixed cb_fixed , VarIdx stop_above ) {
		::string res   ;
		::string fixed ;
		for( size_t i=0 ; i<str.size() ; i++ ) { // /!\ not a iota
			char c = str[i] ;
			if (c!=Rule::StemMrkr) { fixed += c ; continue ;                  }
			if (+fixed           ) { res += cb_fixed(fixed) ; fixed.clear() ; }
			//
			VarIdx stem = decode_int<VarIdx>(&str[i+1]) ;
			i += sizeof(VarIdx) ;
			if (stem>=stop_above) return res ;
			res += cb_stem(res.size(),stem) ;
		}
		if (+fixed) res += cb_fixed(fixed) ;
		return res ;
	}

	//
	// EngineClosure
	//

	::string& operator+=( ::string& os , EngineClosureGlobal const& ecg ) { // START_OF_NO_COV
		return os << "Glb(" << ecg.proc <<')' ;
	}                                                                       // END_OF_NO_COV

	::string& operator+=( ::string& os , EngineClosureReq const& ecr ) {                                                                // START_OF_NO_COV
		os << "Ecr(" << ecr.proc <<',' ;
		switch (ecr.proc) {
			case ReqProc::None    :
			case ReqProc::Kill    : os << ecr.req <<','<< ecr.in_fd  <<','<< ecr.out_fd                                       ; break ;
			case ReqProc::Close   : os << ecr.req                                                                             ; break ;
			case ReqProc::Collect :                                                                                                     // PER_CMD : format for tracing
			case ReqProc::Debug   :                                                                                                     // PER_CMD : format for tracing
			case ReqProc::Forget  :
			case ReqProc::Mark    :
			case ReqProc::Show    : os <<                 ecr.in_fd  <<','<< ecr.out_fd <<','<< ecr.options <<','<< ecr.files ; break ;
			case ReqProc::Make    : os << ecr.req <<','<< ecr.in_fd  <<','<< ecr.out_fd <<','<< ecr.options <<','<< ecr.files ; break ;
		DF}                                                                                                                             // NO_COV
		return os <<')' ;
	}                                                                                                                                   // END_OF_NO_COV

	::string& operator+=( ::string& os , EngineClosureJobStart const& ecjs ) { // START_OF_NO_COV
		First first ;
		/**/                     os << "Ecjs("                           ;
		if (ecjs.report        ) os <<first("",",")<< "report"           ;
		if (+ecjs.report_unlnks) os <<first("",",")<< ecjs.report_unlnks ;
		if (+ecjs.msg_stderr   ) os <<first("",",")<< ecjs.msg_stderr    ;
		return                   os <<')'                                ;
	}                                                                          // END_OF_NO_COV

	::string& operator+=( ::string& os , EngineClosureJobReportStart const& ) { // START_OF_NO_COV
		return os << "Ecjrs()" ;
	}                                                                           // END_OF_NO_COV

	::string& operator+=( ::string& os , EngineClosureJobGiveUp const& ecjgu ) { // START_OF_NO_COV
		First first ;
		/**/               os << "Ecjgu("                 ;
		if ( ecjgu.report) os <<first("",",")<< "report"  ;
		if (+ecjgu.req   ) os <<first("",",")<< ecjgu.req ;
		return             os <<')'                       ;
	}                                                                            // END_OF_NO_COV

	::string& operator+=( ::string& os , EngineClosureJob const& ecj ) {                  // START_OF_NO_COV
		/**/                               os << "(" << ecj.proc() <<','<< ecj.job_exec ;
		switch (ecj.proc()) {
			case JobRpcProc::Start       : os << ecj.start       () ; break ;
			case JobRpcProc::ReportStart : os << ecj.report_start() ; break ;
			case JobRpcProc::GiveUp      : os << ecj.give_up     () ; break ;
			case JobRpcProc::End         : os << ecj.end         () ; break ;
		DF}                                                                               // NO_COV
		return                             os <<')' ;
	}                                                                                     // END_OF_NO_COV

	::string& operator+=( ::string& os , EngineClosureJobMngt const& ecjm ) {                    // START_OF_NO_COV
		/**/                               os << "JobMngt(" << ecjm.proc <<','<< ecjm.job_exec ;
		switch (ecjm.proc) {
			case JobMngtProc::LiveOut    : os <<','<< ecjm.txt.size() ; break ;
			case JobMngtProc::DepVerbose : os <<','<< ecjm.deps       ; break ;
			case JobMngtProc::ChkDeps    : os <<','<< ecjm.deps       ; break ;
		DF}                                                                                      // NO_COV
		return                             os << ')' ;
	}                                                                                            // END_OF_NO_COV

	::string& operator+=( ::string& os , EngineClosure const& ec ) {                        // START_OF_NO_COV
		/**/                                    os << "EngineClosure(" << ec.kind() <<',' ;
		switch (ec.kind()) {
			case EngineClosure::Kind::Global  : os << ec.ecg () ; break ;
			case EngineClosure::Kind::Req     : os << ec.ecr () ; break ;
			case EngineClosure::Kind::Job     : os << ec.ecj () ; break ;
			case EngineClosure::Kind::JobMngt : os << ec.ecjm() ; break ;
		DF}                                                                                 // NO_COV
		return                                  os << ')' ;
	}                                                                                       // END_OF_NO_COV

	Job EngineClosureReq::job() const {
		SWEAR(is_job()) ;
		::vector<Job> candidates ;
		for( Rule r : Persistent::rule_lst() ) {
			Job j { r , files[0] } ;
			if ( !j                                                                                ) continue ;
			if ( options.flags[ReqFlag::Rule] && r->user_name()!=options.flag_args[+ReqFlag::Rule] ) continue ;
			candidates.push_back(j) ;
		}
		if      (candidates.size()==1) return candidates[0]                       ;
		else if (!candidates         ) throw "cannot find job "+mk_file(files[0]) ;
		else {
			SWEAR(!options.flags[ReqFlag::Rule]) ;                   // impossible to have several candidates if the rule is specified
			::string err_str = "several rules match, consider :\n" ;
			for( Job j : candidates ) err_str << _audit_indent( "lmake -R "+mk_shell_str(j->rule()->user_name())+" -J "+files[0] ,1) << '\n' ;
			throw err_str ;
		}
	}

	//
	// Kpi
	//

	::string& operator+=( ::string& os , Kpi const& kpi ) { // START_OF_NO_COV
		os << "Kpi(" ;
		if ( kpi.n_aborted_job_creation) os <<",AJC:" << kpi.n_aborted_job_creation ;
		if ( kpi.n_job_make            ) os <<",JM:"  << kpi.n_job_make             ;
		if ( kpi.n_node_make           ) os <<",NM:"  << kpi.n_node_make            ;
		if ( kpi.n_job_set_pressure    ) os <<",JSP:" << kpi.n_job_set_pressure     ;
		if ( kpi.n_node_set_pressure   ) os <<",NSP:" << kpi.n_node_set_pressure    ;
		if (+kpi.py_exec_time          ) os <<",ET:"  << kpi.py_exec_time           ;
		if (+kpi.reqs                  ) os <<",Reqs:"<< kpi.reqs.size()            ;
		return os << ")" ;
	}                                                       // END_OF_NO_COV

	::string Kpi::pretty_str() const {
		::string res ;
		if (n_aborted_job_creation) res <<"n_aborted_job_creation : "<< n_aborted_job_creation   <<'\n' ;
		if (n_job_make            ) res <<"n_job_make             : "<< n_job_make               <<'\n' ;
		if (n_node_make           ) res <<"n_node_make            : "<< n_node_make              <<'\n' ;
		if (n_job_set_pressure    ) res <<"n_job_set_pressure     : "<< n_job_set_pressure       <<'\n' ;
		if (n_node_set_pressure   ) res <<"n_node_set_pressure    : "<< n_node_set_pressure      <<'\n' ;
		if (+py_exec_time         ) res <<"python_exec_time       : "<< py_exec_time.short_str() <<'\n' ;
		for ( ReqEntry const& re : reqs ) {
			res <<"\tn_job_req_info  : " << re.n_job_req_info  <<'\n' ;
			res <<"\tn_node_req_info : " << re.n_node_req_info <<'\n' ;
		}
		return res ;
	}

	//
	// Rules & Sources
	//

	RulesBase::RulesBase(Dict const& py_d) : RulesBase{New} {
		py_sys_path = &py_d["sys_path"].as_a<Sequence>() ;
		if (!py_sys_path->is_a<Tuple>()) {                                                                                                  // convert to tuple if necessary, so as to be sure ...
			Gil gil ;                                                                                                                       // ... it is frozen and to stabilize crc
			Ptr<Tuple> py_t { py_sys_path->size() } ; for( size_t i : iota(py_sys_path->size()) ) py_t->set_item( i , (*py_sys_path)[i] ) ;
			py_sys_path = ::move(py_t) ;
		}
		sys_path_crc = Crc( New , ::string(*py_sys_path->str()) ) ;
		for( Object const& py_rule : py_d["rules"].as_a<Sequence>() ) emplace_back( self , py_rule.as_a<Dict>() ) ;
	}

	void RulesBase::compile() {
		for( RuleData& rd : self ) rd.compile() ;                                                                          // for cmd and patterns
		name_sz = ::max<size_t>( self , [](RuleData const& rd) { return rd.user_name().size() ; } , Rule::NoRuleNameSz ) ;
		name_sz = ::max( name_sz , strlen("no_rule") )                                                                   ;
	}

	Sources::Sources(PyType const& py_srcs) {
			for( Object const& py_src  : py_srcs ) emplace_back(py_src.as_a<Str>()) ;
	}

}
