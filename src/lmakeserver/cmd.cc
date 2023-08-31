// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh"

using namespace Disk ;

namespace Engine {

	CmdFunc g_cmd_tab[+ReqProc::N] ;
	static bool _inited = (                                                    // PER_CMD : add an entry to point to the function actually executing your command (use show as a template)
		g_cmd_tab[+ReqProc::Forget] = forget
	,	g_cmd_tab[+ReqProc::Freeze] = freeze
	,	g_cmd_tab[+ReqProc::Show  ] = show
	,	true
	) ;

	bool/*ok*/ forget( Fd fd , ReqOptions const& ro , ::vector<Node> const& targets ) {
		bool ok = true ;
		switch (ro.key) {
			case ReqKey::None :
				for( Node t : targets ) ok &= t.forget() ;
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

	static void _freeze( Fd fd , ReqOptions const& ro , Job j , bool add , size_t w ) {
		Trace trace("_freeze_job",STR(add),j) ;
		SWEAR(j.active()) ;
		j->status = add ? Status::Frozen : Status::New ;
		audit( fd , ro  , add?Color::Warning:Color::Note , 0 , to_string(add?"froze job ":"melted job ",::setw(w),j->rule->user_name(),' ') , j.name() ) ;
	}
	static void _freeze( Fd fd , ReqOptions const& ro , Node t , bool add ) {
		Trace trace("_freeze_node",STR(add),t) ;
		if (add) {
			::string     tn  = t.name() ;
			UNode        un  { t      } ;
			FileInfoDate fid { tn     } ;
			t .mk_src() ;
			un.refresh( fid.tag==FileTag::Lnk , Crc(tn,g_config.hash_algo) , fid.date ) ;
			un->actual_job_tgt->status = Status::Frozen ;
		} else {
			t.mk_no_src() ;
		}
		audit( fd , ro  , add?Color::Warning:Color::Note , 0 , add?"froze file":"melted file" , t.name() ) ;
	}
	bool/*ok*/ freeze( Fd fd , ReqOptions const& ro , ::vector<Node> const& targets ) {
		bool force = ro.flags[ReqFlag::Force] ;
		bool add   = ro.key==ReqKey::Add      ;
		bool lst   = ro.key==ReqKey::List     ;
		bool glb   = false                    ;
		Trace trace("freeze",ro,targets) ;
		switch (ro.key) {
			case ReqKey::DeleteAll :
			case ReqKey::List      : glb = true ; break ;
			default : ;
		}

		if (glb) {
			SWEAR(targets.empty()) ;
			if (lst) {
				{	::vector<Job> frozens = Job::s_frozens() ;
					size_t        w       = 0                ;
					trace("job_list",frozens) ;
					for( Job j : frozens ) w = ::max(w,j->rule->user_name().size()) ;
					/**/                   audit(fd,ro,Color::Note   ,0,"jobs :"                                               ) ;
					for( Job j : frozens ) audit(fd,ro,Color::Warning,1,to_string(::setw(w),j->rule->user_name()),j.user_name()) ;
				}
				{	::vector<Node> frozens = Node::s_frozens() ;
					trace("node_list",frozens) ;
					/**/                    audit(fd,ro,Color::Note   ,0,"files :"  ) ;
					for( Node t : frozens ) audit(fd,ro,Color::Warning,1,{},t.name()) ;
				}
			} else {
				size_t w = 0 ;
				for( Job  j : Job ::s_frozens() ) w = ::max(w,j->rule->user_name().size()) ;
				for( Job  j : Job ::s_frozens() ) _freeze(fd,ro,j,false/*add*/,w) ;
				for( Node t : Node::s_frozens() ) _freeze(fd,ro,t,false/*add*/  ) ;
				Job ::s_frozens({}) ;
				Node::s_frozens({}) ;
			}
		} else {
			::string       err        ;
			::vector<Node> to_do_node ;
			::vector<Job > to_do_job  ;
			size_t         w          = 0 ;
			//check
			for( Node t : targets ) {
				Job j = t->actual_job_tgt ;
				if (!j.active()) {
					if (add) to_do_node.push_back(t) ;
					else     err = "file is already melted" ;
				} else if ( j->frozen() && add ) {
					err = "target is already frozen" ;
				} else if (t->is_src()) {
					if (j->frozen()) to_do_node.push_back(t) ;
					else             err = "file is a source" ;
				} else {
					if      ( !force && !t->conform() ) err = "target was not produced by its offical job" ;
					else if ( add                     ) for( Req r [[maybe_unused]] : j.running_reqs() ) err = "job is running" ;
					else if ( !j->frozen()            ) err = "target is already melted" ;
					if (err.empty()) {
						to_do_job.push_back(j) ;
						w = ::max( w , j->rule->user_name().size() ) ;
					}
				}
				if (!err.empty()) {
					audit(fd,ro,Color::Err,0,to_string(err ," :"),t .name()) ;
					return false ;
				}
			}
			if ( !to_do_node.empty() && Req::s_n_reqs() ) {
				audit(fd,ro,Color::Err,0,"cannot freeze/melt files while running") ;
				return false ;
			}
			// do what is asked
			{	::uset<Job> frozens = mk_uset(Job::s_frozens()) ;
				for( Job j : to_do_job ) {
					_freeze(fd,ro,j,add,w) ;
					if (add) frozens.insert(j) ;
					else     frozens.erase (j) ;
				}
				Job::s_frozens(mk_vector(frozens)) ;
			}
			{	::uset<Node> frozens = mk_uset(Node::s_frozens()) ;
				for( Node t : to_do_node ) {
					_freeze(fd,ro,t,add) ;
					if (add) frozens.insert(t) ;
					else     frozens.erase (t) ;
				}
				Node::s_frozens(mk_vector(frozens)) ;
				EngineStore::s_invalidate_match() ;                            // seen from the engine, we have modified sources, we must rematch everything
			}
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
		audit( fd , ro , color , lvl , rule->user_name() , job.user_name() ) ;
		if (show_deps==No) return ;
		size_t  w            = 0 ;
		for( auto const& [k,_] : rule->create_match_attrs.spec.deps ) w = ::max( w , k.size() ) ;
		for( NodeIdx d=0 ; d<job->deps.size() ; d++ ) {
			Dep const& dep = job->deps[d] ;
			bool       cdp = d  >0                && dep           .parallel ;
			bool       ndp = d+1<job->deps.size() && job->deps[d+1].parallel ;
			::string dep_key = dep.flags[DFlag::Static] && !rule->create_match_attrs.spec.full_dynamic ? rule->create_match_attrs.spec.deps[d].first : ""s ;
			::string pfx     = to_string( dep.flags_str() ,' ', ::setw(w) , dep_key ,' ')                                                                  ;
			if      ( !cdp && !ndp ) pfx.push_back(' ' ) ;
			else if ( !cdp &&  ndp ) pfx.push_back('/' ) ;
			else if (  cdp &&  ndp ) pfx.push_back('|' ) ;
			else                     pfx.push_back('\\') ;
			_send_node( fd , ro , show_deps==Yes , (Maybe&!dep.flags[DFlag::Required])|hide , pfx , dep , lvl+1 ) ;
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
			Rule             rule         = jt->rule              ;
			::ifstream       job_stream   { jt.ancillary_file() } ;
			JobInfoStart     report_start ;
			JobInfoEnd       report_end   ;
			bool             has_start    = false                 ;
			bool             has_end      = false                 ;
			JobDigest const& digest       = report_end.digest     ;
			try { deserialize(job_stream,report_start) ; has_start = true ; } catch (...) { goto Go ; }
			try { deserialize(job_stream,report_end  ) ; has_end   = true ; } catch (...) { goto Go ; }
		Go :
			switch (ro.key) {
				case ReqKey::Env        :
				case ReqKey::ExecScript :
				case ReqKey::Info       :
				case ReqKey::Script     :
				case ReqKey::Stderr     :
				case ReqKey::Stdout     : {
					if (rule.is_special()) {
						switch (ro.key) {
							case ReqKey::Info :
							case ReqKey::Stderr : {
								_send_job( fd , ro , No/*show_deps*/ , false/*hide*/ , jt , lvl ) ;
								audit    ( fd , ro , Color::None , lvl+1 , jt.special_stderr()  ) ;
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
						switch (ro.key) {
							case ReqKey::Env : {
								size_t w = 0 ;
								if (!has_start) { audit( fd , ro , Color::Err , lvl , "no info available" ) ; break ; }
								for( auto const& [k,v] : report_start.start.env ) w = max(w,k.size()) ;
								for( auto const& [k,v] : report_start.start.env ) audit( fd , ro , Color::None , lvl , to_string(::setw(w),k," : ",v) ) ;
							} break ;
							case ReqKey::ExecScript : {
								if (!has_start) { audit( fd , ro , Color::Err , 0 , "no info available" ) ; break ; }
								::string abs_cwd = *g_root_dir ; if (!report_start.start.cwd_s.empty()) { append_to_string(abs_cwd,'/',report_start.start.cwd_s) ; abs_cwd.pop_back() ; }
								::string script = "exec env -i\\\n" ;
								for( auto const& [k,v] : report_start.start.env ) append_to_string(script,'\t',k,'=',mk_shell_str(glb_subst(v,report_start.start.local_mrkr,abs_cwd)),"\\\n") ;
								//
								append_to_string( script , "\tTMPDIR="      , mk_shell_str(report_start.start.job_tmp_dir) , "\\\n" ) ;
								append_to_string( script , "\tROOT_DIR="    , mk_shell_str(*g_root_dir                   ) , "\\\n" ) ;
								append_to_string( script , "\tSEQUENCE_ID=" , to_string   (report_start.pre_start.seq_id ) , "\\\n" ) ;
								append_to_string( script , "\tSMALL_ID="    , to_string   (report_start.start.small_id   ) , "\\\n" ) ;
								//
								for( ::string const& c : report_start.start.interpreter ) append_to_string(script,      mk_shell_str(c                     ),' ') ;
								/**/                                                      append_to_string(script,"-c ",mk_shell_str(report_start.start.cmd)    ) ;
								audit( fd , ro , Color::None , 0 , script ) ;
							} break ;
							case ReqKey::Script : {
								if (!has_start) { audit( fd , ro , Color::Err , lvl , "no info available" ) ; break ; }
								::string hdr = "#!" ;
								::string sep ; sep.reserve(1) ;
								for( ::string const& c : report_start.start.interpreter ) { hdr+=sep ; hdr+=c ; sep=" " ; } hdr+='\n' ;
								audit( fd , ro , Color::None , lvl , hdr                    ) ;
								audit( fd , ro , Color::None , lvl , report_start.start.cmd ) ;
							} break ;
							case ReqKey::Stdout :
								if (!has_end ) { audit( fd , ro , Color::Err , lvl , "no info available" ) ; break ; }
								_send_job( fd , ro , No/*show_deps*/ , false/*hide*/ , jt , lvl ) ;
								audit    ( fd , ro , Color::None , lvl+1 , digest.stdout )        ;
							break ;
							case ReqKey::Stderr :
								if (!has_end) { audit( fd , ro , Color::Err , lvl , "no info available" ) ; break ; }
								_send_job( fd , ro , No/*show_deps*/ , false/*hide*/ , jt , lvl     ) ;
								for( auto const& [pfx,ni] : digest.analysis_err ) audit( fd , ro , Color::Note , lvl+1 , pfx , ni?Node(ni).name():""s ) ;
								/**/                                              audit( fd , ro , Color::None , lvl+1 , digest.stderr                ) ;
							break ;
							case ReqKey::Info : {
								int      ws       = digest.wstatus                                                                                                                     ;
								::string rc       = WIFEXITED(ws) ? to_string("exited ",WEXITSTATUS(ws)) : WIFSIGNALED(ws) ? to_string("signaled ",::strsignal(WTERMSIG(ws))) : "??"s  ;
								::string ids      = to_string( "job=",report_start.pre_start.job , " , seq=",report_start.pre_start.seq_id , " , small=",report_start.start.small_id ) ;
								bool     has_host = !report_start.pre_start.host.empty()                                                                                               ;
								//
								_send_job( fd , ro , No/*show_deps*/ , false/*hide*/ , jt , lvl ) ;
								if (has_start) {
									::pair_s<Node> reason = Job::s_reason_str(report_start.submit_attrs.reason) ;
									if (+reason.second) {
										bool err = report_start.submit_attrs.reason.err() ;
										_send_node( fd , ro , true/*always*/ , Maybe&!err/*hide*/ , to_string("reason         : ",reason.first," :") , reason.second.name() , lvl+1 ) ;
									} else {
										audit( fd , ro , Color::None , lvl+1 , "reason         : "+reason.first ) ;
									}
								}
								if (has_host) audit( fd , ro , Color::None , lvl+1 , to_string("host           : ",report_start.pre_start.host) ) ;
								if (has_start) {
									static constexpr AutodepMethod AdMD = AutodepMethod::Dflt ;
									//
									JobInfoStart const& rs     = report_start          ;
									size_t              cwd_sz = rs.start.cwd_s.size() ;
									//
									audit( fd , ro , Color::None , lvl+1 , to_string("backend        : ",mk_snake(rs.submit_attrs.tag)                          ) ) ;
									audit( fd , ro , Color::None , lvl+1 , to_string("id's           : ",ids                                                    ) ) ;
									audit( fd , ro , Color::None , lvl+1 , to_string("tmp dir        : ",rs.start.job_tmp_dir                                   ) ) ;
									audit( fd , ro , Color::None , lvl+1 , to_string("scheduling     : ",rs.eta.str(),'-',rs.submit_attrs.pressure.short_str()) ) ;
									//
									if ( rs.submit_attrs.live_out) audit( fd , ro , Color::None , lvl+1 , to_string("live_out       : true"                              ) ) ;
									if ( rs.start.auto_mkdir     ) audit( fd , ro , Color::None , lvl+1 , to_string("auto_mkdir     : true"                              ) ) ;
									if ( rs.start.ignore_stat    ) audit( fd , ro , Color::None , lvl+1 , to_string("ignore_stat    : true"                              ) ) ;
									if (!rs.start.chroot.empty() ) audit( fd , ro , Color::None , lvl+1 , to_string("chroot         : ",rs.start.chroot                  ) ) ;
									if (!rs.start.cwd_s .empty() ) audit( fd , ro , Color::None , lvl+1 , to_string("cwd            : ",rs.start.cwd_s.substr(0,cwd_sz-1)) ) ;
									if ( rs.start.method!=AdMD   ) audit( fd , ro , Color::None , lvl+1 , to_string("autodep        : ",mk_snake(rs.start.method)        ) ) ;
									if (+rs.start.timeout        ) audit( fd , ro , Color::None , lvl+1 , to_string("timeout        : ",rs.start.timeout.short_str()     ) ) ;
								}
								if (has_end) {
									audit( fd , ro , Color::None , lvl+1 , to_string("end date       : ",digest.end_date.str()                  ) ) ;
									audit( fd , ro , Color::None , lvl+1 , to_string("rc             : ",rc                                     ) ) ;
									audit( fd , ro , Color::None , lvl+1 , to_string("cpu time       : ",float(digest.stats.cpu  )         ,'s' ) ) ;
									audit( fd , ro , Color::None , lvl+1 , to_string("elapsed in job : ",float(digest.stats.job  )         ,'s' ) ) ;
									audit( fd , ro , Color::None , lvl+1 , to_string("elapsed total  : ",float(digest.stats.total)         ,'s' ) ) ;
									audit( fd , ro , Color::None , lvl+1 , to_string("memory         : ",      digest.stats.mem  /1'000'000,"MB") ) ;
								}
								//
								if ( has_start && !report_start.rsrcs.empty() ) {
									SubmitRsrcsAttrs rsrcs_attrs ;
									bool             no_rsrcs    = false ;
									try {
										Rule::SimpleMatch match_ ;
										rsrcs_attrs = rule->submit_rsrcs_attrs.eval(jt,match_) ;
									} catch(::string const& e) {
										audit( fd , ro , Color::Err , lvl+1 , "resources : cannot compute" ) ;
										audit( fd , ro , Color::Err , lvl+2 , e                            ) ;
										no_rsrcs = true ;
									}
									if (!no_rsrcs) {
										audit( fd , ro , Color::None , lvl+1 , "resources :" ) ;
										size_t kw = 0 ;
										for( auto const& [k,_] : rsrcs_attrs.rsrcs ) kw = ::max(kw,k.size()) ;
										for( size_t r=0 ; r<rsrcs_attrs.rsrcs.size() ; r++ )
											audit( fd , ro , Color::None , lvl+2 , to_string(::setw(kw),rsrcs_attrs.rsrcs[r].first," : ",report_start.rsrcs[r]) ) ;
									}
								}
							} break ;
							default : FAIL(ro.key) ;
						}
					}
				} break ;
				case ReqKey::AllDeps :
				case ReqKey::Deps    : {
					bool     always      = ro.key==ReqKey::AllDeps ;
					::string uphill_name = dir_name(target.name()) ;
					double   prio        = -Infinity               ;
					if (!uphill_name.empty()) _send_node( fd , ro , always , Maybe/*hide*/ , "U" , Node(uphill_name) , lvl ) ;
					for( JobTgt job_tgt : target->job_tgts ) {
						if (job_tgt->rule->prio<prio) break ;
						if (job_tgt==jt ) { prio = rule->prio ; continue ; }   // actual job is output last as this is what user views first
						bool hide = job_tgt.produces(target)==No ;
						if      (always) _send_job( fd , ro , Yes   , hide          , job_tgt , lvl ) ;
						else if (!hide ) _send_job( fd , ro , Maybe , false/*hide*/ , job_tgt , lvl ) ;
					}
					if (prio!=-Infinity) _send_job( fd , ro , always?Yes:Maybe , false/*hide*/ , jt ) ; // actual job is output last as this is what user views first
				} break ;
				case ReqKey::Targets :
					if (rule.is_special()) {
						_send_node( fd , ro , true/*always*/ , Maybe/*hide*/ , {} , target , lvl ) ;
					} else {
						Rule::Match match_     = jt.match()     ;
						::string    unexpected = "<unexpected>" ;
						size_t      wk         = 0              ;
						for( auto const& [tn,td] : report_end.digest.targets ) {
							VarIdx ti = match_.idx(tn) ;
							wk = ::max( wk , ti==Rule::NoVar?unexpected.size():rule->targets[ti].first.size() ) ;
						}
						for( auto const& [tn,td] : report_end.digest.targets ) {
							VarIdx ti   = match_.idx(tn)                                       ;
							TFlags stfs = ti==Rule::NoVar ? UnexpectedTFlags : rule->flags(ti) ;
							bool   m    = stfs[TFlag::Match]                                   ;
							::string flags_str ;
							flags_str += m                      ? '-'                : '#'                ;
							flags_str += td.crc==Crc::None      ? '!'                : '-'                ;
							flags_str += +(td.dfs&AccessDFlags) ? (td.write?'U':'R') : (td.write?'W':'-') ;
							flags_str += stfs[TFlag::Star]      ? '*'                : '-'                ;
							flags_str += ' ' ;
							for( TFlag f : TFlag::N ) {
								if (f>=TFlag::RuleOnly) break ;
								flags_str += td.tfs[f]?TFlagChars[+f]:'-' ;
							}
							_send_node( fd , ro , true/*always*/ , Maybe|!m/*hide*/ , to_string( flags_str ,' ', ::setw(wk) , rule->targets[ti].first ) , tn , lvl ) ;
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
