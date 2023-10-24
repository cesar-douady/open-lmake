// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh"

using namespace Disk ;

namespace Engine {

	CmdFunc g_cmd_tab[+ReqProc::N] ;
	static bool _inited = (                                                    // PER_CMD : add an entry to point to the function actually executing your command (use show as a template)
		g_cmd_tab[+ReqProc::Forget] = forget
	,	g_cmd_tab[+ReqProc::Mark  ] = mark
	,	g_cmd_tab[+ReqProc::Show  ] = show
	,	true
	) ;

	bool/*ok*/ forget( Fd fd , ReqOptions const& ro , ::vector<Node> const& targets ) {
		bool ok = true ;
		switch (ro.key) {
			case ReqKey::None :
				for( Node t : targets ) ok &= t->forget( ro.flags[ReqFlag::Targets] , ro.flags[ReqFlag::Deps] ) ;
			break ;
			case ReqKey::Error :
				g_store.invalidate_exec(true/*cmd_ok*/) ;
			break ;
			case ReqKey::Resources :
				for( Rule r : Rule::s_lst() ) {
					if (r->cmd_gen==r->rsrcs_gen) continue ;
					r.rule_data().cmd_gen = r->rsrcs_gen ;
					r.stamp() ;                                                          // we have modified rule, we must record it to make modif persistent
					audit( fd , ro , Color::Note , 0 , to_string("refresh ",r->name) ) ;
				}
			break ;
			default : FAIL(ro.key) ;
		}
		return ok ;
	}

	static bool/*ok*/ _freeze( Fd fd , ReqOptions const& ro , ::vector<Node> const& targets ) {
		Trace trace("freeze",ro,targets) ;
		switch (ro.key) {
			case ReqKey::Clear :
			case ReqKey::List  : {
				SWEAR( targets.empty() , targets ) ;
				::vector<Job> markeds = Job ::s_frozens() ;
				size_t        w       = 0                 ; for( Job j : markeds ) w = ::max(w,j->rule->user_name().size()) ;
				if (ro.key==ReqKey::Clear) {
					for( Job j : markeds ) if (j->rule->is_src()) Node(j.name())->mk_no_src() ;
					Job::s_clear_frozens() ;
				}
				for( Job j : markeds ) audit( fd , ro , ro.key==ReqKey::List?Color::Warning:Color::Note , 0/*lvl*/ , to_string(::setw(w),j->rule->user_name()) , j.name() ) ;
			} break ;
			case ReqKey::Add    :
			case ReqKey::Delete : {
				bool           add        = ro.key==ReqKey::Add ;
				::string       err        ;
				::vector<Node> to_do_node ;
				::vector<Job > to_do_job  ;
				size_t         w          = 0 ;
				//check
				for( Node t : targets ) {
					Job j = t->actual_job_tgt ;
					if      ( !j.active()                                ) { if (add) to_do_node.push_back(t)        ; else err = "file is not frozen" ; }
					else if ( j->frozen()==add                           ) { if (add) err = "file is already frozen" ; else err = "file is not frozen" ; }
					else if ( t->is_src()                                ) { if (add) err = "file is a source"       ; else to_do_node.push_back(t)    ; }
					else if ( !ro.flags[ReqFlag::Force] && !t->conform() ) { err = "target was not produced by its offical job" ;                        }
					else if ( !j->running_reqs().empty()                 ) { err = "job is running"                             ;                        }
					else                                                   { to_do_job.push_back(j) ; w = ::max( w , j->rule->user_name().size() ) ;     }
					if (!err.empty()) {
						audit(fd,ro,Color::Err,0,to_string(err," :"),t.name()) ;
						return false ;
					}
				}
				if ( !to_do_node.empty() && Req::s_n_reqs() ) {
					audit( fd , ro , Color::Err , 0/*lvl*/ , to_string("cannot ",add?"add":"remove"," frozen source file while running :") , to_do_node[0].name() ) ;
					return false ;
				}
				// do what is asked
				for( Node t : to_do_node ) {
					if (add) t->mk_src() ;
					to_do_job.push_back(t->actual_job_tgt) ;
				}
				if (!to_do_job.empty()) {
					Job::s_frozens(add,to_do_job) ;
					for( Job j : to_do_job ) audit( fd , ro , add?Color::Warning:Color::Note , 0/*lvl*/ , to_string(::setw(w),j->rule->user_name()) , j.name() ) ;
				}
				if (!add) for( Node t : to_do_node )
					t->mk_no_src() ;
				if (!to_do_node.empty()) EngineStore::s_invalidate_match() ; // seen from the engine, we have modified sources, we must rematch everything
			} break ;
			default : FAIL(ro.key) ;
		}
		return true ;
	}

	static bool/*ok*/ _manual_ok( Fd fd , ReqOptions const& ro , ::vector<Node> const& targets ) {
		Trace trace("manual_ok",ro,targets) ;
		switch (ro.key) {
			case ReqKey::Clear :
			case ReqKey::List  : {
				SWEAR( targets.empty() , targets ) ;
				::vector<Node> markeds = Node::s_manual_oks() ;
				if (ro.key==ReqKey::Clear) Node::s_clear_manual_oks() ;
				for( Node n : markeds ) audit( fd , ro , ro.key==ReqKey::List?Color::Warning:Color::Note , 0/*lvl*/ , {} , n.name() ) ;
			} break ;
			case ReqKey::Add    :
			case ReqKey::Delete : {
				bool add = ro.key==ReqKey::Add ;
				//check
				for( Node t : targets )
					if (t.manual_ok()==add) {
						audit(fd,ro,Color::Err,0,to_string("file is ",add?"already":"not"," manual-ok :"),t.name()) ;
						return false ;
					}
				// do what is asked
				Node::s_manual_oks(add,targets) ;
				for( Node t : targets ) audit( fd , ro , add?Color::Warning:Color::Note , 0/*lvl*/ , {} , t.name() ) ;
			} break ;
			default : FAIL(ro.key) ;
		}
		return true ;
	}

	static bool/*ok*/ _no_trigger( Fd fd , ReqOptions const& ro , ::vector<Node> const& targets ) {
		Trace trace("no_trigger",ro,targets) ;
		switch (ro.key) {
			case ReqKey::Clear :
			case ReqKey::List  : {
				SWEAR( targets.empty() , targets ) ;
				::vector<Node> markeds = Node::s_no_triggers() ;
				if (ro.key==ReqKey::Clear) Node::s_clear_no_triggers() ;
				for( Node n : markeds ) audit( fd , ro , ro.key==ReqKey::List?Color::Warning:Color::Note , 0/*lvl*/ , {} , n.name() ) ;
			} break ;
			case ReqKey::Add    :
			case ReqKey::Delete : {
				bool add = ro.key==ReqKey::Add ;
				//check
				for( Node t : targets )
					if (t.no_trigger()==add) {
						audit(fd,ro,Color::Err,0,to_string("file is ",add?"already":"not"," no-trigger :"),t.name()) ;
						return false ;
					}
				// do what is asked
				Node::s_no_triggers(add,targets) ;
				for( Node t : targets ) audit( fd , ro , add?Color::Warning:Color::Note , 0/*lvl*/ , {} , t.name() ) ;
			} break ;
			default : FAIL(ro.key) ;
		}
		return true ;
	}

	bool/*ok*/ mark( Fd fd , ReqOptions const& ro , ::vector<Node> const& targets ) {
		bool ok = true ;
		if (ro.flags[ReqFlag::Freeze   ]) ok &= _freeze    (fd,ro,targets) ;
		if (ro.flags[ReqFlag::ManualOk ]) ok &= _manual_ok (fd,ro,targets) ;
		if (ro.flags[ReqFlag::NoTrigger]) ok &= _no_trigger(fd,ro,targets) ;
		return ok ;
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
		audit( fd , ro , color , lvl , rule->user_name() , job.name() ) ;
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

	bool/*ok*/ show( Fd fd , ReqOptions const& ro , ::vector<Node> const& targets ) {
		Trace trace("show",ro.key) ;
		bool ok = true ;
		for( Node target : targets ) {
			trace("target",target) ;
			DepDepth lvl = 0 ;
			if (targets.size()>1) {
				_send_node( fd , ro , true/*always*/ , Maybe/*hide*/ , {} , target ) ;
				lvl++ ;
			}
			JobTgt jt = target->actual_job_tgt ;
			if (!jt.active()) {
				jt = target->makable() ? target->conform_job_tgt() : JobTgt() ;
				if (!jt.active()) {
					audit( fd , ro , Color::Err  , lvl   , "target not built"                ) ;
					audit( fd , ro , Color::Note , lvl+1 , "consider : lmake", target.name() ) ;
					ok = false ;
					continue ;
				}
			}
			trace("target",target,jt) ;
			Rule             rule         = jt->rule               ;
			::ifstream       job_stream   { jt->ancillary_file() } ;
			JobInfoStart     report_start ;
			JobInfoEnd       report_end   ;
			bool             has_start    = false                  ;
			bool             has_end      = false                  ;
			JobDigest const& digest       = report_end.end.digest  ;
			try { deserialize(job_stream,report_start) ; has_start = true ; } catch (...) { goto Go ; }
			try { deserialize(job_stream,report_end  ) ; has_end   = true ; } catch (...) { goto Go ; }
		Go :
			switch (ro.key) {
				case ReqKey::Backend    :
				case ReqKey::Env        :
				case ReqKey::ExecScript :
				case ReqKey::Info       :
				case ReqKey::Script     :
				case ReqKey::Stderr     :
				case ReqKey::Stdout     : {
					if (rule->is_special()) {
						switch (ro.key) {
							case ReqKey::Info :
							case ReqKey::Stderr : {
								_send_job( fd , ro , No/*show_deps*/ , false/*hide*/ , jt , lvl ) ;
								audit    ( fd , ro , Color::None , lvl+1 , jt->special_stderr() ) ;
							} break ;
							case ReqKey::ExecScript :
							case ReqKey::Script     :
							case ReqKey::Stdout     :
								_send_job( fd , ro , No/*show_deps*/ , false/*hide*/ , jt , lvl               ) ;
								audit    ( fd , ro , Color::Err , lvl+1 , "no "+mk_snake(ro.key)+" available" ) ;
							break ;
							default : FAIL(ro.key) ;
						}
					} else {
						JobRpcReq   const& pre_start = report_start.pre_start ;
						JobRpcReply const& start     = report_start.start     ;
						switch (ro.key) {
							case ReqKey::Env : {
								size_t w = 0 ;
								if (!has_start) { audit( fd , ro , Color::Err , lvl , "no info available" ) ; break ; }
								for( auto const& [k,v] : start.env ) w = max(w,k.size()) ;
								for( auto const& [k,v] : start.env ) audit( fd , ro , Color::None , lvl , to_string(::setw(w),k," : ",v) ) ;
							} break ;
							case ReqKey::ExecScript : {
								if (!has_start) { audit( fd , ro , Color::Err , 0 , "no info available" ) ; break ; }
								bool dbg        = ro.flags[ReqFlag::Debug]                      ;
								bool redirected = !start.stdin.empty() || !start.stdout.empty() ;
								bool is_python  = rule->cmd.spec.is_python                      ;
								bool use_pdb    = dbg && is_python                              ;
								//
								::string abs_cwd = *g_root_dir ;
								if (!start.cwd_s.empty()) {
									append_to_string(abs_cwd,'/',start.cwd_s) ;
									abs_cwd.pop_back() ;
								}
								Rule::SimpleMatch             match           = jt->simple_match()         ;
								::string                      script          = "#!/bin/bash\n"            ;
								::pair<vector_s,vector<Node>> targets_to_wash = jt->targets_to_wash(match) ;
								::vector_s const&             to_wash         = targets_to_wash.first      ;
								::uset_s                      to_report       ; for( Node t : targets_to_wash.second ) to_report.insert(t.name()) ;
								//
								for( ::string const& tn : to_wash ) if (!to_report.contains(tn)) append_to_string( script , "rm -f " , tn , '\n' ) ;
								if (!to_report.empty()) {
									script += "( set -x\n" ;
									for( ::string const& tn : to_wash ) if (to_report.contains(tn)) append_to_string( script , "rm -f " , tn , '\n' ) ;
									script += ")\n" ;
								}
								for( ::string const& d : match.target_dirs() ) append_to_string( script , "mkdir -p " , d , '\n' ) ;
								script += to_string("mkdir -p ${TMPDIR:-",P_tmpdir,"}\n") ;
								script += "exec env -i \\\n"   ;
								append_to_string( script , "\tROOT_DIR="          , mk_shell_str(*g_root_dir     ) , " \\\n" ) ;
								append_to_string( script , "\tSEQUENCE_ID="       , to_string   (pre_start.seq_id) , " \\\n" ) ;
								append_to_string( script , "\tSMALL_ID="          , to_string   (start.small_id  ) , " \\\n" ) ;
								append_to_string( script , "\tLMAKE_AUTODEP_ENV=" , "\"$LMAKE_AUTODEP_ENV\""       , " \\\n" ) ;
								switch (start.method) {
									case AutodepMethod::LdAudit   : append_to_string( script , "\tLD_AUDIT="   , "\"$LD_AUDIT\""   , " \\\n" ) ; break ;
									case AutodepMethod::LdPreload : append_to_string( script , "\tLD_PRELOAD=" , "\"$LD_PRELOAD\"" , " \\\n" ) ; break ;
									default : ;
								}
								bool seen_tmp_dir = false ;
								for( auto const& [k,v] : start.env ) {
									if (k=="TMPDIR") {
										if (start.keep_tmp) continue ;
										seen_tmp_dir = true ;
									}
									if (v==EnvPassMrkr) append_to_string(script,'\t',k,"=\"$",k,'"'                                           ," \\\n") ;
									else                append_to_string(script,'\t',k,'=',mk_shell_str(glb_subst(v,start.local_mrkr,abs_cwd))," \\\n") ;
								}
								// if tmp directory is viewed by job under another name, provide it as the script may work only with that name
								::string const& tmp_dir = start.autodep_env.tmp_view.empty() ? start.autodep_env.tmp_dir : start.autodep_env.tmp_view ;
								// add debug prelude if asked to do so
								// try to use stdin/stdout to debug with pdb as much as possible as readline does not work on alternate streams
								::string cmd ;
								if (use_pdb) {
									/**/            cmd += "import sys\n"                                                                      ;
									/**/            cmd += "sys.lmake_read_makefiles = True\nimport Lmakefile\ndel sys.lmake_read_makefiles\n" ;
									if (redirected) cmd += "import os\nsys.dbg_console = ( os.fdopen(3,'r') , os.fdopen(4,'w') )\ndel os\n"    ;
									/**/            cmd += "from debug import *\n"                                                             ;
									if (redirected) cmd += "del sys.dbg_console\n"                                                             ;
									/**/            cmd += "del sys\n"                                                                         ;
								}
								cmd += start.cmd ;
								//
								if ( !seen_tmp_dir                         ) append_to_string( script , "\tTMPDIR=" , mk_shell_str(mk_abs(tmp_dir,*g_root_dir+'/')) , " \\\n" ) ;
								for( ::string const& c : start.interpreter ) append_to_string( script , mk_shell_str(c) , ' '                                                 ) ;
								if ( dbg && !is_python                     ) append_to_string( script , "-x "                                                                 ) ;
								/**/                                         append_to_string( script , "-c " , mk_shell_str(cmd)                                             ) ;
								//
								if      (  use_pdb && redirected ) append_to_string(script," 3<&0 4>&1"                    ) ;
								if      ( !start.stdout.empty()  ) append_to_string(script," > ",mk_shell_str(start.stdout)) ;
								if      ( !start.stdin .empty()  ) append_to_string(script," < ",mk_shell_str(start.stdin )) ;
								else if ( !use_pdb || redirected ) append_to_string(script," < /dev/null"                  ) ;
								audit( fd , ro , Color::None , 0 , script ) ;
								if (ro.flags[ReqFlag::ManualOk]) {
									::vector<Node>    targets ;
									Rule::SimpleMatch match   = jt->simple_match() ;                                 // match holds static target names
									for( ::string const& tn : match.static_targets() ) targets.push_back(Node(tn)) ;
									for( Node t             : jt->star_targets       ) targets.push_back(     t  ) ;
									Node::s_manual_oks(true/*add*/,targets) ;
								}
							} break ;
							case ReqKey::Script : {
								if (!has_start) { audit( fd , ro , Color::Err , lvl , "no info available" ) ; break ; }
								::string hdr = "#!" ;
								::string sep ; sep.reserve(1) ;
								for( ::string const& c : start.interpreter ) { hdr+=sep ; hdr+=c ; sep=" " ; } hdr+='\n' ;
								audit( fd , ro , Color::None , lvl , hdr       ) ;
								audit( fd , ro , Color::None , lvl , start.cmd ) ;
							} break ;
							case ReqKey::Stdout :
								if (!has_end ) { audit( fd , ro , Color::Err , lvl , "no info available" ) ; break ; }
								_send_job( fd , ro , No/*show_deps*/ , false/*hide*/ , jt , lvl ) ;
								audit    ( fd , ro , Color::None , lvl+1 , digest.stdout )        ;
							break ;
							case ReqKey::Stderr :
								if (!has_end) { audit( fd , ro , Color::Err , lvl , "no info available" ) ; break ; }
								_send_job( fd , ro , No/*show_deps*/ , false/*hide*/ , jt , lvl     ) ;
								for( auto const& [pfx,n] : digest.analysis_err ) audit( fd , ro , Color::Note , lvl+1 , pfx , n       ) ;
								/**/                                             audit( fd , ro , Color::None , lvl+1 , digest.stderr ) ;
							break ;
							case ReqKey::Backend :
								if (!has_end ) { audit( fd , ro , Color::Err , lvl , "no info available" ) ; break ; }
								_send_job( fd , ro , No/*show_deps*/ , false/*hide*/ , jt , lvl ) ;
								audit    ( fd , ro , Color::None , lvl+1 , report_end.backend_msg ) ;
							break ;
							case ReqKey::Info : {
								::string ids      = to_string( "job=",pre_start.job , " , small=",start.small_id )                                          ;
								bool     has_host = !pre_start.host.empty()                                                                                 ;
								if (pre_start.seq_id) append_to_string(ids," , seq=",pre_start.seq_id) ; // there may be no seq_id if job hit the cache
								//
								_send_job( fd , ro , No/*show_deps*/ , false/*hide*/ , jt , lvl ) ;
								if (has_start) {
									::pair_s<NodeIdx> reason = report_start.submit_attrs.reason.str() ;
									if (+reason.second) {
										bool err = report_start.submit_attrs.reason.err() ;
										_send_node( fd , ro , true/*always*/ , Maybe&!err/*hide*/ , to_string("reason         : ",reason.first," :") , Node(reason.second).name() , lvl+1 ) ;
									} else {
										audit( fd , ro , Color::None , lvl+1 , "reason         : "+reason.first ) ;
									}
								}
								if (has_host) audit( fd , ro , Color::None , lvl+1 , to_string("host           : ",pre_start.host) ) ;
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
									required_rsrcs = mk_map(rule->submit_rsrcs_attrs.eval(jt,match).rsrcs) ;
								} catch(::string const&) {}
								//
								if (has_end) {
									bool overflow = !allocated_rsrcs.contains("mem") || digest.stats.mem>size_t(atol(allocated_rsrcs.at("mem").c_str())) ;
									int  ws       = digest.wstatus                                                                                       ;
									bool ok       = WIFEXITED(ws) && WEXITSTATUS(ws)==0                                                                  ;
									::string rc =
										ok              ? "ok"s
									:	WIFEXITED  (ws) ? to_string("exited "  ,            WEXITSTATUS(ws) )
									:	WIFSIGNALED(ws) ? to_string("signaled ",::strsignal(WTERMSIG   (ws)))
									:	"??"s
									;
									audit( fd , ro ,                         Color::None , lvl+1 , to_string("end date       : ",digest.end_date.str()          ) ) ;
									audit( fd , ro , !ok     ?Color::Err    :Color::None , lvl+1 , to_string("rc             : ",rc                             ) ) ;
									audit( fd , ro ,                         Color::None , lvl+1 , to_string("cpu time       : ",digest.stats.cpu  .short_str() ) ) ;
									audit( fd , ro ,                         Color::None , lvl+1 , to_string("elapsed in job : ",digest.stats.job  .short_str() ) ) ;
									audit( fd , ro ,                         Color::None , lvl+1 , to_string("elapsed total  : ",digest.stats.total.short_str() ) ) ;
									audit( fd , ro , overflow?Color::Warning:Color::None , lvl+1 , to_string("used mem       : ",digest.stats.mem/1'000'000,"MB") ) ;
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
				case ReqKey::AllDeps :
				case ReqKey::Deps    : {
					bool     always      = ro.flags[ReqFlag::Verbose] ;
					::string uphill_name = dir_name(target.name())    ;
					double   prio        = -Infinity                  ;
					if (!uphill_name.empty()) _send_node( fd , ro , always , Maybe/*hide*/ , "U" , Node(uphill_name) , lvl ) ;
					for( JobTgt job_tgt : target->job_tgts ) {
						if (job_tgt->rule->prio<prio) break ;
						if (job_tgt==jt ) { prio = rule->prio ; continue ; }   // actual job is output last as this is what user views first
						bool hide = !job_tgt.produces(target) ;
						if      (always) _send_job( fd , ro , Yes   , hide          , job_tgt , lvl ) ;
						else if (!hide ) _send_job( fd , ro , Maybe , false/*hide*/ , job_tgt , lvl ) ;
					}
					if (prio!=-Infinity) _send_job( fd , ro , always?Yes:Maybe , false/*hide*/ , jt ) ; // actual job is output last as this is what user views first
				} break ;
				case ReqKey::Targets :
					if (rule->is_special()) {
						_send_node( fd , ro , true/*always*/ , Maybe/*hide*/ , {} , target , lvl ) ;
					} else {
						Rule::FullMatch match      = jt->full_match() ;
						::string        unexpected = "<unexpected>"   ;
						size_t          wk         = 0                ;
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
					}
				break ;
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

}
