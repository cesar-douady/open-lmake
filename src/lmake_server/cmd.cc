// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "cmd.hh" // /!\ must be first to include Python.h first

#include "rpc_job.hh"

using namespace Disk ;
using namespace Py   ;
using namespace Re   ;

namespace Engine {

	static bool/*ok*/ _collect(EngineClosureReq const& ecr) {
		SWEAR(!ecr.is_job()) ;                                                // targets are typically dirs, passing a job is non-sense
		Fd                        fd            = ecr.fd                    ;
		ReqOptions const&         ro            = ecr.options               ;
		ConfigDyn::Collect const& collect       = g_config->collect         ;
		::umap_s<bool/*keep*/>    dirs          ;
		bool                      dry_run       = ro.flags[ReqFlag::DryRun] ;
		bool                      is_repo_root  = false/*garbage*/          ;                                                                                       // set in loop over target_dir
		::umap_ss                 static_ignore ;                             for( auto const& [k,v] : collect.static_ignore ) static_ignore.try_emplace( v , k ) ;
		::vmap<RegExpr,::string>  star_ignore   ;
		//
		auto keep = [&](::string const& dir_s) {
			for( ::string d_s=dir_s ; +d_s ; d_s=dir_name_s(d_s) ) {          // mark uphill dirs as kept
				auto [it,inserted] = dirs.try_emplace(d_s,true/*keep*/) ;
				if (!inserted) {
					if (it->second) break ;
					it->second = true/*keep*/ ;
				}
			}
		} ;
		auto ignore = [&](::string const& path)->::string const* {
			auto it = static_ignore.find(path) ;    if (it!=static_ignore.end()) return &it->second ;
			for( auto const& [re,k] : star_ignore ) if (+re.match(path)        ) return &k          ;
			/**/                                                                 return nullptr     ;
		} ;
		auto prune = [&](::string const& dir_s) {
			::string const* key = nullptr ;
			{	/**/                         if ( !dir_s                           ) goto Keep  ;
				/**/                         if ( is_repo_root && dir_s==AdminDirS ) goto Prune ;
				/**/                         if ( (key=ignore(dir_s))              ) goto Prune ;
				Node n { no_slash(dir_s) } ; if ( !n                               ) goto Keep  ;
				/**/                         if ( n->buildable==Buildable::SrcDir  ) goto Prune ;
			}
		Keep :
			return false ;
		Prune :
			if ( key && ro.flags[ReqFlag::Verbose] ) audit( fd , ro , Color::HiddenNote , cat("prune ",*key," : ",mk_file(no_slash(dir_s))) ) ;
			keep(dir_s) ;
			return true ;
		} ;
		//
		for( auto const& [k,v] : collect.star_ignore ) {
			Pattern            pattern   ;
			::vector<VarIdx  > n_seen    ( collect.stems.size() ) ;           // indexed by stem index, number of time stem is seen
			::vector<uint32_t> groups    ( collect.stems.size() ) ;           // indexed by stem index, provide the corresponding group number in pattern
			VarIdx             cur_group = 1                                ;
			subst_target( v , [&](VarIdx s)->::string { n_seen[s]++ ; return {} ; } ) ;
			subst_target(
				v
			,	[&](VarIdx s)->::string {
					if (groups[s]) { //!                                                 capture
						/**/               pattern.emplace_back( cat('\\',groups[s])     , No  ) ;
					} else {
						if (n_seen[s]>1) { pattern.emplace_back( collect.stems[s].second , Yes ) ; groups[s] = cur_group ; cur_group++ ; }
						else               pattern.emplace_back( collect.stems[s].second , No  ) ;
						cur_group += collect.stem_n_marks[s] ;
					}
					return {} ;
				}
			,	[&](::string const& s)->::string {
					pattern.emplace_back( s , Maybe/*capture*/ ) ;            // Maybe means fixed
					return {} ;
				}
			) ;
			star_ignore.emplace_back( pattern , k ) ;
		}
		//
		for( ::string const& target_dir : ecr.dirs() ) {
			SWEAR( +target_dir ) ;                                            // ecr.dirs provides . for repo root
			is_repo_root = target_dir=="." ;
			//
			for( auto& [target,tag] : walk( target_dir , TargetTags|FileTag::Dir , target_dir , prune ) ) {
				if (is_repo_root) {
					if (target==".") continue ;
					SWEAR( target.starts_with("./") , target_dir ) ;
					target.erase(0,2) ;
				}
				//
				if (tag==FileTag::Dir) {
					dirs.try_emplace(with_slash(target),false/*keep*/) ;
					continue ;
				}
				::string const* key = nullptr ;
				{	/**/                         if ((key=ignore(target))         ) goto Keep       ;
					Node    n  { target }      ; if (!n                           ) goto Quarantine ;
					/**/                         if (n->buildable==Buildable::Src ) goto Keep       ;
					/**/                         if (n->sig.sig!=FileSig(target)  ) goto Quarantine ;
					Job     j  = n->actual_job ; if (!j                           ) goto Quarantine ;
					RuleCrc rc = j->rule_crc   ; if (!rc                          ) goto Quarantine ;
					/**/                         if (rc->state>RuleCrcState::CmdOk) goto Unlnk      ;
				}
			Keep :
				if ( key && ro.flags[ReqFlag::Verbose] ) audit( fd , ro , Color::HiddenNote , cat("ignore ",*key," : ",mk_file(target)) ) ;
				keep(dir_name_s(target)) ;
				continue ;
			Quarantine :
				try {
					if (!dry_run) quarantine(target) ;
					audit( fd , ro , cat("quarantine ",mk_file(target)) ) ;
				} catch (::string const& e) {
					audit( fd , ro , Color::Err , e ) ;
				}
				continue ;
			Unlnk :
				try                       { if (!dry_run) unlnk(target) ; audit( fd , ro ,              cat("rm "               ,mk_file(target,FileDisplay::Shell)) ) ; }
				catch (::string const& e) {                               audit( fd , ro , Color::Err , cat("cannot rm (",e,") ",mk_file(target                   )) ) ; }
			}
		}
		for( auto const& [d_s,k] : dirs ) {
			if (k) continue ;
			::string d  = no_slash(d_s)                    ;
			int      rc = dry_run ? 0 : ::rmdir(d.c_str()) ;
			if (rc==0) audit( fd , ro ,              cat("rmdir "       ,mk_file(d,FileDisplay::Shell)) ) ;
			else       audit( fd , ro , Color::Err , cat("cannot rmdir ",mk_file(d                   )) ) ;
		}
		return true ;
	}

	static bool _is_mark_glb(ReqKey key) {
		switch (key) {
			case ReqKey::Clear  :
			case ReqKey::List   : return true  ;
			case ReqKey::Add    :
			case ReqKey::Delete : return false ;
		DF}                                      // NO_COV
	}

	static bool/*ok*/ _freeze(EngineClosureReq const& ecr) {
		ReqOptions const& ro = ecr.options ;
		Trace trace("freeze",ecr) ;
		if (_is_mark_glb(ro.key)) {
			::vector<Job > jobs  = Job ::s_frozens()                                                      ;
			::vector<Node> nodes = Node::s_frozens()                                                      ;
			size_t         w     = ::max<size_t>( jobs , [&](Job j) { return j->rule()->name.size() ; } ) ;
			if (ro.key==ReqKey::Clear) {
				for( Job  j : jobs  ) j->status = Status::New ;
				for( Node n : nodes ) n->mk_no_src() ;
				Job ::s_clear_frozens() ;
				Node::s_clear_frozens() ;
			}
			for( Job  j : jobs  ) audit( ecr.fd , ro , ro.key==ReqKey::List?Color::Warning:Color::Note , widen(j->rule()->name,w)+' '+mk_file(j->name()              ) ) ;
			for( Node n : nodes ) audit( ecr.fd , ro , ro.key==ReqKey::List?Color::Warning:Color::Note , widen(""             ,w)+' '+mk_file(n->name(),Yes/*exists*/) ) ;
			return true ;
		} else {
			bool           add   = ro.key==ReqKey::Add ;
			size_t         w     = 3/*src*/            ;
			::string       name  ;
			::string       err   ;
			::vector<Job > jobs  ;
			::vector<Node> nodes ;
			//
			auto handle_job = [&](Job j) {
				/**/       if (!j.is_plain(true/*frozen_ok*/)) throw cat("job not found " ,mk_file(j->name())) ;
				if (add) { if ( j.frozen  ()                 ) throw cat("already frozen ",mk_file(j->name())) ; }
				else     { if (!j.frozen  ()                 ) throw cat("not frozen "    ,mk_file(j->name())) ; }
				/**/       if ( j->running()                 ) throw cat("job is running" ,mk_file(j->name())) ;
				//
				w = ::max( w , j->rule()->name.size() ) ;
				jobs.push_back(j) ;
			} ;
			auto handle_node = [&](Node n) {
				if ( add==n.frozen()     ) { ::string nn = n->name() ; throw cat(n.frozen()?"already":"not"," frozen ",mk_file(nn)) ; }
				if ( add && n->is_src () ) { ::string nn = n->name() ; throw cat("cannot freeze source "              ,mk_file(nn)) ; }
				if ( add && n->is_anti() ) { ::string nn = n->name() ; throw cat("cannot freeze anti "                ,mk_file(nn)) ; }
				//
				nodes.push_back(n) ;
			} ;
			//check
			if (ecr.is_job()) {
				handle_job(ecr.job()) ;
			} else {
				bool force = ro.flags[ReqFlag::Force] ;
				for( Node t : ecr.targets() ) {
					t->set_buildable() ;
					Job j = t->actual_job ;
					if      ( add && !j.is_plain(true/*frozen_ok*/)                     ) handle_node(t) ;
					else if ( t->is_src_anti()                                          ) handle_node(t) ;
					else if ( force || (t->status()<=NodeStatus::Makable&&t->conform()) ) handle_job (j) ;
					else {
						Rule r  = j->rule()            ;
						Job  cj = t->conform_job_tgt() ;
						trace("fail",t->buildable,t->conform_idx(),t->status(),cj) ; //!                                                                 exists
						if (+cj) throw cat("target was produced by ",r->name+" instead of ",cj->rule()->name," (use -F to override) : ",mk_file(t->name(),Yes )) ;
						else     throw cat("target was produced by ",r->name+                                " (use -F to override) : ",mk_file(t->name(),Yes )) ;
					}
				}
			}
			throw_if( +nodes && Req::s_n_reqs() , "cannot ",add?"add":"remove"," frozen files while running" ) ;
			// do what is asked
			if (+jobs) {
				trace("jobs",jobs) ;
				Job::s_frozens(add,jobs) ;
				for( Job j : jobs ) {
					if (!add) j->status = Status::New ;
					audit( ecr.fd , ro , add?Color::Warning:Color::Note , widen(j->rule()->name,w)+' '+mk_file(j->name()) ) ;
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
		ReqOptions const& ro = ecr.options ;
		//
		if (_is_mark_glb(ro.key)) {
			::vector<Node> markeds = Node::s_no_triggers() ;
			if (ro.key==ReqKey::Clear) Node::s_clear_no_triggers() ;
			for( Node n : markeds ) audit( ecr.fd , ro , ro.key==ReqKey::List?Color::Warning:Color::Note , mk_file(n->name(),Yes/*exists*/) ) ;
		} else {
			bool           add   = ro.key==ReqKey::Add ;
			::vector<Node> nodes ;
			if (ecr.is_job()) nodes = mk_vector<Node>(ecr.job()->targets()) ;
			else              nodes = ecr.deps()                            ;
			//check
			for( Node n : nodes )
				if (n.no_trigger()==add) {
					audit( ecr.fd , ro , Color::Err , cat("file is ",add?"already":"not"," no-trigger : ",mk_file(n->name(),Yes/*exists*/)) ) ;
					return false ;
				}
			// do what is asked
			Node::s_no_triggers(add,nodes) ;
			for( Node n : nodes ) audit( ecr.fd , ro , add?Color::Warning:Color::Note , mk_file(n->name(),Yes/*exists*/) ) ;
		}
		return true ;
	}

	static Color _node_color( Node n , Bool3 hide=Maybe ) {
		if ( hide==Yes                                 ) return Color::HiddenNote                          ;
		if ( n->ok()==No                               ) return Color::Err                                 ;
		if ( n->crc==Crc::None                         ) return hide==No ? Color::None : Color::HiddenNote ;
		n->set_buildable() ;                                                                                 // for has_file() and is_src_anti()
		if (  n->has_file()==No                        ) return Color::Warning                             ;
		if ( !n->is_src_anti() && !n->has_actual_job() ) return Color::Warning                             ;
		/**/                                             return {}                                         ;
	}

	static Color _job_color( Job j , bool hide=false ) {
		Color color = {} ;
		if      (hide                 ) color = Color::HiddenNote ;
		else if (!j->rule()           ) color = Color::HiddenNote ;
		else if (j->status==Status::Ok) color = Color::Ok         ;
		else if (j.frozen()           ) color = Color::Warning    ;
		else                            color = Color::Err        ;
		return color ;
	}

	static void _audit_node( Fd fd , ReqOptions const& ro , bool verbose , Bool3 hide , ::string const& pfx , Node node , DepDepth lvl=0 ) {
		Color color = _node_color( node , hide ) ;
		//
		if ( verbose || color!=Color::HiddenNote ) {
			if (+pfx) audit( fd , ro , color , pfx+' '+mk_file(node->name()) , false/*as_is*/ , lvl ) ;
			else      audit( fd , ro , color ,         mk_file(node->name()) , false/*as_is*/ , lvl ) ;
		}
	}

	static void _audit_job( Fd fd , ReqOptions const& ro , bool hide , Job job , ::string pfx={} , ::string const& comment={} , ::string const& sfx={} , DepDepth lvl=0 ) {
		Color color      = _job_color( job , hide )      ;
		Rule  rule       = job->rule()                   ;
		bool  porcelaine = ro.flags[ReqFlag::Porcelaine] ;
		::string l          ;
		if (+pfx) l << pfx <<' ' ;
		if (porcelaine) {
			l <<"( "<< mk_py_str(+rule?rule->name:""s) <<" , "<< mk_py_str(job->name()) <<" , "<< mk_py_str(comment) <<" )" ;
		} else {
			if (+rule   ) l << rule->user_name() <<' ' ;
			/**/          l << mk_file(job->name())    ;
			if (+comment) l <<" ("<< comment <<')'     ;
		}
		if (+sfx) l <<' '<< sfx ;
		audit( fd , ro , color , l , porcelaine/*as_is*/ , lvl ) ;
	}
	static void _audit_deps( Fd fd , ReqOptions const& ro , bool hide , Job job , DepDepth lvl=0 ) {
		Rule              rule       = job->rule()                   ;
		bool              porcelaine = ro.flags[ReqFlag::Porcelaine] ;
		bool              verbose    = ro.flags[ReqFlag::Verbose   ] ;
		size_t            wc         = 0                             ;
		size_t            wk         = 0                             ;
		size_t            wf         = 0                             ;
		::umap_ss         rev_map    ;
		::vector<Color  > dep_colors ;                                       // indexed before filtering
		::vector<NodeIdx> dep_groups ;                                       // indexed after  filtering, deps in given group are parallel
		NodeIdx           dep_group  = 0                             ;
		::vmap_s<RegExpr> res        ;
		if (+rule) {
			Rule::RuleMatch m = job->rule_match() ;
			for( auto const& [k,d] : rule->deps_attrs.dep_specs(m) ) {       // this cannot fail as we already have the job
				if (rev_map.try_emplace(d.txt,k).second) {                   // in case of multiple matches, retain first
					if (porcelaine) wk = ::max( wk , mk_py_str(k).size() ) ;
					else            wk = ::max( wk ,           k .size() ) ;
				}
			}
			::vector<Pattern> star_patterns = m.star_patterns() ;
			VarIdx            i             = 0                 ;
			for( MatchKind mk : iota(All<MatchKind>) )
				for( VarIdx mi : rule->matches_iotas[true/*star*/][+mk] ) {
					if (mk!=MatchKind::Target) {                             // deps cannot be found in targets, but they can in side_targets
						::string const& k = rule->matches[mi].first ;
						if (porcelaine) wk = ::max( wk , mk_py_str(k).size() ) ;
						else            wk = ::max( wk ,           k .size() ) ;
						res.emplace_back( k , RegExpr(star_patterns[i]) ) ;
					}
					i++ ;
				}
		}
		for( Dep const& d : job->deps ) {
			Color c = _node_color( d , (Maybe&!d.dflags[Dflag::Required])|hide ) ;
			dep_colors.push_back(c) ;
			if ( !d.parallel                      ) dep_group++ ;
			if ( !verbose && c==Color::HiddenNote ) continue ;
			dep_groups.push_back(dep_group) ;
			if (porcelaine) wc = ::max( wc , mk_py_str(d.crc_str()).size() ) ;
			else            wc = ::max( wc ,           d.crc_str() .size() ) ;
			if (porcelaine) wf = ::max( wf , mk_py_str(d->name()  ).size() ) ;
		}
		NodeIdx di1          = 0 ;                                           // before filtering
		NodeIdx di2          = 0 ;                                           // after  filtering
		NodeIdx n_dep_groups = 0 ;
		if (porcelaine) audit( fd , ro , "(" , true/*as_is*/ , lvl ) ;
		for( Dep const& dep : job->deps ) {
			Color c = dep_colors[di1++] ;
			if ( !verbose && c==Color::HiddenNote ) continue ;
			NodeIdx    dep_group   = dep_groups[di2]                                                       ;
			bool       start_group = di2  ==0                 || dep_group!=dep_groups[di2-1]              ;
			bool       end_group   = di2+1==dep_groups.size() || dep_group!=dep_groups[di2+1]              ;
			auto       it          = dep.dflags[Dflag::Static] ? rev_map.find(dep->name()) : rev_map.end() ;
			::string   dep_key     ;
			di2++ ;
			if (it!=rev_map.end())                                                              dep_key = it->second ;
			else                   for ( auto const& [k,e] : res ) if (+e.match(dep->name())) { dep_key = k          ; break ; }
			if (porcelaine) {
				::string     dep_str = "( " ;
				/**/         dep_str <<              mk_py_str(dep.dflags_str  ())     ;
				/**/         dep_str <<" , "<<       mk_py_str(dep.accesses_str())     ;
				if (verbose) dep_str <<" , "<< widen(mk_py_str(dep.crc_str()     ),wc) ;
				/**/         dep_str <<" , "<< widen(mk_py_str(dep_key           ),wk) ;
				/**/         dep_str <<" , "<< widen(mk_py_str(dep->name()       ),wf) ;
				/**/         dep_str <<" )"                                            ;
				//                                                                                                       as_is
				if      (  start_group &&  end_group ) audit( fd , ro , cat(n_dep_groups?',':' '," { ",dep_str," }"   ) , true , lvl ) ;
				else if (  start_group && !end_group ) audit( fd , ro , cat(n_dep_groups?',':' '," { ",dep_str        ) , true , lvl ) ;
				else if ( !start_group && !end_group ) audit( fd , ro , cat(' '                 ," , ",dep_str        ) , true , lvl ) ;
				else                                   audit( fd , ro , cat(' '                 ," , ",dep_str,"\n  }") , true , lvl ) ;
			} else {
				if ( !c && !dep.up_to_date() ) c = Color::Warning ;
				//
				::string                               dep_str =       dep.dflags_str  ()      ;
				/**/                                   dep_str <<' '<< dep.accesses_str()      ;
				if      ( verbose                    ) dep_str <<' '<< widen(dep.crc_str(),wc) ;
				/**/                                   dep_str <<' '<< widen(dep_key      ,wk) ;
				/**/                                   dep_str <<' '                           ;
				if      (  start_group &&  end_group ) dep_str <<      ' '                     ;
				else if (  start_group && !end_group ) dep_str <<      '/'                     ;
				else if ( !start_group && !end_group ) dep_str <<      '|'                     ;
				else                                   dep_str <<      '\\'                    ;
				/**/                                   dep_str <<' '<< mk_file(dep->name())    ;
				audit( fd , ro , c , dep_str , false/*as_is*/ , lvl ) ;
			}
			n_dep_groups += end_group ;
		}
		if (porcelaine) audit( fd , ro , n_dep_groups==1?",)":")" , true/*as_is*/ , lvl ) ;
	}

	static ::pair<::vmap_ss/*set*/,::vector_s/*keep*/> _mk_env( JobInfo const& job_info ) {
		bool                                        has_end  = +job_info.end                 ;
		::umap_ss                                   de       = mk_umap(job_info.end.dyn_env) ;
		::pair<::vmap_ss/*set*/,::vector_s/*keep*/> set_keep ;
		for( auto const& [k,v] : job_info.start.start.env )
			if      (v!=PassMrkr   ) set_keep.first .emplace_back(k,v       ) ;
			else if (!has_end      ) set_keep.second.push_back   (k         ) ;
			else if (de.contains(k)) set_keep.first .emplace_back(k,de.at(k)) ;
		return set_keep ;
	}

	static ::string _mk_py_str( JobSpace::ViewDescr const& descr , int lvl=-1 ) {
		SWEAR(+descr) ;
		::string nl  = lvl<0 ? " "s : '\n' + ::string(lvl,'\t') ;
		char     tab = lvl<0 ? ' '  : '\t'                      ;
		::string res ;
		if (descr.phys_s.size()==1) {                                                                 // bind case
			SWEAR(!descr.copy_up) ;
			res << mk_py_str(no_slash(descr.phys_s[0])) ;
		} else {                                                                                      // overlay case
			res <<'{'<<tab<< mk_py_str("upper") <<" : "<< mk_py_str(no_slash(descr.phys_s[0])) <<nl ;
			{	First first ;
				res <<','<<tab<< mk_py_str("lower") <<" : (" ;
				for( ::string const& l_s : ::span(&descr.phys_s[1],descr.phys_s.size()-1) ) res << first("",",") << mk_py_str(no_slash(l_s)) ;
				res << first("",",","") << ')'<<nl ;
			}
			if (+descr.copy_up) {
				First first ;
				res <<','<<tab<< mk_py_str("copy_up") <<" : (" ;
				for( ::string const& cu : descr.copy_up ) res << first("",",") << mk_py_str(cu) ;
				res << first("",",","") << ')'<<nl ;
			}
			res << '}' ;
		}
		return res ;
	}

	static ::string _mk_gen_script_line( Job job , ReqOptions const& ro , JobInfo&& job_info , ::string const& dbg_dir_s , ::string const& key ) {
		JobStartRpcReply& jsrr      = job_info.start.start ;
		AutodepEnv      & ade       = jsrr.autodep_env     ;
		JobSpace        & job_space = jsrr.job_space       ;
		::string          tmp_dir_s ;
		{	bool add_key = false ;
			if (ro.flags[ReqFlag::TmpDir]) { tmp_dir_s = with_slash(ro.flag_args[+ReqFlag::TmpDir]) ; goto Tmp ; }
			if (ro.flags[ReqFlag::StdTmp])                                                            goto Tmp ;
			for( auto const& [k,v] : jsrr.env ) {
				if (k!="TMPDIR") continue ;
				/**/                                                                    if (v!=PassMrkr ) { tmp_dir_s = with_slash(v ) ; add_key = true ; goto Tmp ; }
				for( auto const& [k2,v2] : g_config->backends[+BackendTag::Local].env ) if (k2=="TMPDIR") { tmp_dir_s = with_slash(v2) ; add_key = true ; goto Tmp ; }
			}
		Tmp :
			if      (!tmp_dir_s) tmp_dir_s = *g_repo_root_s+dbg_dir_s+"tmp/" ;
			else if (add_key   ) tmp_dir_s << g_config->key << "/0/"         ;                 // 0 is for small_id which does not exist for debug
		}
		ade.repo_root_s = job_space.repo_view_s | *g_repo_root_s ;
		ade.tmp_dir_s   = job_space.tmp_view_s  | tmp_dir_s      ;
		//
		::string res = "script = gen_script(\n" ;
		//
		/**/                          res <<  "\tautodep_method = " << mk_py_str(snake   (jsrr.method            )) << '\n' ;
		if (ade.auto_mkdir          ) res << ",\tauto_mkdir     = " << mk_py_str(         ade.auto_mkdir          ) << '\n' ;
		if (+jsrr.chroot_info.dir_s ) res << ",\tchroot_dir     = " << mk_py_str(no_slash(jsrr.chroot_info.dir_s )) << '\n' ;
		if (+jsrr.chroot_info.action) res << ",\tchroot_action  = " << mk_py_str(snake   (jsrr.chroot_info.action)) << '\n' ;
		/**/                          res << ",\tdebug_dir      = " << mk_py_str(no_slash(dbg_dir_s              )) << '\n' ;
		/**/                          res << ",\tis_python      = " << mk_py_str(         job->rule()->is_python  ) << '\n' ;
		/**/                          res << ",\tkey            = " << mk_py_str(         key                     ) << '\n' ;
		/**/                          res << ",\tjob            = " <<                    +job                      << '\n' ;
		/**/                          res << ",\tlink_support   = " << mk_py_str(snake   (ade.lnk_support        )) << '\n' ;
		if (+jsrr.phy_lmake_root_s  ) res << ",\tlmake_root     = " << mk_py_str(no_slash(jsrr.phy_lmake_root_s  )) << '\n' ;
		if (+job_space.lmake_view_s ) res << ",\tlmake_view     = " << mk_py_str(no_slash(job_space.lmake_view_s )) << '\n' ;
		/**/                          res << ",\tname           = " << mk_py_str(         job->name()             ) << '\n' ;
		if (ade.readdir_ok          ) res << ",\treaddir_ok     = " << mk_py_str(         ade.readdir_ok          ) << '\n' ;
		/**/                          res << ",\trepo_root      = " << mk_py_str(no_slash(*g_repo_root_s         )) << '\n' ;
		if (+job_space.repo_view_s  ) res << ",\trepo_view      = " << mk_py_str(no_slash(job_space.repo_view_s  )) << '\n' ;
		if (+jsrr.stdout            ) res << ",\tstdin          = " << mk_py_str(         jsrr.stdin              ) << '\n' ;
		if (+jsrr.stdin             ) res << ",\tstdout         = " << mk_py_str(         jsrr.stdout             ) << '\n' ;
		if (+ade.sub_repo_s         ) res << ",\tsub_repo       = " << mk_py_str(no_slash(ade.sub_repo_s         )) << '\n' ;
		/**/                          res << ",\ttmp_dir        = " << mk_py_str(no_slash(tmp_dir_s              )) << '\n' ;
		if (+job_space.tmp_view_s   ) res << ",\ttmp_view       = " << mk_py_str(no_slash(job_space.tmp_view_s   )) << '\n' ;
		//
		res << ",\tcmd =\n" << mk_py_str(jsrr.cmd) <<'\n' ;
		//
		::pair<::vmap_ss/*set*/,::vector_s/*keep*/> env     = _mk_env(job_info) ;
		::map_ss                                    env_map = mk_map(env.first) ;
		//
		jsrr.update_env( /*inout*/env_map , *g_repo_root_s , tmp_dir_s ) ;
		//
		if (+env_map) {
			res << ",\tenv = {" ;
			First first ;
			for( auto const& [k,v] : env_map ) res << first("\n\t\t",",\t") << mk_py_str(k) <<" : "<< mk_py_str(v) <<"\n\t" ;
			res << "}\n" ;
		}
		if (+env.second) {
			res << ",\tkeep_env = (" ;
			First first ;
			for( ::string const& k : env.second ) res << first("",",") << mk_py_str(k) ;
			res << first("",",","") << ")\n" ;
		}
		{	res << ",\tinterpreter = (" ;
			First first ;
			for( ::string const& c : jsrr.interpreter ) res << first("",",") << mk_py_str(c) ;
			res << first("",",","") << ")\n" ;
		}
		if ( mk_simple_cmd_line( jsrr.interpreter , ::move(jsrr.cmd) , env_map ) ) {           // jsrr.interpreter is now the entire cmd line
			res << ",\tsimple_cmd_line = (" ;
			First first ;
			for( ::string const& c : jsrr.interpreter ) res << first("",",") << mk_py_str(c) ; // .
			res << first("",",","") << ")\n" ;
		}
		{	First first ;
			res << ",\tpre_actions = {" ;
			for( auto const& [t,a] : job->pre_actions(job->rule_match(),ro.flags[ReqFlag::NoIncremental]) )
				res << first("\n\t\t",",\t") << mk_py_str(t->name()) <<" : "<< mk_py_str(snake(a.tag)) <<"\n\t" ;
			res << "}\n" ;
		}
		if (+*g_src_dirs_s) {
			res << ",\tsource_dirs = (" ;
			First first ;
			for( ::string const& sd_s : *g_src_dirs_s ) res << first("\n\t\t",",\t") << mk_py_str(no_slash(sd_s)) << "\n\t" ;
			res << first("",",","") << ")\n" ;
		}
		{	res << ",\tstatic_deps = (" ;
			First first ;
			for( Dep const& d : job->deps )
				if (d.dflags[Dflag::Static]) res << first("\n\t\t",",\t") << mk_py_str(d->name()) << "\n\t" ;
			res << first("",",","") << ")\n" ;
		}
		{	res << ",\tstatic_targets = (" ;
			First first ;
			for( Target const& t : job->targets() )
				if ( t.tflags[Tflag::Target] && t.tflags[Tflag::Static] ) res << first("\n\t\t",",\t") << mk_py_str(t->name()) << "\n\t" ;
			res << first("",",","") << ")\n" ;
		}
		{	res << ",\tviews = {" ;
			First first1 ;
			for( auto const& [view_s,vd] : job_space.views ) if (+vd) res << first1("\n\t\t",",\t") << mk_py_str(no_slash(view_s)) << " : " << _mk_py_str( vd , 2/*lvl*/ ) << "\n\t" ;
			res << "}\n" ;
		}
		res << ")\n" ;
		return res ;
	}

	static Job _job_from_target( Fd fd , ReqOptions const& ro , Node target , DepDepth lvl=0 ) {
		if ( JobTgt aj=target->actual_job       ; aj.is_plain()                        ) return aj  ;
		if (                                      target->status()>NodeStatus::Makable ) goto NoJob ;
		if( JobTgt cj=target->conform_job_tgt() ; cj.is_plain()                        ) return cj  ;
	NoJob :
		bool porcelaine = ro.flags[ReqFlag::Porcelaine] ;
		target->set_buildable() ;
		if (porcelaine) {
			if (ro.key!=ReqKey::Info) audit( fd , ro , "None" , true/*as_is*/ , lvl ) ;
		} else {
			if ( target->buildable>Buildable::No && !target->is_src() ) { //!                                   as_is
				audit( fd , ro , Color::Err  , "target not built"                                             , true  , lvl   ) ;
				audit( fd , ro , Color::Note , "consider : lmake "+mk_file(target->name(),FileDisplay::Shell) , false , lvl+1 ) ;
			}
		}
		return {} ;
	}

	static bool/*ok*/ _debug(EngineClosureReq const& ecr) {
		Trace trace("debug") ;
		ReqOptions const& ro = ecr.options ;
		//
		Job job ;
		if (ecr.is_job()) {
			job = ecr.job() ;
		} else {
			::vector<Node> targets = ecr.targets() ;
			throw_unless( targets.size()==1 , "can only debug a single target" ) ;
			job = _job_from_target(ecr.fd,ro,targets[0]) ;
		}
		if (!job                             ) throw "no job found"s                                       ;
		if (!job->is_plain(true/*frozen_ok*/)) throw cat("cannot debug ",job->rule()->user_name()," jobs") ;
		//
		JobInfo job_info = job.job_info() ;
		if (!job_info.start.start) {
			audit( ecr.fd , ro , Color::Note , "no info available" ) ;
			return false ;
		}
		//
		::string const& key = ro.flag_args[+ReqFlag::Key] ;
		auto            it  = g_config->dbg_tab.find(key) ;
		throw_unless( it!=g_config->dbg_tab.end() , "unknown debug method ",key ) ;
		throw_unless( +it->second                 , "empty debug method "  ,key ) ;
		::string runner    = split(it->second)[0]                       ;                                                          // allow doc after first word
		::string dbg_dir_s = job->ancillary_file(AncillaryTag::Dbg)+'/' ;
		//
		::string script_file     = dbg_dir_s+"script"     ;
		::string gen_script_file = dbg_dir_s+"gen_script" ;
		{	::string gen_script ;
			gen_script << "#!" PYTHON "\n"                                                                                       ;
			gen_script << "import sys\n"                                                                                         ;
			gen_script << "import os\n"                                                                                          ;
			gen_script << "sys.path[0:0] = ("<<mk_py_str(*g_lmake_root_s+"lib")<<','<<mk_py_str(no_slash(*g_repo_root_s))<<")\n" ; // repo_root is not in path as script is in LMAKE/debug/<job>
			gen_script << "from "<<runner<<" import gen_script\n"                                                                ;
			gen_script << _mk_gen_script_line(job,ro,::move(job_info),dbg_dir_s,key)                                             ;
			gen_script << "print( script , file=open("<<mk_py_str(script_file)<<",'w') )\n"                                      ;
			gen_script << "os.chmod("<<mk_py_str(script_file)<<",0o755)\n"                                                       ;
			AcFd( gen_script_file , {O_WRONLY|O_TRUNC|O_CREAT,0777/*mod*/} ).write( gen_script ) ;
		}                                                                                                                          // ensure gen_script is closed before launching it
		{	SavPyLdLibraryPath spllp ;
			Child              child ;
			child.stdin    = {}                ;                                                                                   // no input
			child.cmd_line = {gen_script_file} ;
			child.spawn() ;
			if (!child.wait_ok()) throw "cannot generate debug script "+script_file ;
		}
		//
		audit_file( ecr.fd , ::move(script_file) ) ;
		return true ;
	}

	static bool/*ok*/ _forget(EngineClosureReq const& ecr) {
		ReqOptions const& ro = ecr.options ;
		bool              ok = true        ;
		switch (ro.key) {
			case ReqKey::None :
				if (ecr.is_job()) {
					Job j = ecr.job() ;
					throw_unless( +j , "job not found" ) ;
					ok = j->forget( ro.flags[ReqFlag::Targets] , ro.flags[ReqFlag::Deps] ) ;
				} else {
					for( Node t : ecr.targets() ) ok &= t->forget( ro.flags[ReqFlag::Targets] , ro.flags[ReqFlag::Deps] ) ;
				}
			break ;
			case ReqKey::Resources : {
				throw_if( Req::s_n_reqs() , "cannot forget resources while jobs are running" ) ;
				::uset<Rule> refreshed ;
				for( RuleCrc rc : Persistent::rule_crc_lst() ) if ( RuleCrcData& rcd=rc.data() ; rcd.state==RuleCrcState::RsrcsOld) {
					rcd.state = RuleCrcState::RsrcsForgotten ;
					if (refreshed.insert(rcd.rule).second) audit( ecr.fd , ro , Color::Note , "refresh "+rcd.rule->user_name() , true/*as_is*/ ) ;
				}
			} break ;
		DF}           // NO_COV
		return ok ;
	}

	static bool/*ok*/ _mark(EngineClosureReq const& ecr) {
		if (ecr.options.flags[ReqFlag::Freeze   ]) return _freeze    (ecr) ;
		if (ecr.options.flags[ReqFlag::NoTrigger]) return _no_trigger(ecr) ;
		throw "no mark specified"s ;
	}

	template<class T> struct Show {
		static constexpr Color HN = Color::HiddenNote ;
		// cxtors & casts
		Show( Fd fd_ , ReqOptions const& ro_ , DepDepth lvl_=0 ) : fd{fd_} , ro{ro_} , lvl{lvl_} , verbose{ro_.flags[ReqFlag::Verbose]} , porcelaine{ro_.flags[ReqFlag::Porcelaine]} {
			if (porcelaine) audit( fd , ro , verbose?"("                :"{" , true/*as_is*/ , lvl ) ; // if verbose, order is guaranteed so that dependents appear before deps
		}
		~Show() {
			if (porcelaine) audit( fd , ro , verbose?first(")",",)",")"):"}" , true/*as_is*/ , lvl ) ; // beware of singletons
		}
		// data
		Fd                 fd         ;
		ReqOptions const&  ro         ;
		DepDepth           lvl        = 0                ;
		::uset<Job >       job_seen   = {}               ;
		::uset<Node>       node_seen  = {}               ;
		::vector<T>        backlog    = {}               ;
		bool               verbose    = false/*garbage*/ ;
		bool               porcelaine = false/*garbage*/ ;
		First              first      ;
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
			//
			node->set_buildable() ;
			if (!node->is_src_anti()) {
				if (verbose) backlog.push_back(node) ;
				lvl += verbose ;
				for( Job j : node->candidate_job_tgts() ) show_job(j) ;
				lvl -= verbose ;
				if (+backlog) backlog.pop_back() ;
			} else if (node->status()<=NodeStatus::Makable) {
				Color    c = node->buildable==Buildable::Src ? Color::None : Color::Warning ;
				DepDepth l = lvl - backlog.size()                                           ;
				if (porcelaine) { //!                                                                               as_is
					for( Node n : backlog ) audit( fd , ro ,      cat(first(' ',','),' ',mk_py_str(n   ->name())) , true  , l++ ) ;
					/**/                    audit( fd , ro ,      cat(first(' ',','),' ',mk_py_str(node->name())) , true  , lvl ) ;
				} else {
					for( Node n : backlog ) audit( fd , ro , HN ,                        mk_file  (n   ->name())  , false , l++ ) ;
					/**/                    audit( fd , ro , c  ,                        mk_file  (node->name())  , false , lvl ) ;
				}
				backlog.clear() ;
			}
		}
	} ;

	struct ShowRunning : Show<Job> {
		static constexpr BitMap<JobStep> InterestingSteps = { JobStep::Dep/*waiting*/ , JobStep::Queued , JobStep::Exec } ;
		using Show<Job>::Show ;
		// services
		void show_job(Job job) {
			JobStep step = {} ;
			for( Req r : Req::s_reqs_by_start() )
				if ( JobStep s = job->c_req_info(r).step() ; InterestingSteps[s] && step!=s ) { // process job as soon as one Req is waiting/running, and this must be coherent
					SWEAR( !step , step,s ) ;
					step = s ;
				}
			Color c   = {}             ;
			char  hdr = '?'/*garbage*/ ;
			switch (step) {
				case JobStep::Dep    :                               break ;
				case JobStep::Queued : c = Color::Note ; hdr = 'Q' ; break ;
				case JobStep::Exec   :                   hdr = 'R' ; break ;
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
					if (porcelaine) { //!                                                                                                                                             as_is
						for( Job j : backlog ) audit( fd , ro ,      cat(first(' ',','),' ',"( '",'W',"' , ",mk_py_str(j  ->rule()->user_name())," , ",mk_py_str(j  ->name())," )") , true  , l++ ) ;
						/**/                   audit( fd , ro ,      cat(first(' ',','),' ',"( '",hdr,"' , ",mk_py_str(job->rule()->user_name())," , ",mk_py_str(job->name())," )") , true  , lvl ) ;
					} else {
						for( Job j : backlog ) audit( fd , ro , HN , cat(                         'W',' '   ,          j  ->rule()->user_name() ,' '  ,mk_file  (j  ->name())     ) , false , l++ ) ;
						/**/                   audit( fd , ro , c  , cat(                         hdr,' '   ,          job->rule()->user_name() ,' '  ,mk_file  (job->name())     ) , false , lvl ) ;
					}
					backlog.clear() ;
					return ;
				}
			DF}                                                                                 // NO_COV
			lvl += verbose ;
			for( Dep const& dep : job->deps ) show_node(dep) ;
			lvl -= verbose ;
			if (+backlog) backlog.pop_back() ;
		}
		void show_node(Node node) {
			for( Req r : Req::s_reqs_by_start() )
				if ( NodeReqInfo const& cri = node->c_req_info(r) ; cri.waiting() ) {           // process node as soon as one Req is waiting
					if (!node_seen.insert(node).second) return ;
					for( Job j : node->conform_job_tgts(cri) ) show_job(j) ;
					return ;
				}
		}
	} ;

	static void _show_job( Fd fd , ReqOptions const& ro , Job job , Node target={} , DepDepth lvl=0 ) {
		Trace trace("show_job",ro.key,job) ;
		bool              verbose    = ro.flags[ReqFlag::Verbose]    ;
		Rule              rule       = job->rule()                   ;
		JobInfo           job_info   = job.job_info()                ;
		JobStartRpcReq  & pre_start  = job_info.start.pre_start      ;
		JobStartRpcReply& start      = job_info.start.start          ;
		JobEndRpcReq    & end        = job_info.end                  ;
		JobDigest<>     & digest     = end.digest                    ;
		bool              porcelaine = ro.flags[ReqFlag::Porcelaine] ;
		switch (ro.key) {
			case ReqKey::Cmd    :
			case ReqKey::Env    :
			case ReqKey::Info   :
			case ReqKey::Stderr :
			case ReqKey::Stdout :
			case ReqKey::Trace  : {
				if ( +rule && !rule->is_plain() ) {
					switch (ro.key) {               // START_OF_NO_COV defensive programming : special jobs are not built and do not reach here
						case ReqKey::Info   :
						case ReqKey::Stderr : {
							MsgStderr msg_stderr = job->special_msg_stderr() ;
							if (porcelaine) { //!                                           as_is
								if (verbose) audit( fd , ro , "None"                       , true , lvl+1 , '(' ) ;
								if (verbose) audit( fd , ro , mk_py_str(msg_stderr.msg   ) , true , lvl+1 , ',' ) ;
								if (verbose) audit( fd , ro , ","                          , true , lvl         ) ;
								/**/         audit( fd , ro , mk_py_str(msg_stderr.stderr) , true               ) ;
								if (verbose) audit( fd , ro , ")"                          , true , lvl         ) ;
							} else { //!                                           as_is
								audit( fd , ro , Color::Note , msg_stderr.msg    , false , lvl+1 ) ;
								audit( fd , ro ,               msg_stderr.stderr , true          ) ;
							}
						} break ;
						case ReqKey::Cmd    :
						case ReqKey::Env    :
						case ReqKey::Stdout :
						case ReqKey::Trace  : //!                                                         as_is
							if (porcelaine) audit( fd , ro ,              "None"                         , true , lvl+1 ) ;
							else            audit( fd , ro , Color::Err , cat("no ",ro.key," available") , true , lvl+1 ) ;
						break ;
					DF}                             // END_OF_NO_COV
				} else {
					//
					if (pre_start.job) SWEAR( pre_start.job==+job , pre_start.job,+job ) ;
					//
					switch (ro.key) {
						case ReqKey::Env : {
							::pair<::vmap_ss/*set*/,::vector_s/*keep*/> env = _mk_env(job_info) ;
							size_t                                      w   = 0                 ;
							if (porcelaine) {
								char sep = '{' ;
								for( auto     const& [k,v] : env.first  ) w = ::max( w , mk_py_str(k).size() ) ;
								for( ::string const&  k    : env.second ) w = ::max( w , mk_py_str(k).size() ) ; //!                   as_is
								for( auto     const& [k,v] : env.first  ) { audit( fd , ro , widen(mk_py_str(k),w)+" : "+mk_py_str(v) , true , lvl+1 , sep ) ; sep = ',' ; }
								for( ::string const&  k    : env.second ) { audit( fd , ro , widen(mk_py_str(k),w)+" : ..."           , true , lvl+1 , sep ) ; sep = ',' ; }
								/**/                                        audit( fd , ro , "}"                                      , true , lvl         ) ;
							} else if (+start) {
								for( auto     const& [k,v] : env.first  ) w = ::max( w , k.size() ) ;
								for( ::string const&  k    : env.second ) w = ::max( w , k.size() ) ; //!      as_is
								for( auto     const& [k,v] : env.first  ) audit( fd , ro , widen(k,w)+" : "+v , true , lvl ) ;
								for( ::string const&  k    : env.second ) audit( fd , ro , widen(k,w)+" ..."  , true , lvl ) ;
							} else {
								audit( fd , ro , Color::Note , "no info available" , true/*as_is*/ , lvl ) ;
							}
						} break ;
						case ReqKey::Cmd : //!                                                        as_is
							if      (porcelaine) audit( fd , ro ,               mk_py_str(start.cmd) , true                  ) ;
							else if (+start    ) audit( fd , ro ,                         start.cmd  , true , bool(lvl),'\t' ) ;
							else                 audit( fd , ro , Color::Note , "no info available"  , true ,      lvl       ) ;
						break ;
						case ReqKey::Stdout : //!                                         as_is
							if      (porcelaine) audit( fd , ro , mk_py_str(end.stdout) , true                  ) ;
							else if (+end      ) audit( fd , ro ,           end.stdout  , true , bool(lvl),'\t' ) ;
							else {
								audit( fd , ro , Color::Note , "no info available" , true , lvl ) ;
								if (+start) {
									::string args ;
									if (+target) args = mk_file(target->name(),FileDisplay::Shell)                                    ;
									else         args = "-R "+mk_shell_str(rule->name)+" -J "+mk_file(job->name(),FileDisplay::Shell) ;
									audit( fd , ro , Color::Note , "consider : lmake -o "+args , false/*as_is*/ , lvl ) ;
								}
							}
						break ;
						case ReqKey::Stderr :
							if (porcelaine) { //!                                               as_is
								if (verbose) audit( fd , ro , mk_py_str(pre_start.msg        ) , true , lvl+1 , '(' ) ;
								if (verbose) audit( fd , ro , mk_py_str(end.msg_stderr.msg   ) , true , lvl+1 , ',' ) ;
								if (verbose) audit( fd , ro , ","                              , true , lvl         ) ;
								/**/         audit( fd , ro , mk_py_str(end.msg_stderr.stderr) , true               ) ;
								if (verbose) audit( fd , ro , ")"                              , true , lvl         ) ;
							} else if ( +end || (+start&&verbose) ) {
								if ( +start && verbose ) audit( fd , ro , Color::Note , pre_start.msg         , false ,      lvl       ) ;
								if ( +end   && verbose ) audit( fd , ro , Color::Note , end.msg_stderr.msg    , false ,      lvl       ) ;
								if ( +end              ) audit( fd , ro ,               end.msg_stderr.stderr , true  , bool(lvl),'\t' ) ; // ensure internal alignment of stderr is maintained
							} else {
								audit( fd , ro , Color::Note , "no info available" , true , lvl ) ;
							}
						break ;
						case ReqKey::Trace : {
							if (!end) { audit( fd , ro , Color::Note , "no info available" , true/*as_is*/ , lvl ) ; break ; }
							::sort( end.user_trace , [](UserTraceEntry const& a , UserTraceEntry const& b )->bool { return ::pair(a.date,a.file)<::pair(b.date,b.file) ; } ) ;
							if (porcelaine) {
								size_t wk  = ::max<size_t>( end.user_trace , [&](UserTraceEntry const& e) { return mk_py_str(e.step()).size() ; } ) ;
								size_t wf  = ::max<size_t>( end.user_trace , [&](UserTraceEntry const& e) { return mk_py_str(e.file  ).size() ; } ) ;
								char   sep = '('                                                                                                    ;
								for( UserTraceEntry const& e : end.user_trace ) {
									::string l = "( "+mk_py_str(e.date.str(3/*prec*/,true/*in_day*/))+" , "+widen(mk_py_str(e.step()),wk)+" , "+widen(mk_py_str(e.file),wf)+" )" ;
									audit( fd , ro , l , true/*as_is*/ , lvl+1 , sep ) ;
									sep = ',' ;
								}
								audit( fd , ro , ")" , true/*as_is*/ , lvl ) ;
							} else {
								size_t w = ::max<size_t>( end.user_trace , [&](UserTraceEntry const& e) { return e.step().size() ; } ) ;
								for( UserTraceEntry const& e : end.user_trace ) //!                                                      as_is
									if (+e.file) audit( fd , ro , e.date.str(3/*prec*/,true/*in_day*/)+' '+widen(e.step(),w)+' '+e.file , true , lvl+1 ) ;
									else         audit( fd , ro , e.date.str(3/*prec*/,true/*in_day*/)+' '+      e.step()               , true , lvl+1 ) ;
							}
						} break ;
						case ReqKey::Info : {
							struct Entry {
								::string txt     ;
								Color    color   = {}   ;
								bool     protect = true ;
							} ;
							::string        su         = porcelaine ? ""s : ro.startup_dir_s ;
							::vmap_s<Entry> tab        ;
							auto push_entry = [&]( const char* k , ::string const& v , Color c={} , bool protect=true ) {
								tab.emplace_back( k , Entry{v,c,protect} ) ;
							} ;
							//
							push_entry( "rule" , rule->user_name() ) ;
							push_entry( "job"  , job->name()       ) ;
							//
							::string ids ;
							if (porcelaine) {
								ids = cat("{ 'job':",+job) ;
								if (+start) {
									if (pre_start.seq_id==SeqId(-1)) {
										ids << " , 'downloaded_from_cache':True" ;
									} else {
										if (start.small_id  ) ids << " , 'small':"<<start.small_id   ;
										if (pre_start.seq_id) ids << " , 'seq':"  <<pre_start.seq_id ;
									}
								}
								ids += " }" ;
							} else {
								ids = cat("job=",+job) ;
								if (+start) {
									if (pre_start.seq_id==SeqId(-1)) {
										ids << " , downloaded_from_cache" ;
									} else {
										if (start.small_id  ) ids << " , small:"<<start.small_id   ;
										if (pre_start.seq_id) ids << " , seq:"  <<pre_start.seq_id ;
									}
								}
							}
							//
							push_entry("ids",ids,Color::None,false/*protect*/) ;
							if ( Node n=job->asking() ; +n ) {
								while ( +n->asking && n->asking.is_a<Node>() ) n = Node(n->asking) ;
								if (+n->asking) push_entry("required by",localize(mk_file(Job(n->asking)->name()),su)) ;
								else            push_entry("required by",localize(mk_file(    n         ->name()),su)) ;
							}
							if (job->cache_hit_info!=CacheHitInfo::NoCache) push_entry( "cache hit info" , CacheHitInfoStrs[+job->cache_hit_info].second ) ;
							if (+start) {
								JobInfoStart const& rs       = job_info.start          ;
								SubmitAttrs  const& sa       = rs.submit_attrs         ;
								::string            pressure = sa.pressure.short_str() ;
								//
								if (+sa.reason            ) push_entry( "reason" , localize(reason_str(sa.reason),su)     ) ;
								if (pre_start.service.addr) push_entry( "host"   , SockFd::s_host(pre_start.service.addr) ) ;
								//
								if (+rs.eta) {
									if (porcelaine) push_entry( "scheduling" , "( "+mk_py_str(rs.eta.str())+" , "+::to_string(double(sa.pressure))+" )"      , Color::None,false/*protect*/ ) ;
									else            push_entry( "scheduling" ,                rs.eta.str() +" - "+                   sa.pressure.short_str()                                ) ;
								}
								//
								if (+start.chroot_info.dir_s     ) push_entry( "chroot_dir"    , no_slash (start.chroot_info.dir_s     ) ) ;
								if (+start.chroot_info.action    ) push_entry( "chroot_action" , snake_str(start.chroot_info.action    ) ) ;
								if (+start.phy_lmake_root_s      ) push_entry( "lmake_root"    , no_slash (start.phy_lmake_root_s      ) ) ;
								if (+start.job_space.lmake_view_s) push_entry( "lmake_view"    , no_slash (start.job_space.lmake_view_s) ) ;
								if (+start.job_space.repo_view_s ) push_entry( "repo_view"     , no_slash (start.job_space.repo_view_s ) ) ;
								if (+start.job_space.tmp_view_s  ) push_entry( "tmp_view"      , no_slash (start.job_space.tmp_view_s  ) ) ;
								if (+start.autodep_env.sub_repo_s) push_entry( "sub_repo"      , no_slash (start.autodep_env.sub_repo_s) ) ;
								if ( start.autodep_env.readdir_ok) push_entry( "readdir_ok"    , "true"                                  ) ;
								if ( start.autodep_env.auto_mkdir) push_entry( "auto_mkdir"    , "true"                                  ) ;
								/**/                               push_entry( "autodep"       , snake_str(start.method)                 ) ;
								if (+start.timeout               ) push_entry( "timeout"       , start.timeout.short_str()               ) ;
								if ( start.use_script            ) push_entry( "use_script"    , "true"                                  ) ;
								//
								if      (job->backend==BackendTag::Local  ) SWEAR(sa.used_backend==BackendTag::Local) ;
								else if (job->backend==BackendTag::Unknown) push_entry( "backend" , snake_str(sa.used_backend)                                         ) ;
								else if (sa.used_backend==job->backend    ) push_entry( "backend" , snake_str(job->backend   )                                         ) ;
								else                                        push_entry( "backend" , snake_str(job->backend   )+" -> "+sa.used_backend , Color::Warning ) ;
							}
							//
							::map_ss allocated_rsrcs = mk_map(job_info.start.rsrcs) ;
							::map_ss required_rsrcs  ;
							try {
								required_rsrcs = mk_map( rule->submit_rsrcs_attrs.eval( job , ::ref(Rule::RuleMatch()) , &::ref(::vmap_s<DepDigest>()) ).rsrcs ) ; // dont care about deps
							} catch(MsgStderr const&) {}
							//
							if (job->run_status!=RunStatus::Ok) push_entry( "run status" , snake_str(RunStatus(job->run_status)) , Color::Err ) ;
							if (+end) {
								/**/              push_entry( "end date" , end.end_date.str(3/*prec*/) ) ;
								if (+end.os_info) push_entry( "os"       , end.os_info                 ) ;
								Color status_color =
									StatusAttrs[+digest.status].second.first==Yes   ? Color::Ok
								:	StatusAttrs[+digest.status].second.first==Maybe ? Color::Note
								:	                                                  Color::Err
								;
								/**/                    push_entry( "status"      , snake_str(digest.status) , status_color ) ;
								if (digest.incremental) push_entry( "incremental" , "true"                                  ) ;
							}
							if ( +end && digest.status>Status::Early ) {
								// no need to localize phy_tmp_dir as this is an absolute dir
								if (+start.job_space.tmp_view_s) push_entry( "physical tmp dir" , no_slash(end.phy_tmp_dir_s) ) ;
								else                             push_entry( "tmp dir"          , no_slash(end.phy_tmp_dir_s) ) ;
								//
								if (porcelaine) { //!                                                                                    color protect
									/**/                      push_entry( "rc"                    , wstatus_str(end.wstatus)             , {} , true  ) ;
									/**/                      push_entry( "cpu time"              , ::to_string(double(end.stats.cpu  )) , {} , false ) ;
									/**/                      push_entry( "elapsed in job"        , ::to_string(double(end.stats.job  )) , {} , false ) ;
									/**/                      push_entry( "elapsed total"         , ::to_string(double(digest.exe_time)) , {} , false ) ;
									/**/                      push_entry( "used mem"              , cat        (end.stats.mem          ) , {} , false ) ;
									/**/                      push_entry( "cost"                  , ::to_string(double(job->cost()    )) , {} , false ) ;
									/**/                      push_entry( "total size"            , cat        (end.total_sz           ) , {} , false ) ;
									if ( end.total_z_sz     ) push_entry( "total compressed size" , cat        (end.total_z_sz         ) , {} , false ) ;
									if ( verbose && +target ) push_entry( "checksum"              , ::string   (target->crc            ) , {} , true  ) ;
								} else {
									::string const& mem_rsrc_str = allocated_rsrcs.contains("mem") ? allocated_rsrcs.at("mem") : required_rsrcs.contains("mem") ? required_rsrcs.at("mem") : ""s ;
									size_t          mem_rsrc     = +mem_rsrc_str?from_string_with_unit(mem_rsrc_str):0                                                                           ;
									bool            overflow     = end.stats.mem > mem_rsrc                                                                                                      ;
									::string        mem_str      = to_short_string_with_unit(end.stats.mem)+'B'                                                                                  ;
									if ( overflow && mem_rsrc ) mem_str += " > "+mem_rsrc_str+'B' ;
									::string rc_str   = wstatus_str(end.wstatus) + (wstatus_ok(end.wstatus)&&+end.msg_stderr.stderr?" (with non-empty stderr)":"") ;
									Color    rc_color = wstatus_ok(end.wstatus) ? Color::Ok : Color::Err                                                           ;
									if ( rc_color==Color::Ok && +end.msg_stderr.stderr ) rc_color = job->status==Status::Ok ? Color::Warning : Color::Err ;
									/**/                      push_entry( "rc"                 , rc_str                                        , rc_color                            ) ;
									/**/                      push_entry( "cpu time"           , end.stats.cpu  .short_str()                                                         ) ;
									/**/                      push_entry( "elapsed in job"     , end.stats.job  .short_str()                                                         ) ;
									/**/                      push_entry( "elapsed total"      , digest.exe_time.short_str()                                                         ) ;
									/**/                      push_entry( "used mem"           , mem_str                                       , overflow?Color::Warning:Color::None ) ;
									/**/                      push_entry( "cost"               , job->cost()     .short_str()                                                        ) ;
									/**/                      push_entry( "total targets size" , to_short_string_with_unit(end.total_sz  )+'B'                                       ) ;
									if ( end.total_z_sz     ) push_entry( "compressed size"    , to_short_string_with_unit(end.total_z_sz)+'B'                                       ) ;
									if ( verbose && +target ) push_entry( "checksum"           , ::string(target->crc)                                                               ) ;
								}
							}
							//
							if (+pre_start.msg     ) push_entry( "start message" , localize(pre_start.msg     ,su) ) ;
							if (+end.msg_stderr.msg) push_entry( "message"       , localize(end.msg_stderr.msg,su) ) ;
							// generate output
							if (porcelaine) {
								auto audit_map = [&]( ::string const& key , ::map_ss const& m , bool protect ) {
									if (!m) return ;
									size_t w   = ::max<size_t>( m , [&](auto const& k_v) { return mk_py_str(k_v.first).size() ; } ) ;
									char   sep = ' '                                                                                ;
									audit( fd , ro , mk_py_str(key)+" : {" , true/*as_is*/ , lvl+1 , ',' ) ;
									for( auto const& [k,v] : m ) {
										::string v_str ;
										if      (!protect                    ) v_str = v                             ;
										else if (k=="cpu"||k=="mem"||k=="tmp") v_str = cat(from_string_with_unit(v)) ;
										else                                   v_str = mk_py_str(v)                  ;
										audit( fd , ro , widen(mk_py_str(k),w)+" : "+v_str , true/*as_is*/ , lvl+2 , sep ) ;
										sep = ',' ;
									}
									audit( fd , ro , "}" , true/*as_is*/ , lvl+1 ) ;
								} ;
								size_t   w     = ::max<size_t>( tab , [&](auto const& k_e) { return mk_py_str(k_e.first).size() ; } ) ;
								::map_ss views ;
								for( auto const& [v_s,vd] : start.job_space.views ) if (+vd) views[no_slash(v_s)] = _mk_py_str(vd) ;
								char sep = '{' ;
								for( auto const& [k,e] : tab ) {
									audit( fd , ro , widen(mk_py_str(k),w)+" : "+(e.protect?mk_py_str(e.txt):e.txt) , true/*as_is*/ , lvl+1 , sep ) ;
									sep = ',' ;
								}
								//                                                  protect
								audit_map( "views"               , views           , false ) ;
								audit_map( "required resources"  , required_rsrcs  , true  ) ;
								audit_map( "allocated resources" , allocated_rsrcs , true  ) ;
								audit( fd , ro , "}" , true , lvl ) ;
							} else {
								size_t w  = ::max<size_t>( tab                   , [&](auto const& k_e ) { return k_e.second.txt.find('\n')==Npos ? k_e .first.size() : 0 ; } ) ;
								size_t w2 = ::max<size_t>( start.job_space.views , [&](auto const& v_vd) { return +v_vd.second                    ? v_vd.first.size() : 0 ; } ) ;
								for( auto const& [k,e] : tab ) //!                                                   as_is
									if (e.txt.find('\n')==Npos)   audit( fd , ro , e.color , widen(k,w)+" : "+e.txt , true , lvl ) ;
									else                        { audit( fd , ro , e.color ,       k   +" :"        , true , lvl ) ; audit(fd,ro,e.txt,true/*as_is*/,lvl+1) ; }
								if (w2) {
									audit( fd , ro , "views :" , true/*as_is*/ , lvl ) ;
									for( auto const& [v,vd] : start.job_space.views ) if (+vd) {
										::string vd_str ;
										if (vd.phys_s.size()==1) {
											vd_str << no_slash(vd.phys_s[0]) ;
										} else {
											{	vd_str <<        "upper:" << no_slash(vd.phys_s[0]) ; }
											{	vd_str <<" , "<< "lower:"                           ;
												First first ;
												for( size_t i : iota(1,vd.phys_s.size()) ) vd_str << first("",",") << no_slash(vd.phys_s[i]) ;
											}
											if (+vd.copy_up) {
												vd_str <<" , "<< "copy_up" <<':' ;
												First first ;
												for( ::string const& cu : vd.copy_up ) vd_str << first("",",") << cu ;
											}
										}
										audit( fd , ro , widen(v,w2)+" : "+vd_str , true/*as_is*/ , lvl+1 ) ;
									}
								}
								if ( +required_rsrcs || +allocated_rsrcs ) {
									size_t w2 = 0 ;
									for( auto const& [k,_] : required_rsrcs  ) w2 = ::max(w2,k.size()) ;
									for( auto const& [k,_] : allocated_rsrcs ) w2 = ::max(w2,k.size()) ;
									::string hdr  ;
									bool     both = false ;
									if      (!allocated_rsrcs) hdr  = "required "  ;
									else if (!required_rsrcs ) hdr  = "allocated " ;
									else                       both = true         ;
									audit( fd , ro , ::move(hdr)+"resources :" , true/*as_is*/ , lvl ) ;
									::string no_msg        ;
									::string required_msg  ;
									::string allocated_msg ;
									if (both) {
										int w3 = 0 ;
										for( auto const& [k,rv] : required_rsrcs  ) if ( auto it=allocated_rsrcs.find(k) ; it==allocated_rsrcs.end() || rv!=it->second ) w3 = ::max(w3,8/*required*/ ) ;
										for( auto const& [k,av] : allocated_rsrcs ) if ( auto it=required_rsrcs .find(k) ; it==required_rsrcs .end() || av!=it->second ) w3 = ::max(w3,9/*allocated*/) ;
										if (w3) {
											no_msg        = "  "+widen(""         ,w3)+' ' ;
											required_msg  = " ("+widen("required" ,w3)+')' ;
											allocated_msg = " ("+widen("allocated",w3)+')' ;
										}
									}
									for( auto const& [k,rv] : required_rsrcs ) {
										auto it = allocated_rsrcs.find(k) ; //!                                                                         as_is
										if ( it!=allocated_rsrcs.end() && rv==it->second ) audit( fd , ro , widen(k,w2)+no_msg       +" : "+rv         , true , lvl+1 ) ;
										else                                               audit( fd , ro , widen(k,w2)+required_msg +" : "+rv         , true , lvl+1 ) ;
										if ( it!=allocated_rsrcs.end() && rv!=it->second ) audit( fd , ro , widen(k,w2)+allocated_msg+" : "+it->second , true , lvl+1 ) ;
									}
									for( auto const& [k,av] : allocated_rsrcs ) {
										auto it = required_rsrcs.find(k) ;
										if ( it==required_rsrcs.end()                    ) audit( fd , ro , widen(k,w2)+allocated_msg+" : "+av         , true , lvl+1 ) ;
									}
								}
							}
						} break ;
					DF}                                                                                                                                            // NO_COV
				}
			} break ;
			case ReqKey::Bom     : ShowBom    (fd,ro,lvl).show_job(job) ; break ;
			case ReqKey::Running : ShowRunning(fd,ro,lvl).show_job(job) ; break ;
			case ReqKey::Deps    :
				_audit_deps( fd , ro , false/*hide*/ , job , lvl ) ;
			break ;
			case ReqKey::Targets : {
				size_t            wc      = 0 ;
				size_t            wk      = 0 ;
				size_t            wt      = 0 ;
				::umap_ss         rev_map ;
				::vmap_s<RegExpr> res     ;
				::vector_s        keys    ;
				First             first   ;
				if (+rule) {
					Rule::RuleMatch m              = job->rule_match()        ;
					::vector_s      static_matches = m.matches(false/*star*/) ;
					VarIdx          i              = 0                        ;
					for( MatchKind mk : iota(All<MatchKind>) )
						for( VarIdx mi : rule->matches_iotas[false/*star*/][+mk] ) {
							if (mk!=MatchKind::SideDep)                                                                           // side deps cannot be targets
								rev_map.try_emplace( static_matches[i] , rule->matches[mi].first ) ;                              // in case of multiple matches, retain first
							i++ ;
						}
					::vector<Pattern> star_patterns = m.star_patterns() ;
					/**/              i             = 0                 ;
					for( MatchKind mk : iota(All<MatchKind>) )
						for( VarIdx mi : rule->matches_iotas[true/*star*/][+mk] ) {
							if (mk!=MatchKind::SideDep) res.emplace_back( rule->matches[mi].first , RegExpr(star_patterns[i]) ) ; // side deps cannot be targets
							i++ ;
						}
				}
				for( Target t : job->targets() ) {
					::string tn  = t->name()        ;
					auto     it  = rev_map.find(tn) ;
					::string key ;
					if (it!=rev_map.end())                                                     key = it->second ;
					else                   for ( auto const& [k,e] : res ) if (+e.match(tn)) { key = k          ; break ; }
					keys.push_back(key) ;
					if (porcelaine) wc = ::max( wc , mk_py_str(::string(t->crc)).size() ) ;
					else            wc = ::max( wc ,           ::string(t->crc) .size() ) ;
					if (porcelaine) wk = ::max( wk , mk_py_str(key             ).size() ) ;
					else            wk = ::max( wk ,           key              .size() ) ;
					if (porcelaine) wt = ::max( wt , mk_py_str(tn              ).size() ) ;
				}
				NodeIdx ti = 0 ;
				for( Target t : job->targets() ) {
					bool            exists = t->crc!=Crc::None                        ;
					Bool3           hide   = Maybe|!(exists||t.tflags[Tflag::Target]) ;
					Color           c      = _node_color( t , hide )                  ;
					::string const& k      = keys[ti++]                               ;
					//
					if ( !verbose && c==Color::HiddenNote ) continue ;
					//
					::string tn    = t->name()                   ;
					char     wr    = !exists?'U':+t->crc?'W':'-' ;
					::string flags ;                               for( Tflag tf : iota(All<Tflag>) ) flags << (t.tflags[tf]?TflagChars[+tf].second:'-') ;
					//                                                                                                                                                  as_is
					if (porcelaine) {
						::string     target_str = first("{",",")                                ;
						/**/         target_str <<" ( "<<       mk_py_str(wr              )     ;
						/**/         target_str <<" , "<<       mk_py_str(flags           )     ;
						if (verbose) target_str <<" , "<< widen(mk_py_str(::string(t->crc)),wc) ;
						/**/         target_str <<" , "<< widen(mk_py_str(k               ),wk) ;
						/**/         target_str <<" , "<< widen(mk_py_str(tn              ),wt) ;
						/**/         target_str <<" )"                                          ;
						audit( fd , ro ,     target_str , true/*as_is*/  , lvl ) ;
					} else {
						::string     target_str ;
						/**/         target_str <<             wr                   ;
						/**/         target_str <<' ' <<       flags                ;
						if (verbose) target_str <<' ' << widen(::string(t->crc),wc) ;
						/**/         target_str <<' ' << widen(k               ,wk) ;
						/**/         target_str <<' ' <<       mk_file(tn)          ;
						audit( fd , ro , c , target_str , false/*as_is*/ , lvl ) ;
					}
				}
				if (porcelaine) audit( fd , ro , first("{}","}") , true/*as_is*/ , lvl ) ;
			} break ;
			default :
				throw cat("cannot show ",ro.key," for job ",mk_file(job->name())) ;
		}
	}

	static bool/*ok*/ _show(EngineClosureReq const& ecr) {
		Trace trace("show",ecr) ;
		Fd                fd      = ecr.fd                     ;
		ReqOptions const& ro      = ecr.options                ;
		bool              verbose = ro.flags[ReqFlag::Verbose] ;
		if (ecr.is_job()) {
			_show_job( fd , ro , ecr.job() ) ;
			return true ;
		}
		bool           ok         = true                          ;
		bool           porcelaine = ro.flags[ReqFlag::Porcelaine] ;
		char           sep        = '{'                           ;                                                                                 // used with porcelaine
		::vector<Node> nodes      ;
		try {
			switch (ro.key) {
				case ReqKey::Info    :                                                                                                              // we may ask info on sources
				case ReqKey::InvDeps : nodes = ecr.deps   () ; break ;
				default              : nodes = ecr.targets() ;
			}
		} catch (::string const&) {
			if (g_writable) throw ;                                                                                                                 // dont know this case : propagate
			switch (ecr.files.size()) {
				case 0  : throw                                                              ;                                                      // unknown case : propagate
				case 1  : throw cat("repo is read-only and file is unknown : ",ecr.files[0]) ;
				default : {
					::string msg = "repo is read-only and some files are unknown among : " ;
					for( ::string const& f : ecr.files ) msg <<"\n  "<< f ;
					throw msg ;
				}
			}
		}
		switch (ro.key) {
			case ReqKey::Bom     : { ShowBom     sb {fd,ro} ; for( Node t : nodes ) sb.show_node(t) ; goto Return ; }
			case ReqKey::Running : { ShowRunning sr {fd,ro} ; for( Node t : nodes ) sr.show_node(t) ; goto Return ; }
		DN}
		for( Node target : nodes ) {
			trace("target",target) ;
			DepDepth lvl = 0 ;
			if (porcelaine) {
				lvl++ ; //!                                      as_is
				audit( fd , ro , cat(sep)                       , true       ) ;
				audit( fd , ro , mk_py_str(target->name())+" :" , true , lvl ) ;
				sep = ',' ;
			} else if (nodes.size()>1) {
				_audit_node( fd , ro , true/*always*/ , Maybe/*hide*/ , {} , target ) ;
				lvl++ ;
			}
			bool for_job = true ;
			switch (ro.key) {
				case ReqKey::InvDeps    :
				case ReqKey::InvTargets :
				case ReqKey::Running    : for_job = false ; break ;
			DN}
			Job job ;
			if (for_job) {
				job = _job_from_target(fd,ro,target,lvl) ;
				if ( !job && ro.key!=ReqKey::Info ) { ok = false ; continue ; }
			}
			switch (ro.key) {
				case ReqKey::Cmd     :
				case ReqKey::Env     :
				case ReqKey::Stderr  :
				case ReqKey::Stdout  :
				case ReqKey::Targets :
				case ReqKey::Trace   :
					_show_job( fd , ro , job , target , lvl ) ;
				break ;
				case ReqKey::Info :
					target->set_buildable() ;
					if ( target->status()==NodeStatus::Plain && !porcelaine ) {
						Job    cj             = target->conform_job_tgt()                                                                    ;
						size_t w              = ::max<size_t>( target->conform_job_tgts() , [ ](Job j) { return j->rule()->name.size() ; } ) ;
						bool   seen_candidate = ::any_of     ( target->conform_job_tgts() , [&](Job j) { return j!=cj                  ; } ) ;
						for( Job j : target->conform_job_tgts() ) if (j!=job) {
							Rule r = j->rule() ;
							if      (!seen_candidate) audit( fd , ro , Color::Note , "official job " +widen(r->name,w)+" : "+mk_file(j->name()) ) ; // no need to align
							else if (j==cj          ) audit( fd , ro , Color::Note , "official job  "+widen(r->name,w)+" : "+mk_file(j->name()) ) ; // align
							else                      audit( fd , ro , Color::Note , "job candidate "+widen(r->name,w)+" : "+mk_file(j->name()) ) ;
						}
					}
					if (!job) {
						Node                      n       = target ; while ( +n->asking && n->asking.is_a<Node>() ) n = Node(n->asking) ;
						::vmap_s<::pair_s<Color>> entries ;
						if      (+n->asking) entries.push_back({ porcelaine?"required_by":"required by" , {Job(n->asking)->name(),{}} }) ;
						else if (n!=target ) entries.push_back({ porcelaine?"required_by":"required by" , {    n         ->name(),{}} }) ;
						if (target->is_src_anti()) {
							Color c = {} ; if ( !porcelaine && verbose && FileSig(target->name())!=target->sig.sig ) c = Color::Warning ;
							//
							/**/         entries.push_back({ "special"  , {snake_str(::copy(target->buildable)),{}} }) ;
							if (verbose) entries.push_back({ "checksum" , {::string(target->crc)               ,c } }) ;
						} else {
							entries.push_back({"special",{}}) ;
						}
						size_t w = ::max<size_t>( entries , [](auto const& k_v) { return k_v.first.size() ; } ) ;
						if (porcelaine) {
							First first ;
							audit( fd , ro , "{None:{" , true/*as_is*/ , lvl ) ;
							for( auto const& k_vc : entries ) {
								::string v = !k_vc.second.first ? "None"s : mk_py_str(k_vc.second.first) ;
								audit( fd , ro , k_vc.second.second , cat(widen(cat('\'',k_vc.first,'\''),w+2)," : ",v) , true/*as_is*/ , lvl+1 , first(char(0),',') ) ;
							}
							audit( fd , ro , "}}" , true/*as_is*/ , lvl ) ;
						} else {
							for( auto const& k_vc : entries ) {
								::string v = !k_vc.second.first ? "None"s : k_vc.second.first ;
								audit( fd , ro , k_vc.second.second , cat(widen(k_vc.first,w)," : ",v) , true/*as_is*/ , lvl ) ;
							}
						}
						continue ;
					}
					_show_job( fd , ro , job , target , lvl ) ;
				break ;
				case ReqKey::Deps : {
					bool  seen_actual = false ;
					First first       ;
					if (porcelaine) audit( fd , ro , "{" , true/*as_is*/ , lvl ) ;
					if (+target->dir) {
						if (porcelaine) {
							if ( verbose || _node_color(target->dir)!=Color::HiddenNote ) { //!                              as_is
								audit( fd , ro , "( '' , "+mk_py_str(target->name())+" , 'up_hill' ) : "                    , true , lvl+1 ) ;
								audit( fd , ro , "( ( ( '----SF' , 'L-T' , '' , "+mk_py_str(target->dir->name())+" ) ,) ,)" , true , lvl+1 ) ;
								first() ;
							}
						} else {
							_audit_node( fd , ro , verbose , Maybe/*hide*/ , "UP_HILL" , target->dir , lvl ) ;
						}
					}
					for( JobTgt jt : target->conform_job_tgts() ) {
						bool     hide      = !jt.produces(target)          ; if ( hide && !verbose ) continue ;
						bool     is_actual = !hide && jt==job              ;
						::string comment   = is_actual ? "generating" : "" ;
						seen_actual |= is_actual ;
						_audit_job ( fd , ro , hide , jt , porcelaine?first(" ",","):"" , comment , porcelaine?":":"" , lvl   ) ;
						_audit_deps( fd , ro , hide , jt                                                              , lvl+1 ) ;
					}
					if (!seen_actual) {
						if (+job) { //!            hide
							_audit_job ( fd , ro , false , job , porcelaine?first(" ",","):"" , "polluting" , porcelaine?":":"" , lvl   ) ;
							_audit_deps( fd , ro , false , job                                                                  , lvl+1 ) ;
						} else if (!porcelaine) {
							audit      ( fd , ro , Color::Note , "no job found" , true/*as_is*/                                 , lvl+1 ) ;
						}
					}
					if (porcelaine) audit( fd , ro , "}" , true/*as_is*/ , lvl ) ;
				} break ;
				case ReqKey::InvDeps    :
				case ReqKey::InvTargets : {
					::vector<Job> jobs ;
					for( Job j : Persistent::job_lst() ) {
						if ( !verbose && _job_color(j)==Color::HiddenNote ) continue ;
						//
						if (ro.key==ReqKey::InvDeps) for( Dep    const& d : j->deps      ) { if (d==target) { jobs.push_back(j) ; break ; } }
						else                         for( Target const& t : j->targets() ) { if (t==target) { jobs.push_back(j) ; break ; } }
					}
					First  first ;
					size_t wr    = ::max<size_t>( jobs , [&](Job j) { Rule r = j->rule() ; return +r ? (porcelaine?mk_py_str(r->user_name()):r->user_name()    ).size() : 0 ; } ) ;
					size_t wj    = ::max<size_t>( jobs , [&](Job j) {                      return      (porcelaine?mk_py_str(j->name()     ):mk_file(j->name())).size()     ; } ) ;
					for( Job j : jobs ) {
						Rule r = j->rule() ;
						::string run = +r ? r->user_name() : ""s ; //!                                                                                                 as_is
						if (porcelaine) audit( fd , ro ,                 cat(first('{',',')," ( ",widen(mk_py_str(run),wr)," , ",widen(mk_py_str(j->name()),wj)," )") , true  , lvl ) ;
						else            audit( fd , ro , _job_color(j) , cat(                     widen(          run ,wr),' '  ,widen(mk_file  (j->name()),wj)     ) , false , lvl ) ;
					}
					if (porcelaine) audit( fd , ro , first("{}","}") , true/*as_is*/ , lvl ) ;
				} break ;
			DF}                                                                                                                                     // NO_COV
		}
		if (porcelaine) { //!                    as_is
			if (sep=='{') audit( fd , ro , "{}" , true ) ;                                                                                          // opening { has not been written, do it now
			else          audit( fd , ro , "}"  , true ) ;
		}
	Return :
		trace(STR(ok)) ;
		return ok ;
	}

	CmdFunc g_cmd_tab[N<ReqProc>] ;
	static bool _inited = (                     // PER_CMD : add an entry to point to the function actually executing your command (use show as a template)
		g_cmd_tab[+ReqProc::Collect] = _collect
	,	g_cmd_tab[+ReqProc::Debug  ] = _debug
	,	g_cmd_tab[+ReqProc::Forget ] = _forget
	,	g_cmd_tab[+ReqProc::Mark   ] = _mark
	,	g_cmd_tab[+ReqProc::Show   ] = _show
	,	true
	) ;

}
