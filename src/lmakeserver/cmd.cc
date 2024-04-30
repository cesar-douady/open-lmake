// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "cmd.hh"

#include <linux/binfmts.h>
#include <regex>

using namespace Disk ;

namespace Engine {

	static bool _is_mark_glb(ReqKey key) {
		switch (key) {
			case ReqKey::Clear  :
			case ReqKey::List   : return true  ;
			case ReqKey::Add    :
			case ReqKey::Delete : return false ;
		DF}
	}

	static bool/*ok*/ _freeze(EngineClosureReq const& ecr) {
		Fd                fd = ecr.out_fd  ;
		ReqOptions const& ro = ecr.options ;
		Trace trace("freeze",ecr) ;
		if (_is_mark_glb(ro.key)) {
			::vector<Job > jobs  = Job ::s_frozens() ;
			::vector<Node> nodes = Node::s_frozens() ;
			size_t         w     = 0                 ; for( Job j : jobs ) w = ::max(w,j->rule->name.size()) ;
			if (ro.key==ReqKey::Clear) {
				for( Job  j : jobs  ) j->status = Status::New ;
				for( Node n : nodes ) n->mk_no_src() ;
				Job ::s_clear_frozens() ;
				Node::s_clear_frozens() ;
			}
			for( Job  j : jobs  ) audit( fd , ro , ro.key==ReqKey::List?Color::Warning:Color::Note , to_string(::setw(w),j->rule->name,' ',mk_file(j->name()              )) ) ;
			for( Node n : nodes ) audit( fd , ro , ro.key==ReqKey::List?Color::Warning:Color::Note , to_string(::setw(w),              ' ',mk_file(n->name(),Yes/*exists*/)) ) ;
			return true ;
		} else {
			bool           add   = ro.key==ReqKey::Add ;
			size_t         w     = 3/*src*/            ;
			::string       name  ;
			::string       err   ;
			::vector<Job > jobs  ;
			::vector<Node> nodes ;
			//
			auto handle_job = [&](Job j)->void {
				if (add) {
					if (!j.active()) throw "job not found " +mk_file(j->name()) ;
					if ( j.frozen()) throw "already frozen "+mk_file(j->name()) ;
				} else {
					if (!j.active()) throw "not frozen "+mk_file(j->name()) ;
					if (!j.frozen()) throw "not frozen "+mk_file(j->name()) ;
				}
				if (j->running()) throw "job is running"+mk_file(j->name()) ;
				//
				w = ::max( w , j->rule->name.size() ) ;
				jobs.push_back(j) ;
			} ;
			auto handle_node = [&](Node n)->void {
				if      ( add==n.frozen()         ) { ::string nn = n->name() ; throw to_string(n.frozen()?"already":"not"," frozen ",mk_file(nn)) ; }
				else if ( add && n->is_src_anti() ) { ::string nn = n->name() ; throw to_string("cannot freeze source/anti "         ,mk_file(nn)) ; }
				//
				nodes.push_back(n) ;
			} ;
			//check
			if (ecr.as_job()) {
				handle_job(ecr.job()) ;
			} else {
				bool force = ro.flags[ReqFlag::Force] ;
				for( Node t : ecr.targets() ) {
					t->set_buildable() ;
					Job j = t->actual_job() ;
					if      ( add && !j.active()                                        ) handle_node(t) ;
					else if ( t->is_src_anti()                                          ) handle_node(t) ;
					else if ( force || (t->status()<=NodeStatus::Makable&&t->conform()) ) handle_job (j) ;
					else {
						Job cj = t->conform_job() ;
						trace("fail",t->buildable,t->conform_idx(),t->status(),cj) ;
						if (+cj) throw to_string("target was produced by ",j->rule->name," instead of ",cj->rule->name," (use -F to override) : ",mk_file(t->name(),Yes/*exists*/)) ;
						else     throw to_string("target was produced by ",j->rule->name,                              " (use -F to override) : ",mk_file(t->name(),Yes/*exists*/)) ;
					}
				}
			}
			bool mod_nodes = +nodes ;
			if ( mod_nodes && Req::s_n_reqs() ) throw to_string("cannot ",add?"add":"remove"," frozen files while running") ;
			// do what is asked
			if (+jobs) {
				trace("jobs",jobs) ;
				Job::s_frozens(add,jobs) ;
				for( Job j : jobs ) {
					if (!add) j->status = Status::New ;
					audit( fd , ro , add?Color::Warning:Color::Note , to_string(::setw(w),j->rule->name,' ',mk_file(j->name())) ) ;
				}
			}
			if (+nodes) {
				trace("nodes",nodes) ;
				Node::s_frozens(add,nodes) ;
				for( Node n : nodes ) if (add) n->mk_src() ; else n->mk_no_src() ;
				Persistent::invalidate_match() ;                                   // seen from the engine, we have modified sources, we must rematch everything
			}
			trace("done") ;
			return true ;
		}
	}

	static bool/*ok*/ _no_trigger(EngineClosureReq const& ecr) {
		Trace trace("_no_trigger",ecr) ;
		Fd                fd = ecr.out_fd  ;
		ReqOptions const& ro = ecr.options ;
		//
		if (_is_mark_glb(ro.key)) {
			::vector<Node> markeds = Node::s_no_triggers() ;
			if (ro.key==ReqKey::Clear) Node::s_clear_no_triggers() ;
			for( Node n : markeds ) audit( fd , ro , ro.key==ReqKey::List?Color::Warning:Color::Note , mk_file(n->name(),Yes/*exists*/) ) ;
		} else {
			bool           add   = ro.key==ReqKey::Add ;
			::vector<Node> nodes ;
			if (ecr.as_job()) nodes = mk_vector<Node>(ecr.job()->targets) ;
			else              nodes = ecr.targets()                       ;
			//check
			for( Node n : nodes )
				if (n.no_trigger()==add) {
					audit( fd , ro , Color::Err , to_string("file is ",add?"already":"not"," no-trigger : ",mk_file(n->name(),Yes/*exists*/)) ) ;
					return false ;
				}
			// do what is asked
			Node::s_no_triggers(add,nodes) ;
			for( Node n : nodes ) audit( fd , ro , add?Color::Warning:Color::Note , mk_file(n->name(),Yes/*exists*/) ) ;
		}
		return true ;
	}

	static void _send_node( Fd fd , ReqOptions const& ro , bool always , Bool3 hide , ::string const& pfx , Node node , DepDepth lvl=0 ) {
		Color color = Color::None ;
		if      ( hide==Yes                                           ) color =                         Color::HiddenNote ;
		else if ( !node->has_actual_job() && !is_target(node->name()) ) color = hide==No ? Color::Err : Color::HiddenNote ;
		else if ( node->ok()==No                                      ) color =            Color::Err                     ;
		//
		if ( always || color!=Color::HiddenNote ) audit( fd , ro , color , to_string(pfx,' ',mk_file(node->name())) , false/*as_is*/ , lvl ) ;
	}

	static void _send_job( Fd fd , ReqOptions const& ro , Bool3 show_deps , bool hide , Job job , DepDepth lvl=0 ) {
		Color color = Color::None ;
		Rule  rule  = job->rule   ;
		if      (hide                   ) color = Color::HiddenNote ;
		else if (job->status==Status::Ok) color = Color::Ok         ;
		else if (job.frozen()           ) color = Color::Warning    ;
		else                              color = Color::Err        ;
		audit( fd , ro , color , to_string(rule->name,' ',mk_file(job->name())) , false/*as_is*/ , lvl ) ;
		if (show_deps==No) return ;
		size_t    w       = 0 ;
		::umap_ss rev_map ;
		for( auto const& [k,d] : rule->deps_attrs.eval(job->simple_match()) ) {
			w                  = ::max( w , k.size() ) ;
			rev_map[d.txt] = k                     ;
		}
		::vector<bool> parallel ;  for( Dep const& d : job->deps ) parallel.push_back(d.parallel) ; // first pass to count deps as they are compressed and size is not known upfront
		NodeIdx d      = 0 ;
		for( Dep const& dep : job->deps ) {
			bool       cdp     = d  >0               && parallel[d  ]                                                   ;
			bool       ndp     = d+1<parallel.size() && parallel[d+1]                                                   ;
			::string   dep_key = dep.dflags[Dflag::Static] ? rev_map.at(dep->name()) : ""s                              ;
			::string   pfx     = to_string( dep.dflags_str() ,' ', dep.accesses_str() , ' ' , ::setw(w) , dep_key ,' ') ;
			if      ( !cdp && !ndp ) pfx.push_back(' ' ) ;
			else if ( !cdp &&  ndp ) pfx.push_back('/' ) ;
			else if (  cdp &&  ndp ) pfx.push_back('|' ) ;
			else                     pfx.push_back('\\') ;
			_send_node( fd , ro , show_deps==Yes , (Maybe&!dep.dflags[Dflag::Required])|hide , pfx , dep , lvl+1 ) ;
			d++ ;
		}
	}

	static ::vmap_ss _mk_env( ::vmap_ss const& env , ::vmap_ss const& dynamic_env ) {
		::umap_ss de  = mk_umap(dynamic_env) ;
		::vmap_ss res ;
		for( auto const& [k,v] : env )
			if      (v!=EnvPassMrkr) res.emplace_back(k,env_decode(::copy(v       ))) ;
			else if (de.contains(k)) res.emplace_back(k,env_decode(::copy(de.at(k)))) ;
		return res ;
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
		if ( start.interpreter.size()>2 || res.size()>BINPRM_BUF_SIZE )                                  // inform user we do not use the sheebang line if it actually does not work ...
			res += "# the sheebang line above is informative only, interpreter is called explicitly\n" ; // ... just so that it gets no headache wondering why it works with a apparently buggy line
		//
		res += start.cmd.first ;
		//
		if ( flags[ReqFlag::Debug] && j->rule->is_python ) {
			::string runner = flags[ReqFlag::Vscode] ? "run_vscode" : flags[ReqFlag::Graphic] ? "run_pudb" : "run_pdb" ;
			//
			size_t   open_pos  = start.cmd.second.find ('(')         ;
			size_t   close_pos = start.cmd.second.rfind(')')         ;
			::string run_call  = start.cmd.second.substr(0,open_pos) ; if (close_pos>open_pos+1) run_call = ','+start.cmd.second.substr(open_pos+1,close_pos) ;
			//
			append_line_to_string( res , "lmake_dbg = {}\n"                                                               ) ;
			append_line_to_string( res , "exec(open(",mk_py_str(*g_lmake_dir+"/lib/lmake_dbg.py"),").read(),lmake_dbg)\n" ) ;
			append_line_to_string( res , "lmake_dbg['deps'] = (\n"                                                        ) ; // generate deps that debugger can use to pre-populate browser
			bool first = true ;
			for( Dep const& d : j->deps ) {
				if (d->crc==Crc::None) continue ; // we are only interested in existing deps as other ones are of marginal interest
				if (first) first  = false ;
				else       res   += ','   ;
				append_to_string( res , '\t',mk_py_str(d->name()),'\n')  ;
			}
			res += ")\n" ;
			//
			append_line_to_string( res , "lmake_dbg[" , mk_py_str(runner) , "](" , mk_py_str(dbg_dir) , ',' , redirected?"True":"False" , ',' ,run_call ,")\n" ) ;
		} else {
			res += start.cmd.second ;
		}
		return res ;
	}

	static ::string _mk_vscode( Job j , JobInfoStart const& report_start , JobInfoEnd const& report_end , ::string const& dbg_dir , ::vector_s const& vs_ext ) {
		JobRpcReply const& start = report_start.start ;
		::string res =
R"({
	"folders": [
		{ "path" : $g_root_dir }
	]
,	"settings": {
		"files.associations" : {
			"**/script" : "python"
		,	"cmd"       : "python"
		,	"script"    : "python"
		,	"**.py*"    : "python"
		}
	,	"files.exclude" : {
			".vscode/**" : true
		,	".git*/**"   : true
		}
	,	"telemetry.enableTelemetry" : false
	,	"telemetry.telemetryLevel"  : "off"
	}
,	"launch" : {
		"configurations" : [
			{	"name"       : $name
			,	"type"       : "python"
			,	"request"    : "launch"
			,	"program"    : $program
			,	"console"    : "integratedTerminal"
			,	"cwd"        : $g_root_dir
			,	"subProcess" : true
			,	"env" : {
					$env
				}

			}
		,	{
				"type"      : "by-gdb"
			,	"request"   : "attach"
			,	"name"      : "Attach C/C++"
			,	"program"   : $interpreter
			,	"cwd"       : $g_root_dir
			,	"processId" : 0
			}
		]
	}
,	"extensions" : {
		"recommendations" : [
			$extensions
		]
	}
}
)" ;
		::string extensions ;
		bool     first      = true ;
		for ( auto& ext : vs_ext ) {
			if (!first) append_to_string( extensions , "\n\t\t,\t" ) ;
			/**/        append_to_string( extensions , '"',ext,'"' ) ;
			first = false ;
		}
		res = ::regex_replace( res , ::regex("\\$extensions" ) , extensions                                             ) ;
		res = ::regex_replace( res , ::regex("\\$name"       ) , mk_json_str(          j->name()                      ) ) ;
		res = ::regex_replace( res , ::regex("\\$g_root_dir" ) , mk_json_str(          *g_root_dir                    ) ) ;
		res = ::regex_replace( res , ::regex("\\$program"    ) , mk_json_str(to_string(*g_root_dir,'/',dbg_dir,"/cmd")) ) ;
		res = ::regex_replace( res , ::regex("\\$interpreter") , mk_json_str(to_string(start.interpreter[0]          )) ) ;
		//
		::vmap_ss env     = _mk_env(start.env,report_end.end.dynamic_env) ;
		size_t    kw      = 13/*SEQUENCE_ID*/ ; for( auto&& [k,v] : env ) if (k!="TMPDIR") kw = ::max(kw,mk_json_str(k).size()) ;
		::string  env_str ;
		append_to_string( env_str ,                 to_string(::setw(kw),mk_json_str("ROOT_DIR"   ))," : ",mk_json_str(*g_root_dir                              ) ) ;
		append_to_string( env_str , "\n\t\t\t\t,\t",to_string(::setw(kw),mk_json_str("SEQUENCE_ID"))," : ",mk_json_str(to_string(report_start.pre_start.seq_id) ) ) ;
		append_to_string( env_str , "\n\t\t\t\t,\t",to_string(::setw(kw),mk_json_str("SMALL_ID"   ))," : ",mk_json_str(to_string(start.small_id               ) ) ) ;
		append_to_string( env_str , "\n\t\t\t\t,\t",to_string(::setw(kw),mk_json_str("TMPDIR"     ))," : ",mk_json_str(to_string(*g_root_dir,'/',dbg_dir,"/tmp")) ) ;
		for( auto&& [k,v] : env )
			if (k!="TMPDIR") append_to_string ( env_str , "\n\t\t\t\t,\t",to_string(::setw(kw),mk_json_str(k))," : ",mk_json_str(v) ) ;
		res = ::regex_replace( res , ::regex("\\$env") , env_str );
		return res ;
	}

	static ::string _mk_script( Job j , ReqFlags flags , JobInfoStart const& report_start , JobInfoEnd const& report_end ,::string const& dbg_dir , bool with_cmd , ::vector_s const& vs_ext={} ) {
		JobRpcReply const& start   = report_start.start ;
		AutodepEnv  const& ade     = start.autodep_env  ;
		::string           abs_cwd = *g_root_dir        ;
		if (+start.cwd_s) {
			append_to_string(abs_cwd,'/',start.cwd_s) ;
			abs_cwd.pop_back() ;
		}
		Rule::SimpleMatch match = j->simple_match() ;
		//
		for( Node t  : j->targets ) t->set_buildable() ;                                                                       // necessary for pre_actions()
		//
		::pair<vmap<Node,FileAction>,vector<Node>/*warn*/> pre_actions = j->pre_actions(match)         ;
		::string                                           script      = "#!/bin/bash\n"               ;
		bool                                               is_python   = j->rule->is_python            ;
		bool                                               dbg         = flags[ReqFlag::Debug]         ;
		bool                                               redirected  = +start.stdin || +start.stdout ;
		//
		::uset<Node> warn      ; for( auto n     : pre_actions.second )                                  warn     .insert(n) ;
		::uset<Node> to_mkdirs ; for( auto [d,a] : pre_actions.first  ) if (a.tag==FileActionTag::Mkdir) to_mkdirs.insert(d) ;
		//
		append_to_string( script , "cd ",mk_shell_str(*g_root_dir),'\n') ;
		//
		for( auto [_,a] : pre_actions.first )
			if (a.tag==FileActionTag::Uniquify) {
				append_to_string( script ,
					"uniquify() {\n"
					"\tif [ -f \"$1\" -a $(stat -c%h \"$1\" 2>/dev/null||echo 0) -gt 1 ] ; then\n"
					"\t\techo warning : uniquify \"$1\"\n"
					"\t\tmv \"$1\" \"$1.$$\" ; cp -p \"$1.$$\" \"$1\" ; rm -f \"$1.$$\"\n"
					"\tfi\n"
					"}\n"
				) ;
				break ;
			}
		for( auto [t,a] : pre_actions.first ) {
			::string tn = mk_shell_str(t->name()) ;
			switch (a.tag) {
				case FileActionTag::None     :                                                                 break ;
				case FileActionTag::Mkdir    :   append_to_string(script,"mkdir -p ",tn,               '\n') ; break ;
				case FileActionTag::Rmdir    :   append_to_string(script,"rmdir "   ,tn," 2>/dev/null",'\n') ; break ;
				case FileActionTag::Unlnk    : { ::string c = "rm -f "   +tn ; if (warn.contains(t)) append_to_string(script,"echo warning : ",c,">&2 ;") ; append_to_string(script,c,'\n') ; } break ;
				case FileActionTag::Uniquify : { ::string c = "uniquify "+tn ;                                                                              append_to_string(script,c,'\n') ; } break ;
			DF}
		}
		//
		::string tmp_dir ;
		if (!dbg_dir) {
			tmp_dir = mk_abs(ade.tmp_dir,*g_root_dir+'/') ;
			if (!start.keep_tmp)
				for( auto&& [k,v] : start.env )
					if ( k=="TMPDIR" && v!=EnvPassMrkr )
						tmp_dir = env_decode(::copy(v)) ;
		} else {
			tmp_dir = to_string(*g_root_dir,'/',dbg_dir,"/tmp") ;
		}
		//
		append_to_string( script , "export      TMPDIR="  , mk_shell_str(tmp_dir) , '\n' ) ;
		append_to_string( script , "rm -rf   \"$TMPDIR\""                         , '\n' ) ;
		append_to_string( script , "mkdir -p \"$TMPDIR\""                         , '\n' ) ;
		if (flags[ReqFlag::Vscode]) {
			for (auto const& extension : vs_ext )
				append_to_string( script , "code --list-extensions | grep -q '^",extension,"$' || code --install-extension ",extension,'\n' ) ;
			append_to_string( script , "DEBUG_DIR=",mk_shell_str(*g_root_dir+'/'+dbg_dir),'\n'                                          ) ;
			append_to_string( script , "args=()\n"                                                                                      ) ;
			append_to_string( script , "type code | grep -q .vscode-server || args+=( \"--user-data-dir ${DEBUG_DIR}/vscode/user\" )\n" ) ;
			for( Dep const& dep : j->deps )
				if (dep.dflags[Dflag::Static]) append_to_string( script , "args+=( ",mk_shell_str(dep->name()),")\n" ) ; // list dependences file to open in vscode
			append_to_string( script , "args+=(\"${DEBUG_DIR}/cmd\")\n"                          ) ;
			append_to_string( script , "args+=(\"${DEBUG_DIR}/vscode/ldebug.code-workspace\")\n" ) ;
			append_to_string( script , "code -n -w --password-store=basic ${args[@]} &"          ) ;
		} else {
			::vmap_ss env = _mk_env(start.env,report_end.end.dynamic_env) ;
			/**/                                      append_to_string( script , "exec env -i"    ,                                 " \\\n" ) ;
			/**/                                      append_to_string( script , "\tROOT_DIR="    , mk_shell_str(*g_root_dir)     , " \\\n" ) ;
			/**/                                      append_to_string( script , "\tSEQUENCE_ID=" , report_start.pre_start.seq_id , " \\\n" ) ;
			/**/                                      append_to_string( script , "\tSMALL_ID="    , start.small_id                , " \\\n" ) ;
			/**/                                      append_to_string( script , "\tTMPDIR="      , "\"$TMPDIR\""                 , " \\\n" ) ;
			for( auto& [k,v] : env ) if (k!="TMPDIR") append_to_string( script , '\t',k,'='       , mk_shell_str(v)               , " \\\n" ) ;
			if ( dbg || ade.auto_mkdir || +ade.tmp_view ) {                                                                    // in addition of dbg, autodep may be needed for functional reasons
				/**/                               append_to_string( script , *g_lmake_dir,"/bin/autodep"        , ' ' ) ;
				if      ( dbg )                    append_to_string( script , "-s " , snake(ade.lnk_support)     , ' ' ) ;
				else                               append_to_string( script , "-s " , "none"                     , ' ' ) ;     // dont care about deps
				/**/                               append_to_string( script , "-m " , snake(start.method   )     , ' ' ) ;
				if      ( !dbg                   ) append_to_string( script , "-o " , "/dev/null"                , ' ' ) ;
				else if ( +dbg_dir               ) append_to_string( script , "-o " , dbg_dir,"/accesses"        , ' ' ) ;
				if      ( ade.auto_mkdir         ) append_to_string( script , "-d"                               , ' ' ) ;
				if      ( dbg && ade.ignore_stat ) append_to_string( script , "-i"                               , ' ' ) ;
				if      ( +ade.tmp_view          ) append_to_string( script , "-t " , mk_shell_str(ade.tmp_view) , ' ' ) ;
			}
			for( ::string const& c : start.interpreter )                     append_to_string( script , mk_shell_str(c) , ' '                                               ) ;
			if ( dbg && !is_python                     )                     append_to_string( script , "-x "                                                               ) ;
			if ( with_cmd                              ) { SWEAR(+dbg_dir) ; append_to_string( script , dbg_dir,"/cmd"                                                      ) ; }
			else                                                             append_to_string( script , "-c \\\n" , mk_shell_str(_mk_cmd(j,flags,start,dbg_dir,redirected)) ) ;
			//
			if      ( +start.stdout                    ) append_to_string( script , " > " , mk_shell_str(start.stdout) ) ;
			if      ( +start.stdin                     ) append_to_string( script , " < " , mk_shell_str(start.stdin ) ) ;
			else if ( !dbg || !is_python || redirected ) append_to_string( script , " < /dev/null"                     ) ;
		}
		script += '\n' ;
		return script ;
	}

	static Job _job_from_target( Fd fd , ReqOptions const& ro , Node target ) {
		JobTgt job = target->actual_job() ;
		if (!job.active()) {
			/**/                          if (target->status()>NodeStatus::Makable) goto NoJob ;
			job = target->conform_job() ; if (!job.active()                       ) goto NoJob ;
		}
		Trace("target",target,job) ;
		return job ;
	NoJob :
		target->set_buildable() ;
		if (!target->is_src_anti()) {
			audit( fd , ro , Color::Err  , "target not built"                                               ) ;
			audit( fd , ro , Color::Note , "consider : lmake "+mk_file(target->name()) , false/*as_is*/ , 1 ) ;
		}
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
		JobInfo job_info = job->job_info() ;
		if (!job_info.start.start.proc) {
			audit( fd , ro , Color::Err , "no info available" ) ;
			return false ;
		}
		//
		JobRpcReply const& start       = job_info.start.start                    ;
		bool               redirected  = +start.stdin || +start.stdout           ;
		::string           dbg_dir     = job->ancillary_file(AncillaryTag::Dbg)  ;
		::string           script_file = dbg_dir+"/script"                       ;
		::string           cmd_file    = dbg_dir+"/cmd"                          ;
		::string           vscode_file = dbg_dir+"/vscode/ldebug.code-workspace" ;
		//
		::vector_s vs_ext {
			"ms-python.python"
		,	"ms-vscode.cpptools"
		,	"coolchyni.beyond-debug"
		} ;
		//
		::string script = _mk_script( job , ro.flags , job_info.start , job_info.end , dbg_dir , true/*with_cmd*/ , vs_ext ) ;
		::string cmd    = _mk_cmd   ( job , ro.flags , start        ,                  dbg_dir , redirected                ) ;
		::string vscode = _mk_vscode( job ,            job_info.start , job_info.end , dbg_dir ,                    vs_ext ) ;
		//
		OFStream(dir_guard(script_file)) << script ; ::chmod(script_file.c_str(),0755) ; // make executable
		OFStream(dir_guard(cmd_file   )) << cmd    ; ::chmod(cmd_file   .c_str(),0755) ; // .
		OFStream(dir_guard(vscode_file)) << vscode ;
		//
		audit_file( fd , ::move(script_file) ) ;
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
				Persistent::invalidate_exec(true/*cmd_ok*/) ;
			break ;
			case ReqKey::Resources :
				for( Rule r : Rule::s_lst() ) {
					if (r->cmd_gen==r->rsrcs_gen) continue ;
					r.data().cmd_gen = r->rsrcs_gen ;
					r.save() ;                                                                               // we have modified rule, we must record it to make modif persistent
					audit( ecr.out_fd , ro , Color::Note , to_string("refresh ",r->name) , true/*as_is*/ ) ;
				}
			break ;
		DF}
		return ok ;
	}

	static bool/*ok*/ _mark(EngineClosureReq const& ecr) {
		if (ecr.options.flags[ReqFlag::Freeze   ]) return _freeze    (ecr) ;
		if (ecr.options.flags[ReqFlag::NoTrigger]) return _no_trigger(ecr) ;
		throw "no mark specified"s ;
	}

	template<class T> struct Show {
		// cxtors & casts
		Show(                                                  ) = default ;
		Show( Fd fd_ , ReqOptions const& ro_ , DepDepth lvl_=0 ) : fd{fd_} , ro{&ro_} , lvl{lvl_} , verbose{ro_.flags[ReqFlag::Verbose]} {}
		// data
		Fd                fd        ;
		ReqOptions const* ro        = nullptr          ;
		DepDepth          lvl       = 0                ;
		::uset<Job >      job_seen  = {}               ;
		::uset<Node>      node_seen = {}               ;
		::vector<T>       backlog   = {}               ;
		bool              verbose   = false/*garbage*/ ;
	} ;

	struct ShowBom : Show<Node> {
		using Show<Node>::Show ;
		// services
		void show_job(Job job) {
			if (!job_seen.insert(job).second) return ;
			for ( Dep const& dep : job->deps ) show_node(dep) ;
		}
		void show_node(Node node) {
			if (!node_seen.insert(node).second) return ;
			node->set_buildable() ;
			//
			if (!node->is_src_anti()) {
				if (verbose) backlog.push_back(node) ;
				lvl += verbose ;
				for( Job j : node->candidate_job_tgts() ) show_job(j) ;
				lvl -= verbose ;
				if (+backlog) backlog.pop_back() ;
			} else if (node->status()<=NodeStatus::Makable) {
				Color c = node->buildable==Buildable::Src ? Color::None : Color::Warning ;
				DepDepth l = lvl - backlog.size() ;
				for( Node n : backlog ) audit( fd , *ro , Color::HiddenNote , mk_file(n   ->name()) , false/*as_is*/ , l++ ) ;
				/**/                    audit( fd , *ro , c                 , mk_file(node->name()) , false/*as_is*/ , lvl ) ;
				backlog = {} ;
			}
		}
	} ;

	struct ShowRunning : Show<Job> {
		static constexpr BitMap<JobStep> InterestingSteps = { JobStep::Dep/*waiting*/ , JobStep::Queued , JobStep::Exec } ;
		using Show<Job>::Show ;
		// services
		void show_job(Job job) {
			JobStep step = {} ;
			for( Req r : Req::s_reqs_by_start )
				if ( JobStep s = job->c_req_info(r).step() ; InterestingSteps[s] && step!=s ) { // process job as soon as one Req is waiting/running, and this must be coherent
					SWEAR(!step,step,s) ;
					step = s ;
				}
			Color color = {} /*garbage*/ ;
			char  hdr   = '?'/*garbage*/ ;
			switch (step) {
				case JobStep::Dep    :                                   break ;
				case JobStep::Queued : color = Color::Note ; hdr = 'Q' ; break ;
				case JobStep::Exec   : color = Color::None ; hdr = 'R' ; break ;
				default : return ;
			}
			if (!job_seen.insert(job).second) return ;
			//
			switch (step) {
				case JobStep::Dep    :
					if (verbose) backlog.push_back(job) ;
				break ;
				case JobStep::Queued :
				case JobStep::Exec   : {
					SWEAR( lvl>=backlog.size() , lvl , backlog.size() ) ;
					DepDepth l = lvl - backlog.size() ;
					for( Job j : backlog ) audit( fd , *ro , Color::HiddenNote , to_string('W',' ',j  ->rule->name,' ',mk_file(j  ->name())) , false/*as_is*/ , l++ ) ;
					/**/                   audit( fd , *ro , color             , to_string(hdr,' ',job->rule->name,' ',mk_file(job->name())) , false/*as_is*/ , lvl ) ;
					backlog = {} ;
					return ;
				}
			DF}
			lvl += verbose ;
			for( Dep const& dep : job->deps ) show_node(dep) ;
			lvl -= verbose ;
			if (+backlog) backlog.pop_back() ;
		}
		void show_node(Node node) {
			for( Req r : Req::s_reqs_by_start )
				if ( NodeReqInfo const& cri = node->c_req_info(r) ; cri.waiting() ) {           // process node as soon as one Req is waiting
					if (!node_seen.insert(node).second) return ;
					for( Job j : node->conform_job_tgts(cri) ) show_job(j) ;
					return ;
				}
		}
	} ;

	static void _show_job( Fd fd , ReqOptions const& ro , Job job , DepDepth lvl=0 ) {
		Trace trace("show_job",ro.key,job) ;
		Rule             rule         = job->rule                  ;
		JobInfo          job_info     = job->job_info()            ;
		bool             has_start    = +job_info.start.start.proc ;
		bool             has_end      = +job_info.end  .end  .proc ;
		bool             verbose      = ro.flags[ReqFlag::Verbose] ;
		JobDigest const& digest       = job_info.end.end.digest    ;
		switch (ro.key) {
			case ReqKey::Cmd        :
			case ReqKey::Env        :
			case ReqKey::ExecScript :
			case ReqKey::Info       :
			case ReqKey::Stderr     :
			case ReqKey::Stdout     : {
				if (rule->is_special()) {
					switch (ro.key) {
						case ReqKey::Info   :
						case ReqKey::Stderr : {
							_send_job( fd , ro , No/*show_deps*/ , false/*hide*/ , job  , lvl   ) ;
							audit    ( fd , ro , job->special_stderr() , false/*as_is*/ , lvl+1 ) ;
						} break ;
						case ReqKey::Cmd        :
						case ReqKey::Env        :
						case ReqKey::ExecScript :
						case ReqKey::Stdout     :
							_send_job( fd , ro , No/*show_deps*/ , false/*hide*/ , job                                    , lvl   ) ;
							audit    ( fd , ro , Color::Err , to_string("no ",snake(ro.key)," available") , true/*as_is*/ , lvl+1 ) ;
						break ;
					DF}
				} else {
					JobRpcReq   const& pre_start  = job_info.start.pre_start      ;
					JobRpcReply const& start      = job_info.start.start          ;
					JobRpcReq   const& end        = job_info.end  .end            ;
					bool               redirected = +start.stdin || +start.stdout ;
					//
					if (pre_start.job) SWEAR(pre_start.job==+job,pre_start.job,+job) ;
					//
					switch (ro.key) {
						case ReqKey::Env : {
							if (!has_start) { audit( fd , ro , Color::Err , "no info available" , true/*as_is*/ , lvl ) ; break ; }
							::vmap_ss env = _mk_env(start.env,end.dynamic_env) ;
							size_t    w   = 0                                   ;
							for( auto const& [k,v] : env ) w = ::max(w,k.size()) ;
							for( auto const& [k,v] : env ) audit( fd , ro , to_string(::setw(w),k," : ",v) , true/*as_is*/ , lvl ) ;
						} break ;
						case ReqKey::ExecScript : //!                                                                                                                           as_is
							if (!has_start) audit( fd , ro , Color::Err , "no info available"                                                                                  , true , lvl ) ;
							else            audit( fd , ro ,              _mk_script(job,ro.flags,job_info.start,job_info.end,ro.flag_args[+ReqFlag::Debug],false/*with_cmd*/) , true , lvl ) ;
						break ;
						case ReqKey::Cmd : //!                                                                       as_is
							if (!has_start) audit( fd , ro , Color::Err , "no info available"                       , true , lvl ) ;
							else            audit( fd , ro ,              _mk_cmd(job,ro.flags,start,{},redirected) , true , lvl ) ;
						break ;
						case ReqKey::Stdout :
							if (!has_end) { audit( fd , ro , Color::Err , "no info available" , true/*as_is*/ , lvl ) ; break ; }
							_send_job( fd , ro , No/*show_deps*/ , false/*hide*/ , job , lvl   ) ;
							audit    ( fd , ro , digest.stdout                         , lvl+1 ) ;
						break ;
						case ReqKey::Stderr :
							if (!has_end && !(has_start&&verbose) ) { audit( fd , ro , Color::Err , "no info available" , true/*as_is*/ , lvl ) ; break ; }
							_send_job( fd , ro , No/*show_deps*/ , false/*hide*/ , job , lvl ) ;
							if (has_start) {
								if (verbose) audit( fd , ro , Color::Note , pre_start.msg  , false/*as_is*/  , lvl+1 ) ;
							}
							if (has_end) { //!                                                as_is
								if (verbose) audit( fd , ro , Color::Note , end.msg , false , lvl+1 ) ;
								/**/         audit( fd , ro ,               digest.stderr   , true  , lvl+1 ) ;
							}
						break ;
						case ReqKey::Info : {
							struct Entry {
								::string txt   ;
								Color    color = Color::None ;
								bool     as_is = false       ;
							} ;
							bool            porcelaine = ro.flags[ReqFlag::Porcelaine] ;
							::vmap_s<Entry> tab        ;
							auto push_entry = [&]( const char* k , ::string const& v , Color c=Color::None , bool as_is=false )->void {
								tab.emplace_back( k , Entry{v,c,as_is} ) ;
							} ;
							//
							::string ids ;
							if (porcelaine) {
								ids = to_string("{ 'job':",+job) ;
								if (has_start) {
									if (start    .small_id) append_to_string(ids," , 'small':",start    .small_id) ;
									if (pre_start.seq_id  ) append_to_string(ids," , 'seq':"  ,pre_start.seq_id  ) ;
								}
								ids += " }" ;
							} else {
								ids = to_string("job=",+job) ;
								if (has_start) {
									if (start    .small_id) append_to_string(ids," , small=",start    .small_id) ;
									if (pre_start.seq_id  ) append_to_string(ids," , seq="  ,pre_start.seq_id  ) ;
								}
							}
							//
							push_entry("ids",ids,Color::None,true/*as_is*/) ;
							if ( Node n=job->asking ; +n ) {
								while ( +n->asking && n->asking.is_a<Node>() ) n = Node(n->asking) ;
								if (+n->asking) push_entry("required by",localize(mk_file(Job(n->asking)->name()),ro.startup_dir_s)) ;
								else            push_entry("required by",localize(mk_file(    n         ->name()),ro.startup_dir_s)) ;
							}
							if (has_start) {
								JobInfoStart const& rs       = job_info.start                             ;
								SubmitAttrs  const& sa       = rs.submit_attrs                            ;
								::string            cwd      = start.cwd_s.substr(0,start.cwd_s.size()-1) ;
								::string            tmp_dir  = start.autodep_env.tmp_dir                  ;
								::string            pressure = sa.pressure.short_str()                    ;
								//
								if (!start.keep_tmp)
									for( auto const& [k,v] : start.env )
										if (k=="TMPDIR") { tmp_dir = v==EnvPassMrkr ? "..." : v ; break ; }
								//
								if (+sa.reason         ) push_entry( "reason" , localize(reason_str(sa.reason) , ro.startup_dir_s) ) ;
								if (rs.host!=NoSockAddr) push_entry( "host"   , SockFd::s_host(rs.host)                            ) ;
								//
								if (+rs.eta) {
									if (porcelaine) push_entry( "scheduling" , to_string("( ",mk_py_str(rs.eta.str())," , ",double(sa.pressure)           ," )") , Color::None,true/*as_is*/ ) ;
									else            push_entry( "scheduling" , to_string(               rs.eta.str() ," - ",       sa.pressure.short_str()     )                             ) ;
								}
								//
								if (+tmp_dir                      ) push_entry( "tmp dir"     , localize(mk_file(tmp_dir),ro.startup_dir_s) ) ;
								if (+start.autodep_env.tmp_view   ) push_entry( "tmp view"    , start.autodep_env.tmp_view                  ) ;
								if ( sa.live_out                  ) push_entry( "live_out"    , "true"                                      ) ;
								if (+start.chroot                 ) push_entry( "chroot"      , start.chroot                                ) ;
								if (+start.cwd_s                  ) push_entry( "cwd"         , cwd                                         ) ;
								if ( start.autodep_env.auto_mkdir ) push_entry( "auto_mkdir"  , "true"                                      ) ;
								if ( start.autodep_env.ignore_stat) push_entry( "ignore_stat" , "true"                                      ) ;
								/**/                                push_entry( "autodep"     , snake_str(start.method)                     ) ;
								if (+start.timeout                ) push_entry( "timeout"     , start.timeout.short_str()                   ) ;
								if (sa.tag!=BackendTag::Local     ) push_entry( "backend"     , snake_str(sa.tag)                           ) ;
							}
							//
							::map_ss allocated_rsrcs = mk_map(job_info.start.rsrcs) ;
							::map_ss required_rsrcs  ;
							try {
								Rule::SimpleMatch match ;
								required_rsrcs = mk_map(rule->submit_rsrcs_attrs.eval(job,match,&::ref(vmap_s<DepDigest>())).rsrcs) ; // dont care about deps
							} catch(::pair_ss const&) {}
							//
							if (has_end) {
								push_entry( "end date" , digest.end_date.str()                                                                                          ) ;
								push_entry( "rc"       , wstatus_str(digest.wstatus) , WIFEXITED(digest.wstatus)&&WEXITSTATUS(digest.wstatus)==0?Color::None:Color::Err ) ;
								if (porcelaine) { //!                                                                   as_is
									push_entry( "cpu time"       , to_string(double(digest.stats.cpu  )) , Color::None , true ) ;
									push_entry( "elapsed in job" , to_string(double(digest.stats.job  )) , Color::None , true ) ;
									push_entry( "elapsed total"  , to_string(double(digest.stats.total)) , Color::None , true ) ;
									push_entry( "used mem"       , to_string(       digest.stats.mem   ) , Color::None , true ) ;
								} else {
									::string const& mem_rsrc_str = allocated_rsrcs.contains("mem") ? allocated_rsrcs.at("mem") : required_rsrcs.contains("mem") ? required_rsrcs.at("mem") : ""s ;
									size_t          mem_rsrc     = +mem_rsrc_str?from_string_with_units<size_t>(mem_rsrc_str):0                                                                  ;
									bool            overflow     = digest.stats.mem > mem_rsrc                                                                                                   ;
									::string        mem_str      = to_string_with_units<'M'>(digest.stats.mem>>20)+'B'                                                                           ;
									if ( overflow && mem_rsrc ) mem_str += " > "+mem_rsrc_str+'B' ;
									push_entry( "cpu time"       , digest.stats.cpu  .short_str()                                       ) ;
									push_entry( "elapsed in job" , digest.stats.job  .short_str()                                       ) ;
									push_entry( "elapsed total"  , digest.stats.total.short_str()                                       ) ;
									push_entry( "used mem"       , mem_str                        , overflow?Color::Warning:Color::None ) ;
								}
							}
							//
							if (+pre_start.msg        ) push_entry( "start message" , localize(pre_start.msg,ro.startup_dir_s)                  ) ;
							if (+job_info.start.stderr) push_entry( "start stderr"  , job_info.start.stderr                    , Color::Warning ) ;
							if (+end.msg              ) push_entry( "message"       , localize(end      .msg,ro.startup_dir_s)                  ) ;
							// generate output
							if (porcelaine) {
								auto audit_rsrcs = [&]( ::string const& k , ::map_ss const& rsrcs , bool allocated )->void {
									size_t w2  = 0   ; for( auto const& [k,_] : rsrcs  ) w2 = ::max(w2,mk_py_str(k).size()) ;
									char   sep = ' ' ;
									audit( fd , ro , mk_py_str(k)+" : {" , true/*as_is*/ , lvl+1 , ',' ) ;
									for( auto const& [k,v] : required_rsrcs ) {
										::string v_str ;
										if ( allocated && (k=="cpu"||k=="mem"||k=="tmp") ) v_str = to_string(from_string_with_units<size_t>(v)) ;
										else                                               v_str = mk_py_str(v)                                 ;
										audit( fd , ro , to_string(::setw(w2),mk_py_str(k)," : ",v_str) , false/*as_is*/ , lvl+2 , sep ) ;
										sep = ',' ;
									}
									audit( fd , ro , "}" , true/*as_is*/ , lvl+1 ) ;
								} ;
								size_t   w  = mk_py_str("job").size()                         ; for( auto const& [k,_] : tab ) w = ::max(w,mk_py_str(k).size()) ;
								::string jn = localize(mk_file(job->name()),ro.startup_dir_s) ;
								audit( fd , ro , to_string(::setw(w),mk_py_str("job")," : ",mk_py_str(jn)) , false/*as_is*/ , lvl+1 , '{' ) ;
								for( auto const& [k,e] : tab )
									audit( fd , ro , to_string(::setw(w),mk_py_str(k)," : ",e.as_is?e.txt:mk_py_str(e.txt)) , false/*as_is*/ , lvl+1 , ',' ) ;
								if (+required_rsrcs ) audit_rsrcs( "required resources"  , required_rsrcs  , false/*allocated*/ ) ;
								if (+allocated_rsrcs) audit_rsrcs( "allocated resources" , allocated_rsrcs , true /*allocated*/ ) ;
								audit( fd , ro , "}" , true/*as_is*/ , lvl ) ;
							} else {
								size_t w = 0 ; for( auto const& [k,e] : tab ) if (e.txt.find('\n')==Npos) w = ::max(w,k.size()) ;
								_send_job( fd , ro , No/*show_deps*/ , false/*hide*/ , job , lvl ) ;
								for( auto const& [k,e] : tab ) //!                                                                as_is
									if (e.txt.find('\n')==Npos)   audit( fd , ro , e.color , to_string(::setw(w),k," : ",e.txt) , false , lvl+1 ) ;
									else                        { audit( fd , ro , e.color , to_string(          k," :"       ) , false , lvl+1 ) ; audit(fd,ro,e.txt,true/*as_is*/,lvl+2) ; }
								if ( +required_rsrcs || +allocated_rsrcs ) {
									size_t w2            = 0                ;
									for( auto const& [k,_] : required_rsrcs  ) w2 = ::max(w2,k.size()) ;
									for( auto const& [k,_] : allocated_rsrcs ) w2 = ::max(w2,k.size()) ;
									::string hdr = "resources :" ;
									if      (!+allocated_rsrcs) hdr = "required " +hdr ;
									else if (!+required_rsrcs ) hdr = "allocated "+hdr ;
									audit( fd , ro , hdr , true/*as_is*/ , lvl+1 ) ; //!                                                                                   as_is
									if      (!required_rsrcs                ) for( auto const& [k,v] : allocated_rsrcs ) audit( fd , ro , to_string(::setw(w2),k," : ",v) , true , lvl+2 ) ;
									else if (!allocated_rsrcs               ) for( auto const& [k,v] : required_rsrcs  ) audit( fd , ro , to_string(::setw(w2),k," : ",v) , true , lvl+2 ) ;
									else if (required_rsrcs==allocated_rsrcs) for( auto const& [k,v] : required_rsrcs  ) audit( fd , ro , to_string(::setw(w2),k," : ",v) , true , lvl+2 ) ;
									else {
										for( auto const& [k,rv] : required_rsrcs ) { //!                                                         as_is
											if (!allocated_rsrcs.contains(k)) { audit( fd , ro , to_string(::setw(w2),k,"(required )"," : ",rv) , true , lvl+2 ) ; continue ; }
											::string const& av = allocated_rsrcs.at(k) ;
											if (rv==av                      ) { audit( fd , ro , to_string(::setw(w2),k,"           "," : ",rv) , true , lvl+2 ) ; continue ; }
											/**/                                audit( fd , ro , to_string(::setw(w2),k,"(required )"," : ",rv) , true , lvl+2 ) ;
											/**/                                audit( fd , ro , to_string(::setw(w2),k,"(allocated)"," : ",av) , true , lvl+2 ) ;
										}
										for( auto const& [k,av] : allocated_rsrcs )
											if (!required_rsrcs.contains(k))    audit( fd , ro , to_string(::setw(w2),k,"(allocated)"," : ",av) , true , lvl+2 ) ;
									}
								}
							}
						} break ;
					DF}
				}
			} break ;
			case ReqKey::Bom     : ShowBom    (fd,ro,lvl).show_job(job) ;                             break ;
			case ReqKey::Running : ShowRunning(fd,ro,lvl).show_job(job) ;                             break ;
			case ReqKey::Deps    : _send_job( fd , ro , Maybe|verbose , false/*hide*/ , job , lvl ) ; break ;
			case ReqKey::Targets : {
				Rule::SimpleMatch match = job->simple_match() ;
				for( auto const& [tn,td] : digest.targets ) {
					Node t { tn } ;
					::string flags_str ;
					/**/                         flags_str += t->crc==Crc::None ? 'U' : +t->crc ? 'W' : '-' ;
					/**/                         flags_str += ' '                                           ;
					for( Tflag tf : All<Tflag> ) flags_str += td.tflags[tf] ? TflagChars[+tf].second : '-'  ;
					//
					_send_node( fd , ro , verbose , Maybe|!td.tflags[Tflag::Target]/*hide*/ , flags_str , t , lvl ) ;
				}
			} break ;
			default :
				throw to_string("cannot show ",snake(ro.key)," for job ",mk_file(job->name())) ;
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
		bool           ok         = true                          ;
		::vector<Node> targets    = ecr.targets()                 ;
		bool           porcelaine = ro.flags[ReqFlag::Porcelaine] ;
		char           sep        = ' '                           ;
		switch (ro.key) {
			case ReqKey::Bom     : { ShowBom     sb {fd,ro} ; for( Node t : targets ) sb.show_node(t) ; goto Return ; }
			case ReqKey::Running : { ShowRunning sr {fd,ro} ; for( Node t : targets ) sr.show_node(t) ; goto Return ; }
			default : ;
		}
		if (porcelaine) audit( fd , ro , "{" , true/*as_is*/ ) ;
		for( Node target : targets ) {
			trace("target",target) ;
			DepDepth lvl = 1 ;
			if      (porcelaine      ) audit     ( fd , ro , to_string(sep,' ',mk_py_str(localize(mk_file(target->name()),ro.startup_dir_s))," :") , true/*as_is*/ ) ;
			else if (targets.size()>1) _send_node( fd , ro , true/*always*/ , Maybe/*hide*/ , {} , target                                                          ) ;
			else                       lvl-- ;
			sep = ',' ;
			bool for_job = true ;
			switch (ro.key) {
				case ReqKey::InvDeps    :
				case ReqKey::InvTargets :
				case ReqKey::Running    : for_job = false ; break ;
				default                 : ;
			}
			Job job ;
			if (for_job) {
				job = _job_from_target(fd,ro,target) ;
				if ( !job && ro.key!=ReqKey::Info ) { ok = false ; continue ; }
			}
			switch (ro.key) {
				case ReqKey::Cmd        :
				case ReqKey::Env        :
				case ReqKey::ExecScript :
				case ReqKey::Stderr     :
				case ReqKey::Stdout     :
				case ReqKey::Targets    :
					_show_job(fd,ro,job,lvl) ;
				break ;
				case ReqKey::Info :
					if (target->status()==NodeStatus::Plain) {
						Job    cj             = target->conform_job() ;
						size_t w              = 0                     ;
						bool   seen_candidate = false                 ;
						for( Job j : target->conform_job_tgts() ) {
							w               = ::max(w,j->rule->name.size()) ;
							seen_candidate |= j!=cj                         ;
						}
						for( Job j : target->conform_job_tgts() ) {
							if      (j==job         ) continue ;
							if      (!seen_candidate) audit( fd , ro , Color::Note , to_string("official job " ,::setw(w),j->rule->name," : ",mk_file(j->name())) ) ; // no need to align
							else if (j==cj          ) audit( fd , ro , Color::Note , to_string("official job  ",::setw(w),j->rule->name," : ",mk_file(j->name())) ) ; // align
							else                      audit( fd , ro , Color::Note , to_string("job candidate ",::setw(w),j->rule->name," : ",mk_file(j->name())) ) ;
						}
					}
					if (!job) {
						Node n = target ;
						while ( +n->asking && n->asking.is_a<Node>() ) n = Node(n->asking) ;
						if (n!=target) {
							if (+n->asking) audit( fd , ro , to_string("required by : ",mk_file(Job(n->asking)->name())) ) ;
							else            audit( fd , ro , to_string("required by : ",mk_file(    n         ->name())) ) ;
						}
						continue ;
					}
					_show_job(fd,ro,job,lvl) ;
				break ;
				case ReqKey::Deps    : {
					bool     always = ro.flags[ReqFlag::Verbose] ;
					double   prio   = -Infinity                  ;
					if ( target->is_plain() && +target->dir() ) _send_node( fd , ro , always , Maybe/*hide*/ , "U" , target->dir() , lvl ) ;
					for( JobTgt jt : target->conform_job_tgts() ) {
						bool hide = !jt.produces(target) ;
						if      (always) _send_job( fd , ro , Yes   , hide          , jt , lvl ) ;
						else if (!hide ) _send_job( fd , ro , Maybe , false/*hide*/ , jt , lvl ) ;
					}
					if (prio!=-Infinity) _send_job( fd , ro , always?Yes:Maybe , false/*hide*/ , job ) ; // actual job is output last as this is what user views first
				} break ;
				case ReqKey::InvDeps :
					for( Job j : Persistent::job_lst() )
						for( Dep const& d : j->deps ) if (d==target) {
							_send_job( fd , ro , No , false/*hide*/ , j , lvl ) ;
							break ;
						}
				break ;
				case ReqKey::InvTargets :
					for( Job j : Persistent::job_lst() )
						for( Target const& t : j->targets ) if (t==target) {
							_send_job( fd , ro , No , false/*hide*/ , j , lvl ) ;
							break ;
						}
				break ;
			DF}
		}
		if (porcelaine) audit( fd , ro , "}" , true/*as_is*/ ) ;
	Return :
		trace(STR(ok)) ;
		return ok ;
	}

	CmdFunc g_cmd_tab[N<ReqProc>] ;
	static bool _inited = (                  // PER_CMD : add an entry to point to the function actually executing your command (use show as a template)
		g_cmd_tab[+ReqProc::Debug ] = _debug
	,	g_cmd_tab[+ReqProc::Forget] = _forget
	,	g_cmd_tab[+ReqProc::Mark  ] = _mark
	,	g_cmd_tab[+ReqProc::Show  ] = _show
	,	true
	) ;

}
