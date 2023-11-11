// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh"

using namespace Disk ;

namespace Engine {

	static bool _is_mark_glb(ReqKey key) {
		switch (key) {
			case ReqKey::Clear  :
			case ReqKey::List   : return true  ;
			case ReqKey::Add    :
			case ReqKey::Delete : return false ;
			default : FAIL(key) ;
		}
	}

	static bool/*ok*/ _freeze(EngineClosureReq const& ecr) {
		Fd                fd = ecr.out_fd  ;
		ReqOptions const& ro = ecr.options ;
		Trace trace("freeze",ecr) ;
		if (_is_mark_glb(ro.key)) {
			::vector<Job> markeds = Job::s_frozens() ;
			size_t        w       = 0                ; for( Job j : markeds ) w = ::max(w,j->rule->name.size()) ;
			if (ro.key==ReqKey::Clear) {
				for( Job j : markeds )
					if (j->rule->is_src()) Node(j.name())->mk_no_src() ;
					else                   j->status = Status::Garbage ;
				Job::s_clear_frozens() ;
			}
			for( Job j : markeds ) audit( fd , ro , ro.key==ReqKey::List?Color::Warning:Color::Note , 0/*lvl*/ , to_string(::setw(w),j->rule->name) , j.name() ) ;
			return true ;
		} else {
			bool           add      = ro.key==ReqKey::Add ;
			size_t         w        = 0                   ;
			::vector<Node> new_srcs ;
			::vector<Node> old_srcs ;
			::string       name     ;
			::string       err      ;
			::vector<Job > to_do    ;
			//
			auto handle_job = [&](Job j)->bool/*ok*/ {
				if (add) {
					if (!j.active() ) throw ::pair("job not found"s   ,j.name()) ;
					if ( j->frozen()) throw ::pair("already frozen"s  ,j.name()) ;
					if ( j->is_src()) throw ::pair("file is a source"s,j.name()) ;
				} else {
					if (!j.active() ) throw ::pair("not frozen"s,j.name()) ;
					if (!j->frozen()) throw ::pair("not frozen"s,j.name()) ;
					if ( j->is_src()) old_srcs.push_back(j.name()) ;
				}
				if ( !j->running_reqs().empty() ) throw ::pair("job is running"s,j.name()) ;
				//
				w = ::max( w , j->rule->name.size() ) ;
				to_do.push_back(j) ;
				return true/*ok*/ ;
			} ;
			//check
			if (ecr.as_job()) {
				handle_job(ecr.job()) ;
			} else {
				bool force = ro.flags[ReqFlag::Force] ;
				for( Node t : ecr.targets() ) {
					Job j = t->actual_job_tgt ;
					if      ( !j.active() && add                      ) new_srcs.push_back(t) ;
					else if ( t->is_src()                             ) handle_job(j) ;
					else if ( !force && !(t->makable()&&t->conform()) ) throw ::pair("target was not produced by its offical job"s,t.name()) ;
					else                                                handle_job(j) ;
				}
			}
			bool mod_srcs = !new_srcs.empty() || !old_srcs.empty() ;
			if ( mod_srcs && Req::s_n_reqs() ) throw to_string("cannot ",add?"add":"remove"," frozen source files while running") ;
			// do what is asked
			for( Node t : new_srcs ) { t->mk_src() ; to_do.push_back(t->actual_job_tgt) ; }
			if (!to_do.empty()) {
				Job::s_frozens(add,to_do) ;
				for( Job j : to_do ) {
					if (!add) j->status = Status::Garbage ;
					audit( fd , ro , add?Color::Warning:Color::Note , 0/*lvl*/ , to_string(::setw(w),j->rule->name) , j.name() ) ;
				}
			}
			for( Node t : old_srcs ) t->mk_no_src() ;
			if (mod_srcs) EngineStore::s_invalidate_match() ; // seen from the engine, we have modified sources, we must rematch everything
			return true ;
		}
	}

	static bool/*ok*/ _manual_ok(EngineClosureReq const& ecr) {
		Trace trace("manual_ok",ecr) ;
		Fd                fd = ecr.out_fd  ;
		ReqOptions const& ro = ecr.options ;
		//
		if (_is_mark_glb(ro.key)) {
			::vector<Node> markeds = Node::s_manual_oks() ;
			if (ro.key==ReqKey::Clear) Node::s_clear_manual_oks() ;
			for( Node n : markeds ) audit( fd , ro , ro.key==ReqKey::List?Color::Warning:Color::Note , 0/*lvl*/ , {} , n.name() ) ;
		} else {
			bool           add     = ro.key==ReqKey::Add ;
			::vector<Node> targets ;
			if (ecr.as_job()) targets = ecr.job()->targets() ;
			else              targets = ecr.targets()        ;
			//check
			for( Node t : targets )
				if (t.manual_ok()==add) {
					audit( fd , ro , Color::Err , 0 , to_string("file is ",add?"already":"not"," manual-ok :") , t.name() ) ;
					return false ;
				}
			// do what is asked
			Node::s_manual_oks(add,targets) ;
			for( Node t : targets ) audit( fd , ro , add?Color::Warning:Color::Note , 0/*lvl*/ , {} , t.name() ) ;
		}
		return true ;
	}

	static bool/*ok*/ _no_trigger(EngineClosureReq const& ecr) {
		Trace trace("manual_ok",ecr) ;
		Fd                fd = ecr.out_fd  ;
		ReqOptions const& ro = ecr.options ;
		//
		if (_is_mark_glb(ro.key)) {
			::vector<Node> markeds = Node::s_no_triggers() ;
			if (ro.key==ReqKey::Clear) Node::s_clear_manual_oks() ;
			for( Node n : markeds ) audit( fd , ro , ro.key==ReqKey::List?Color::Warning:Color::Note , 0/*lvl*/ , {} , n.name() ) ;
		} else {
			bool           add     = ro.key==ReqKey::Add ;
			::vector<Node> targets ;
			if (ecr.as_job()) targets = ecr.job()->targets() ;
			else              targets = ecr.targets()        ;
			//check
			for( Node t : targets )
				if (t.no_trigger()==add) {
					audit( fd , ro , Color::Err , 0 , to_string("file is ",add?"already":"not"," manual-ok :") , t.name() ) ;
					return false ;
				}
			// do what is asked
			Node::s_no_triggers(add,targets) ;
			for( Node t : targets ) audit( fd , ro , add?Color::Warning:Color::Note , 0/*lvl*/ , {} , t.name() ) ;
		}
		return true ;
	}

	static void _send_node( Fd fd , ReqOptions const& ro , bool always , Bool3 hide , ::string const& pfx , Node node , DepDepth lvl=0 ) {
		Color color = Color::None ;
		if      ( hide==Yes                                         ) color =                         Color::HiddenNote ;
		else if ( !node->has_actual_job() && !FileInfo(node.name()) ) color = hide==No ? Color::Err : Color::HiddenNote ;
		else if ( node->err()                                       ) color =            Color::Err                     ;
		//
		if ( always || color!=Color::HiddenNote ) audit( fd , ro , color , lvl , pfx , node.name() ) ;
	}

	static void _send_job( Fd fd , ReqOptions const& ro , Bool3 show_deps , bool hide , Job job , DepDepth lvl=0 ) {
		Color color = Color::None ;
		Rule  rule  = job->rule   ;
		if      (hide                   ) color = Color::HiddenNote ;
		else if (job->status==Status::Ok) color = Color::Ok         ;
		else if (job->frozen()          ) color = Color::Warning    ;
		else                              color = Color::Err        ;
		audit( fd , ro , color , lvl , rule->name , job.name() ) ;
		if (show_deps==No) return ;
		size_t    w       = 0 ;
		::umap_ss rev_map ;
		for( auto const& [k,d] : rule->deps_attrs.eval(job->simple_match()) ) {
			w                = ::max( w , k.size() ) ;
			rev_map[d.first] = k                     ;
		}
		for( NodeIdx d=0 ; d<job->deps.size() ; d++ ) {
			Dep const& dep     = job->deps[d]                                                                           ;
			bool       cdp     = d  >0                && dep           .parallel                                        ;
			bool       ndp     = d+1<job->deps.size() && job->deps[d+1].parallel                                        ;
			::string   dep_key = dep.dflags[Dflag::Static] ? rev_map.at(dep.name()) : ""s                               ;
			::string   pfx     = to_string( dep.dflags_str() ,' ', dep.accesses_str() , ' ' , ::setw(w) , dep_key ,' ') ;
			if      ( !cdp && !ndp ) pfx.push_back(' ' ) ;
			else if ( !cdp &&  ndp ) pfx.push_back('/' ) ;
			else if (  cdp &&  ndp ) pfx.push_back('|' ) ;
			else                     pfx.push_back('\\') ;
			_send_node( fd , ro , show_deps==Yes , (Maybe&!dep.dflags[Dflag::Required])|hide , pfx , dep , lvl+1 ) ;
		}
	}

	static ::string _mk_cmd( Job j , ReqFlags flags , JobRpcReply const& start , ::string const& dbg_dir , bool redirected ) {
		// add debug prelude if asked to do so
		// try to use stdin/stdout to debug with pdb as much as possible as readline does not work on alternate streams
		//
		// header is not strictly necessary, but at least, it allows editors (e.g. vi) to do syntax coloring
		::string res   = "#!" ;
		bool     first = true ;
		for( ::string const& c : start.interpreter ) {
			if (first) first  = false ;
			else       res   += ' '   ;
			res += c ;
		}
		res+='\n' ;
		//
		res += start.cmd.first ;
		//
		if ( flags[ReqFlag::Debug] && j->rule->cmd.spec.is_python ) {
			::string pdb       = flags[ReqFlag::Graphic]?"pudb":"pdb"   ;
			::string r         = redirected             ?"True":"False" ;
			size_t   open_pos  = start.cmd.second.find ('(')            ;
			size_t   close_pos = start.cmd.second.rfind(')')            ;
			::string run_call  = start.cmd.second.substr(0,open_pos)    ; if (close_pos>open_pos+1) run_call = ','+start.cmd.second.substr(open_pos+1,close_pos) ;
			res += "import lmake_runtime\n" ;
			//
			res += "lmake_runtime.deps = (\n" ;                                // generate deps that debugger can use to pre-populate browser
			bool first = true ;
			for( Dep const& d : j->deps ) {
				if (d->crc==Crc::None) continue ;                              // we are only interested in existing deps as other ones are of marginal interest
				if (first) first  = false ;
				else       res   += ','   ;
				res += to_string('\t',mk_py_str(d.name()),'\n') ;
			}
			res += ")\n" ;
			//
			res += to_string("lmake_runtime.run_",pdb,'(',mk_py_str(dbg_dir),',',r,',',run_call,")\n") ;
		} else {
			res += start.cmd.second ;
		}
		return res ;
	}

	static ::string _mk_script( Job j , JobInfoStart const& report_start , ::string const& dbg_dir , ReqFlags flags , bool with_cmd ) {
		JobRpcReply const& start   = report_start.start ;
		AutodepEnv const&  ade     = start.autodep_env  ;
		::string           abs_cwd = *g_root_dir        ;
		if (!start.cwd_s.empty()) {
			append_to_string(abs_cwd,'/',start.cwd_s) ;
			abs_cwd.pop_back() ;
		}
		Rule::SimpleMatch             match           = j->simple_match()                             ;
		::string                      script          = "#!/bin/bash\n"                               ;
		::pair<vector_s,vector<Node>> targets_to_wash = j->targets_to_wash(match)                     ;
		::vector_s const&             to_wash         = targets_to_wash.first                         ;
		bool                          is_python       = j->rule->cmd.spec.is_python                   ;
		bool                          dbg             = flags[ReqFlag::Debug]                         ;
		bool                          redirected      = !start.stdin.empty() || !start.stdout.empty() ;
		::uset_s                      to_report       ;                                                 for( Node t : targets_to_wash.second ) to_report.insert(t.name()) ;
		//
		for( ::string const& tn : to_wash ) if (!to_report.contains(tn)) append_to_string( script , "rm -f " , tn , '\n' ) ;
		if (!to_report.empty()) {
			script += "( set -x\n" ;
			for( ::string const& tn : to_wash ) if (to_report.contains(tn)) append_to_string( script , "rm -f " , tn , '\n' ) ;
			script += ")\n" ;
		}
		for( ::string const& d : match.target_dirs() ) append_to_string( script , "mkdir -p " , d , '\n' ) ;
		//
		::string tmp_dir ;
		if (dbg_dir.empty()) {
			tmp_dir = mk_abs(ade.tmp_dir,*g_root_dir+'/') ;
			if (!start.keep_tmp)
				for( auto&& [k,v] : start.env )
					if ( k=="TMPDIR" && v!=EnvPassMrkr )
						tmp_dir = env_decode(::string(v)) ;
		} else {
			tmp_dir = to_string(*g_root_dir,'/',dbg_dir,"/tmp") ;
		}
		//
		append_to_string( script , "export     TMPDIR="   , mk_shell_str(tmp_dir)   , '\n'    ) ;
		append_to_string( script , "rm -rf   \"$TMPDIR\""                           , '\n'    ) ;
		append_to_string( script , "mkdir -p \"$TMPDIR\""                           , '\n'    ) ;
		append_to_string( script , "exec env -i"    ,                                 " \\\n" ) ;
		append_to_string( script , "\tROOT_DIR="    , mk_shell_str(*g_root_dir)     , " \\\n" ) ;
		append_to_string( script , "\tSEQUENCE_ID=" , report_start.pre_start.seq_id , " \\\n" ) ;
		append_to_string( script , "\tSMALL_ID="    , start.small_id                , " \\\n" ) ;
		append_to_string( script , "\tTMPDIR="      , "\"$TMPDIR\""                 , " \\\n" ) ;
		for( auto&& [k,v] : start.env ) {
			if (k=="TMPDIR"   ) continue ;
			if (v==EnvPassMrkr) append_to_string(script,'\t',k,"=\"$",k,'"'                             ," \\\n") ;
			else                append_to_string(script,'\t',k,'=',mk_shell_str(env_decode(::string(v)))," \\\n") ;
		}
		if ( dbg || ade.auto_mkdir || !ade.tmp_view.empty() ) {                                                        // in addition of dbg, autodep may be needed for functional reasons
			/**/                               append_to_string( script , *g_lmake_dir,"/bin/autodep"        , ' ' ) ;
			if      ( dbg )                    append_to_string( script , "-s " , mk_snake(ade.lnk_support)  , ' ' ) ;
			else                               append_to_string( script , "-s " , "none"                     , ' ' ) ; // dont care about deps
			/**/                               append_to_string( script , "-m " , mk_snake(start.method   )  , ' ' ) ;
			if      ( !dbg                   ) append_to_string( script , "-o " , "/dev/null"                , ' ' ) ;
			else if ( !dbg_dir.empty()       ) append_to_string( script , "-o " , dbg_dir,"/accesses"        , ' ' ) ;
			if      ( ade.auto_mkdir         ) append_to_string( script , "-d"                               , ' ' ) ;
			if      ( dbg && ade.ignore_stat ) append_to_string( script , "-i"                               , ' ' ) ;
			if      ( !ade.tmp_view.empty()  ) append_to_string( script , "-t " , mk_shell_str(ade.tmp_view) , ' ' ) ;
		}
		for( ::string const& c : start.interpreter ) {                           append_to_string( script , mk_shell_str(c) , ' '                                               ) ; }
		if ( dbg && !is_python                     ) {                           append_to_string( script , "-x "                                                               ) ; }
		if ( with_cmd                              ) { SWEAR(!dbg_dir.empty()) ; append_to_string( script , dbg_dir,"/cmd"                                                      ) ; }
		else                                         {                           append_to_string( script , "-c \\\n" , mk_shell_str(_mk_cmd(j,flags,start,dbg_dir,redirected)) ) ; }
		//
		if      ( !start.stdout.empty()            ) append_to_string( script , " > " , mk_shell_str(start.stdout) ) ;
		if      ( !start.stdin .empty()            ) append_to_string( script , " < " , mk_shell_str(start.stdin ) ) ;
		else if ( !dbg || !is_python || redirected ) append_to_string( script , " < /dev/null"                     ) ;
		script += '\n' ;
		return script ;
	}

	static JobTgt _job_from_target( Fd fd , ReqOptions const& ro , Node target ) {
		JobTgt jt = target->actual_job_tgt ;
		if (!jt.active()) {
			/**/                             if (!target->makable()) goto NoJob ;
			jt = target->conform_job_tgt() ; if (!jt.active()      ) goto NoJob ;
		}
		Trace("target",target,jt) ;
		return jt ;
	NoJob :
		audit( fd , ro , Color::Err  , 0 , "target not built"                ) ;
		audit( fd , ro , Color::Note , 1 , "consider : lmake", target.name() ) ;
		return {} ;
	}

	static bool/*ok*/ _debug(EngineClosureReq const& ecr) {
		Trace trace("debug") ;
		Fd                fd = ecr.out_fd  ;
		ReqOptions const& ro = ecr.options ;
		SWEAR(ro.flags[ReqFlag::Debug],ro) ;
		//
		Job job ;
		if (ecr.as_job()) {
			job = ecr.job() ;
		} else {
			::vector<Node> targets = ecr.targets() ;
			if (targets.size()!=1) throw "can only debug a single target"s ;
			job = _job_from_target(fd,ro,ecr.targets()[0]) ;
		}
		if (!job                   ) throw "no job found"s                                    ;
		if (job->rule->is_special()) throw to_string("cannot debug ",job->rule->name," jobs") ;
		//
		JobInfoStart report_start ;
		try         { ::ifstream job_stream{job->ancillary_file() } ; deserialize(job_stream,report_start) ; }
		catch (...) { audit( fd , ro , Color::Err , 0 , "no info available" ) ; return false ;               }
		//
		JobRpcReply const& start       = report_start.start                                                       ;
		bool               redirected  = !start.stdin.empty() || !start.stdout.empty()                            ;
		::string           dbg_dir     = job->ancillary_file(AncillaryTag::Dbg)                                   ;
		::string           script_file = dbg_dir+"/script"                                                        ;
		::string           cmd_file    = dbg_dir+"/cmd"                                                           ;
		::string           script      = _mk_script( job , report_start , dbg_dir , ro.flags , true/*with_cmd*/ ) ;
		::string           cmd         = _mk_cmd( job , ro.flags , start , dbg_dir , redirected )                 ;
		//
		make_dir(dbg_dir) ;
		OFStream(script_file) << script ; ::chmod(script_file.c_str(),0755) ;  // make executable
		OFStream(cmd_file   ) << cmd    ; ::chmod(cmd_file   .c_str(),0755) ;  // .
		//
		::vector<Node>    jts   ;
		Rule::SimpleMatch match = job->simple_match() ;                               // match holds static target names
		for( ::string const& tn : match.static_targets() ) jts.push_back(Node(tn)) ;
		for( Node t             : job->star_targets      ) jts.push_back(     t  ) ;
		Node::s_manual_oks(true/*add*/,jts) ;
		//
		audit( fd , ro , Color::None , 0 , script_file ) ;
		return true ;
	}

	static bool/*ok*/ _forget(EngineClosureReq const& ecr) {
		ReqOptions const& ro = ecr.options ;
		bool              ok = true        ;
		switch (ro.key) {
			case ReqKey::None :
				if (ecr.as_job()) {
					Job j = ecr.job() ;
					if (!j) throw "job not found"s ;
					ok = j->forget( ro.flags[ReqFlag::Targets] , ro.flags[ReqFlag::Deps] ) ;
				} else {
					for( Node t : ecr.targets() ) ok &= t->forget( ro.flags[ReqFlag::Targets] , ro.flags[ReqFlag::Deps] ) ;
				}
			break ;
			case ReqKey::Error :
				g_store.invalidate_exec(true/*cmd_ok*/) ;
			break ;
			case ReqKey::Resources :
				for( Rule r : Rule::s_lst() ) {
					if (r->cmd_gen==r->rsrcs_gen) continue ;
					r.rule_data().cmd_gen = r->rsrcs_gen ;
					r.stamp() ;                                                                  // we have modified rule, we must record it to make modif persistent
					audit( ecr.out_fd , ro , Color::Note , 0 , to_string("refresh ",r->name) ) ;
				}
			break ;
			default : FAIL(ro.key) ;
		}
		return ok ;
	}

	static bool/*ok*/ _mark(EngineClosureReq const& ecr) {
		if (ecr.options.flags[ReqFlag::Freeze   ]) return _freeze    (ecr) ;
		if (ecr.options.flags[ReqFlag::ManualOk ]) return _manual_ok (ecr) ;
		if (ecr.options.flags[ReqFlag::NoTrigger]) return _no_trigger(ecr) ;
		throw "no mark specified"s ;
	}

	static void _show_job( Fd fd , ReqOptions const& ro , Job job , DepDepth lvl=0 ) {
		Trace trace("show_job",ro.key,job) ;
		Rule             rule         = job->rule               ;
		::ifstream       job_stream   { job->ancillary_file() } ;
		JobInfoStart     report_start ;
		JobInfoEnd       report_end   ;
		bool             has_start    = false                   ;
		bool             has_end      = false                   ;
		JobDigest const& digest       = report_end.end.digest   ;
		try { deserialize(job_stream,report_start) ; has_start = true ; } catch (...) { goto Go ; }
		try { deserialize(job_stream,report_end  ) ; has_end   = true ; } catch (...) { goto Go ; }
	Go :
		switch (ro.key) {
			case ReqKey::Backend    :
			case ReqKey::Cmd        :
			case ReqKey::Env        :
			case ReqKey::ExecScript :
			case ReqKey::Info       :
			case ReqKey::Stderr     :
			case ReqKey::Stdout     : {
				if (rule->is_special()) {
					switch (ro.key) {
						case ReqKey::Info :
						case ReqKey::Stderr : {
							_send_job( fd , ro , No/*show_deps*/ , false/*hide*/ , job , lvl ) ;
							audit    ( fd , ro , Color::None , lvl+1 , job->special_stderr() ) ;
						} break ;
						case ReqKey::Cmd        :
						case ReqKey::ExecScript :
						case ReqKey::Stdout     :
							_send_job( fd , ro , No/*show_deps*/ , false/*hide*/ , job , lvl              ) ;
							audit    ( fd , ro , Color::Err , lvl+1 , "no "+mk_snake(ro.key)+" available" ) ;
						break ;
						default : FAIL(ro.key) ;
					}
				} else {
					JobRpcReq   const& pre_start  = report_start.pre_start                          ;
					JobRpcReply const& start      = report_start.start                              ;
					bool               redirected = !start.stdin.empty() || !start.stdout.empty()   ;
					switch (ro.key) {
						case ReqKey::Env : {
							size_t w = 0 ;
							if (!has_start) { audit( fd , ro , Color::Err , lvl , "no info available" ) ; break ; }
							for( auto const& [k,v] : start.env ) w = max(w,k.size()) ;
							for( auto const& [k,v] : start.env ) audit( fd , ro , Color::None , lvl , to_string(::setw(w),k," : ",env_decode(::string(v))) ) ;
						} break ;
						case ReqKey::ExecScript :
							if (!has_start) { audit( fd , ro , Color::Err , 0 , "no info available" ) ; break ; }
							audit( fd , ro , Color::None , lvl , _mk_script(job,report_start,ro.flag_args[+ReqFlag::Debug],ro.flags,false/*with_cmd*/) ) ;
						break ;
						case ReqKey::Cmd : {
							if (!has_start) { audit( fd , ro , Color::Err , lvl , "no info available" ) ; break ; }
							audit( fd , ro , Color::None , lvl , _mk_cmd( job , ro.flags , start , {} , redirected ) ) ;
						} break ;
						case ReqKey::Stdout :
							if (!has_end ) { audit( fd , ro , Color::Err , lvl , "no info available" ) ; break ; }
							_send_job( fd , ro , No/*show_deps*/ , false/*hide*/ , job , lvl ) ;
							audit    ( fd , ro , Color::None , lvl+1 , digest.stdout )         ;
						break ;
						case ReqKey::Stderr :
							if (!has_end) { audit( fd , ro , Color::Err , lvl , "no info available" ) ; break ; }
							_send_job( fd , ro , No/*show_deps*/ , false/*hide*/ , job , lvl ) ;
							for( auto const& [pfx,n] : digest.analysis_err ) audit( fd , ro , Color::Note , lvl+1 , pfx , n       ) ;
							/**/                                             audit( fd , ro , Color::None , lvl+1 , digest.stderr ) ;
						break ;
						case ReqKey::Backend :
							if (!has_end ) { audit( fd , ro , Color::Err , lvl , "no info available" ) ; break ; }
							_send_job( fd , ro , No/*show_deps*/ , false/*hide*/ , job , lvl ) ;
							audit    ( fd , ro , Color::None , lvl+1 , report_end.backend_msg ) ;
						break ;
						case ReqKey::Info : {
							::string ids      = to_string( "job=",pre_start.job , " , small=",start.small_id ) ;
							bool     has_host = report_start.host!=NoSockAddr                                  ;
							if (pre_start.seq_id) append_to_string(ids," , seq=",pre_start.seq_id) ;             // there may be no seq_id if job hit the cache
							//
							_send_job( fd , ro , No/*show_deps*/ , false/*hide*/ , job , lvl ) ;
							if (has_start) {
								::pair_s<NodeIdx> reason = report_start.submit_attrs.reason.str() ;
								if (+reason.second) {
									bool err = report_start.submit_attrs.reason.err() ;
									_send_node( fd , ro , true/*always*/ , Maybe&!err/*hide*/ , to_string("reason         : ",reason.first," :") , Node(reason.second).name() , lvl+1 ) ;
								} else {
									audit( fd , ro , Color::None , lvl+1 , "reason         : "+reason.first ) ;
								}
							}
							if (has_host) audit( fd , ro , Color::None , lvl+1 , to_string("host           : ",SockFd::s_host(report_start.host)) ) ;
							if (has_start) {
								static constexpr AutodepMethod AdMD = AutodepMethod::Dflt ;
								//
								JobInfoStart const& rs     = report_start                                                             ;
								size_t              cwd_sz = rs.start.cwd_s.size()                                                    ;
								::string            bem    = rs.backend_msg.empty() ? ::string() : to_string(" (",rs.backend_msg,')') ;
								//
								audit( fd , ro , Color::None , lvl+1 , to_string("backend        : ",mk_snake(rs.submit_attrs.tag),bem                      ) ) ;
								audit( fd , ro , Color::None , lvl+1 , to_string("id's           : ",ids                                                    ) ) ;
								audit( fd , ro , Color::None , lvl+1 , to_string("tmp dir        : ",rs.start.autodep_env.tmp_dir                           ) ) ;
								audit( fd , ro , Color::None , lvl+1 , to_string("scheduling     : ",rs.eta.str()," - ",rs.submit_attrs.pressure.short_str()) ) ;
								//
								if ( rs.submit_attrs.live_out        ) audit( fd , ro , Color::None , lvl+1 , to_string("live_out       : true"                              ) ) ;
								if (!rs.start.chroot.empty()         ) audit( fd , ro , Color::None , lvl+1 , to_string("chroot         : ",rs.start.chroot                  ) ) ;
								if (!rs.start.cwd_s .empty()         ) audit( fd , ro , Color::None , lvl+1 , to_string("cwd            : ",rs.start.cwd_s.substr(0,cwd_sz-1)) ) ;
								if ( rs.start.autodep_env.auto_mkdir ) audit( fd , ro , Color::None , lvl+1 , to_string("auto_mkdir     : true"                              ) ) ;
								if ( rs.start.autodep_env.ignore_stat) audit( fd , ro , Color::None , lvl+1 , to_string("ignore_stat    : true"                              ) ) ;
								if ( rs.start.method!=AdMD           ) audit( fd , ro , Color::None , lvl+1 , to_string("autodep        : ",mk_snake(rs.start.method)        ) ) ;
								if (+rs.start.timeout                ) audit( fd , ro , Color::None , lvl+1 , to_string("timeout        : ",rs.start.timeout.short_str()     ) ) ;
							}
							//
							::map_ss required_rsrcs  ;
							::map_ss allocated_rsrcs = mk_map(report_start.rsrcs) ;
							try {
								Rule::SimpleMatch match ;
								required_rsrcs = mk_map(rule->submit_rsrcs_attrs.eval(job,match).rsrcs) ;
							} catch(::string const&) {}
							//
							if (has_end) {
								bool     overflow = !allocated_rsrcs.contains("mem") || digest.stats.mem>from_string_with_units<size_t>(allocated_rsrcs.at("mem")) ;
								int      ws       = digest.wstatus                                                                                                 ;
								bool     ok       = WIFEXITED(ws) && WEXITSTATUS(ws)==0                                                                            ;
								::string mem_str  = to_string_with_units<'M'>(digest.stats.mem>>20)+'B' ;
								if ( overflow && allocated_rsrcs.contains("mem") ) mem_str += " > "+allocated_rsrcs.at("mem")+'B' ;
								::string rc =
									ok              ? "ok"s
								:	WIFEXITED  (ws) ? to_string("exited "  ,            WEXITSTATUS(ws) )
								:	WIFSIGNALED(ws) ? to_string("signaled ",::strsignal(WTERMSIG   (ws)))
								:	"??"s
								;
								//
								audit( fd , ro ,                         Color::None , lvl+1 , "end date       : "+digest.end_date.str()          ) ;
								audit( fd , ro , !ok     ?Color::Err    :Color::None , lvl+1 , "rc             : "+rc                             ) ;
								audit( fd , ro ,                         Color::None , lvl+1 , "cpu time       : "+digest.stats.cpu  .short_str() ) ;
								audit( fd , ro ,                         Color::None , lvl+1 , "elapsed in job : "+digest.stats.job  .short_str() ) ;
								audit( fd , ro ,                         Color::None , lvl+1 , "elapsed total  : "+digest.stats.total.short_str() ) ;
								audit( fd , ro , overflow?Color::Warning:Color::None , lvl+1 , "used mem       : "+mem_str                        ) ;
							}
							//
							bool   has_required  = !required_rsrcs .empty() ;
							bool   has_allocated = !allocated_rsrcs.empty() ;
							size_t kw            = 0                        ;
							for( auto const& [k,_] : required_rsrcs  ) kw = ::max(kw,k.size()) ;
							for( auto const& [k,_] : allocated_rsrcs ) kw = ::max(kw,k.size()) ;
							if ( has_required || has_allocated ) {
								::string hdr = "resources :" ;
								if      (!has_allocated) hdr = "required " +hdr ;
								else if (!has_required ) hdr = "allocated "+hdr ;
								audit( fd , ro , Color::None , lvl+1 , hdr ) ;
								if      (!has_required                  ) for( auto const& [k,v] : allocated_rsrcs ) audit( fd , ro , Color::None , lvl+2 , to_string(::setw(kw),k," : ",v) ) ;
								else if (!has_allocated                 ) for( auto const& [k,v] : required_rsrcs  ) audit( fd , ro , Color::None , lvl+2 , to_string(::setw(kw),k," : ",v) ) ;
								else if (required_rsrcs==allocated_rsrcs) for( auto const& [k,v] : required_rsrcs  ) audit( fd , ro , Color::None , lvl+2 , to_string(::setw(kw),k," : ",v) ) ;
								else {
									for( auto const& [k,rv] : required_rsrcs ) {
										if (!allocated_rsrcs.contains(k)) { audit( fd , ro , Color::None , lvl+2 , to_string(::setw(kw),k,"(required )"," : ",rv) ) ; continue ; }
										::string const& av = allocated_rsrcs.at(k) ;
										if (rv==av                      ) { audit( fd , ro , Color::None , lvl+2 , to_string(::setw(kw),k,"           "," : ",rv) ) ; continue ; }
										/**/                                audit( fd , ro , Color::None , lvl+2 , to_string(::setw(kw),k,"(required )"," : ",rv) ) ;
										/**/                                audit( fd , ro , Color::None , lvl+2 , to_string(::setw(kw),k,"(allocated)"," : ",av) ) ;
									}
									for( auto const& [k,av] : allocated_rsrcs )
										if (!required_rsrcs.contains(k))    audit( fd , ro , Color::None , lvl+2 , to_string(::setw(kw),k,"(allocated)"," : ",av) ) ;
								}
							}
						} break ;
						default : FAIL(ro.key) ;
					}
				}
			} break ;
			case ReqKey::Deps :
				_send_job( fd , ro , Maybe|ro.flags[ReqFlag::Verbose] , false/*hide*/ , job , lvl ) ;
			break ;
			case ReqKey::Targets : {
				Rule::FullMatch match      = job->full_match() ;
				::string        unexpected = "<unexpected>"    ;
				size_t          wk         = 0                 ;
				for( auto const& [tn,td] : digest.targets ) {
					VarIdx ti = match.idx(tn) ;
					wk = ::max( wk , ti==Rule::NoVar?unexpected.size():rule->targets[ti].first.size() ) ;
				}
				for( auto const& [tn,td] : digest.targets ) {
					VarIdx   ti         = match.idx(tn)      ;
					Tflags   stfs       = rule->tflags(ti)   ;
					bool     m          = stfs[Tflag::Match] ;
					::string flags_str  ;
					::string target_key = ti==Rule::NoVar ? unexpected : rule->targets[ti].first ;
					flags_str += m                 ? '-'                : '#'                ;
					flags_str += td.crc==Crc::None ? '!'                : '-'                ;
					flags_str += +td.accesses      ? (td.write?'U':'R') : (td.write?'W':'-') ;
					flags_str += stfs[Tflag::Star] ? '*'                : '-'                ;
					flags_str += ' ' ;
					for( Tflag tf=Tflag::HiddenMin ; tf<Tflag::HiddenMax1 ; tf++ ) flags_str += td.tflags[tf]?TflagChars[+tf]:'-' ;
					_send_node( fd , ro , ro.flags[ReqFlag::Verbose] , Maybe|!m/*hide*/ , to_string( flags_str ,' ', ::setw(wk) , target_key ) , tn , lvl ) ;
				}
			} break ;
			default : FAIL(ro.key) ;
		}
	}

	static bool/*ok*/ _show(EngineClosureReq const& ecr) {
		Trace trace("show",ecr) ;
		Fd                fd = ecr.out_fd  ;
		ReqOptions const& ro = ecr.options ;
		if (ecr.as_job()) {
			_show_job(fd,ro,ecr.job()) ;
			return true ;
		}
		bool           ok      = true          ;
		::vector<Node> targets = ecr.targets() ;
		for( Node target : targets ) {
			trace("target",target) ;
			DepDepth lvl = 0 ;
			if (targets.size()>1) {
				_send_node( fd , ro , true/*always*/ , Maybe/*hide*/ , {} , target ) ;
				lvl++ ;
			}
			JobTgt jt = _job_from_target(fd,ro,target) ; if (!jt) { ok = false ; continue ; }
			switch (ro.key) {
				case ReqKey::Backend    :
				case ReqKey::Cmd        :
				case ReqKey::Env        :
				case ReqKey::ExecScript :
				case ReqKey::Info       :
				case ReqKey::Stderr     :
				case ReqKey::Stdout     :
				case ReqKey::Targets    :
					_show_job(fd,ro,jt,lvl) ;
				break ;
				case ReqKey::Deps    : {
					bool     always      = ro.flags[ReqFlag::Verbose] ;
					::string uphill_name = dir_name(target.name())    ;
					double   prio        = -Infinity                  ;
					if (!uphill_name.empty()) _send_node( fd , ro , always , Maybe/*hide*/ , "U" , Node(uphill_name) , lvl ) ;
					for( JobTgt job_tgt : target->job_tgts ) {
						if (job_tgt->rule->prio<prio) break ;
						if (job_tgt==jt ) { prio = job_tgt->rule->prio ; continue ; }   // actual job is output last as this is what user views first
						bool hide = !job_tgt.produces(target) ;
						if      (always) _send_job( fd , ro , Yes   , hide          , job_tgt , lvl ) ;
						else if (!hide ) _send_job( fd , ro , Maybe , false/*hide*/ , job_tgt , lvl ) ;
					}
					if (prio!=-Infinity) _send_job( fd , ro , always?Yes:Maybe , false/*hide*/ , jt ) ; // actual job is output last as this is what user views first
				} break ;
				case ReqKey::InvDeps :
					for( Job job : g_store.job_lst() )
						for( Dep const& dep : job->deps ) {
							if (dep!=target) continue ;
							_send_job( fd , ro , No , false/*hide*/ , job , lvl ) ;
							break ;
						}
				break ;
				default : FAIL(ro.key) ;
			}
		}
		trace(STR(ok)) ;
		return ok ;
	}

	CmdFunc g_cmd_tab[+ReqProc::N] ;
	static bool _inited = (                                                    // PER_CMD : add an entry to point to the function actually executing your command (use show as a template)
		g_cmd_tab[+ReqProc::Debug ] = _debug
	,	g_cmd_tab[+ReqProc::Forget] = _forget
	,	g_cmd_tab[+ReqProc::Mark  ] = _mark
	,	g_cmd_tab[+ReqProc::Show  ] = _show
	,	true
	) ;

}
