// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <regex>

#include "cmd.hh"

using namespace Disk ;
using namespace Py   ;

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
			size_t         w     = 0                 ; for( Job j : jobs ) w = ::max(w,j->rule()->name.size()) ;
			if (ro.key==ReqKey::Clear) {
				for( Job  j : jobs  ) j->status = Status::New ;
				for( Node n : nodes ) n->mk_no_src() ;
				Job ::s_clear_frozens() ;
				Node::s_clear_frozens() ;
			}
			for( Job  j : jobs  ) audit( fd , ro , ro.key==ReqKey::List?Color::Warning:Color::Note , widen(j->rule()->name,w)+' '+mk_file(j->name()              ) ) ;
			for( Node n : nodes ) audit( fd , ro , ro.key==ReqKey::List?Color::Warning:Color::Note , widen(""             ,w)+' '+mk_file(n->name(),Yes/*exists*/) ) ;
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
				/**/       if ( !j || j->rule().is_shared() ) throw "job not found " +mk_file(j->name()) ;
				if (add) { if (  j.frozen  ()               ) throw "already frozen "+mk_file(j->name()) ; }
				else     { if ( !j.frozen  ()               ) throw "not frozen "    +mk_file(j->name()) ; }
				/**/       if (  j->running()               ) throw "job is running" +mk_file(j->name()) ;
				//
				w = ::max( w , j->rule()->name.size() ) ;
				jobs.push_back(j) ;
			} ;
			auto handle_node = [&](Node n)->void {
				if      ( add==n.frozen()         ) { ::string nn = n->name() ; throw (n.frozen()?"already":"not")+" frozen "s+mk_file(nn) ; }
				else if ( add && n->is_src_anti() ) { ::string nn = n->name() ; throw "cannot freeze source/anti "            +mk_file(nn) ; }
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
					if      ( add && (!j||j->rule().is_shared())                        ) handle_node(t) ;
					else if ( t->is_src_anti()                                          ) handle_node(t) ;
					else if ( force || (t->status()<=NodeStatus::Makable&&t->conform()) ) handle_job (j) ;
					else {
						Rule r  = j->rule()            ;
						Job  cj = t->conform_job_tgt() ;
						trace("fail",t->buildable,t->conform_idx(),t->status(),cj) ;
						if (+cj) throw "target was produced by "+r->name+" instead of "+cj->rule()->name+" (use -F to override) : "+mk_file(t->name(),Yes/*exists*/) ;
						else     throw "target was produced by "+r->name+                                " (use -F to override) : "+mk_file(t->name(),Yes/*exists*/) ;
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
					audit( fd , ro , add?Color::Warning:Color::Note , widen(j->rule()->name,w)+' '+mk_file(j->name()) ) ;
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
					audit( fd , ro , Color::Err , "file is "s+(add?"already":"not")+" no-trigger : "+mk_file(n->name(),Yes/*exists*/) ) ;
					return false ;
				}
			// do what is asked
			Node::s_no_triggers(add,nodes) ;
			for( Node n : nodes ) audit( fd , ro , add?Color::Warning:Color::Note , mk_file(n->name(),Yes/*exists*/) ) ;
		}
		return true ;
	}

	static void _audit_node( Fd fd , ReqOptions const& ro , bool verbose , Bool3 hide , ::string const& pfx , Node node , DepDepth lvl=0 ) {
		Color color = Color::None ;
		if      ( hide==Yes                                           ) color =                         Color::HiddenNote ;
		else if ( !node->has_actual_job() && !is_target(node->name()) ) color = hide==No ? Color::Err : Color::HiddenNote ;
		else if ( node->ok()==No                                      ) color =            Color::Err                     ;
		//
		if ( verbose || color!=Color::HiddenNote ) {
			if (ro.flags[ReqFlag::Quiet]) audit( fd , ro , color ,         mk_file(node->name()) , false/*as_is*/ , 0   ) ; // if quiet, no header, no reason to indent
			else                          audit( fd , ro , color , pfx+' '+mk_file(node->name()) , false/*as_is*/ , lvl ) ;
		}
	}

	static void _audit_job( Fd fd , ReqOptions const& ro , bool show_deps , bool hide , Job job , DepDepth lvl=0 ) {
		Color color = Color::None ;
		Rule  rule  = job->rule() ;
		if      (hide                   ) color = Color::HiddenNote ;
		else if (job->status==Status::Ok) color = Color::Ok         ;
		else if (job.frozen()           ) color = Color::Warning    ;
		else                              color = Color::Err        ;
		::string rn ; if (+rule) rn = rule->name+' ' ;
		if (!ro.flags[ReqFlag::Quiet]) audit( fd , ro , color , rn+mk_file(job->name()) , false/*as_is*/ , lvl ) ;
		if (!show_deps) return ;
		size_t                w       = 0 ;
		::umap_ss             rev_map ;
		::vmap_s<Re::RegExpr> res     ;
		if (+rule) {
			Rule::SimpleMatch m = job->simple_match() ;
			VarIdx            i = rule->n_statics     ;
			for( auto const& [k,d] : rule->deps_attrs.eval(m) ) {
				w              = ::max( w , k.size() ) ;
				rev_map[d.txt] = k                     ;
			}
			for( ::string const& p : m.star_patterns() ) {
				::pair_s<RuleData::MatchEntry> const& me = rule->matches[i++] ;
				if (me.second.flags.is_target!=No) continue ;
				w = ::max( w , me.first.size() ) ;
				res.emplace_back( me.first , Re::RegExpr(p,false/*cache*/) ) ;
			}
		}
		::vector<bool> parallel ;     for( Dep const& d : job->deps ) parallel.push_back(d.parallel) ; // first pass to count deps as they are compressed and size is not known upfront
		NodeIdx        d        = 0 ;
		for( Dep const& dep : job->deps ) {
			bool       cdp     = d  >0               && parallel[d  ]                                  ;
			bool       ndp     = d+1<parallel.size() && parallel[d+1]                                  ;
			auto       it      = dep.dflags[Dflag::Static] ? rev_map.find(dep->name()) : rev_map.end() ;
			::string   dep_key ;
			if (it!=rev_map.end())                                                              dep_key = it->second ;
			else                   for ( auto const& [k,e] : res ) if (+e.match(dep->name())) { dep_key = k          ; break ; }
			::string pfx = dep.dflags_str()+' '+dep.accesses_str()+' '+widen(dep_key,w)+' ' ;
			if      ( !cdp && !ndp ) pfx.push_back(' ' ) ;
			else if ( !cdp &&  ndp ) pfx.push_back('/' ) ;
			else if (  cdp &&  ndp ) pfx.push_back('|' ) ;
			else                     pfx.push_back('\\') ;
			_audit_node( fd , ro , ro.flags[ReqFlag::Verbose] , (Maybe&!dep.dflags[Dflag::Required])|hide , pfx , dep , lvl+1 ) ;
			d++ ;
		}
	}

	static ::pair<::vmap_ss/*set*/,::vector_s/*keep*/> _mk_env( JobInfo const& job_info ) {
		bool                                        has_end = +job_info.end                     ;
		::umap_ss                                   de      = mk_umap(job_info.end.dynamic_env) ;
		::pair<::vmap_ss/*set*/,::vector_s/*keep*/> res     ;
		for( auto const& [k,v] : job_info.start.start.env )
			if      (v!=EnvPassMrkr) res.first .emplace_back(k,v       ) ;
			else if (!has_end      ) res.second.push_back   (k         ) ;
			else if (de.contains(k)) res.first .emplace_back(k,de.at(k)) ;
		return res ;
	}

	static ::string _mk_gen_script_line( Job j , ReqOptions const& ro , JobInfo const& job_info , ::string const& dbg_dir_s , ::string const& key ) {
		JobStartRpcReply const& start = job_info.start.start ;
		AutodepEnv       const& ade   = start.autodep_env    ;
		Rule::SimpleMatch       match = j->simple_match()    ;
		//
		for( Node t  : j->targets ) t->set_buildable() ;                    // necessary for pre_actions()
		::string res ;
		res << "script = gen_script(\n" ;
		//
		/**/                               res <<  "\tauto_mkdir     = " << mk_py_str(ade.auto_mkdir                        ) << '\n' ;
		/**/                               res << ",\tautodep_method = " << mk_py_str(snake(start.method)                   ) << '\n' ;
		if (+start.job_space.chroot_dir_s) res << ",\tchroot_dir     = " << mk_py_str(no_slash(start.job_space.chroot_dir_s)) << '\n' ;
		if (+start.cwd_s                 ) res << ",\tcwd            = " << mk_py_str(no_slash(start.cwd_s)                 ) << '\n' ;
		/**/                               res << ",\tdebug_dir      = " << mk_py_str(no_slash(dbg_dir_s)                   ) << '\n' ;
		/**/                               res << ",\tignore_stat    = " << mk_py_str(ade.ignore_stat                       ) << '\n' ;
		/**/                               res << ",\tis_python      = " << mk_py_str(j->rule()->is_python                  ) << '\n' ;
		if (ro.flags[ReqFlag::KeepTmp]   ) res << ",\tkeep_tmp       = " <<           "True"                                  << '\n' ;
		/**/                               res << ",\tkey            = " << mk_py_str(key                                   ) << '\n' ;
		/**/                               res << ",\tjob            = " <<           +j                                      << '\n' ;
		/**/                               res << ",\tlink_support   = " << mk_py_str(snake(ade.lnk_support)                ) << '\n' ;
		/**/                               res << ",\tname           = " << mk_py_str(j->name()                             ) << '\n' ;
		if (+start.job_space.repo_view_s ) res << ",\trepo_view      = " << mk_py_str(no_slash(start.job_space.repo_view_s )) << '\n' ;
		/**/                               res << ",\tstdin          = " << mk_py_str(start.stdin                           ) << '\n' ;
		/**/                               res << ",\tstdout         = " << mk_py_str(start.stdout                          ) << '\n' ;
		if (ro.flags[ReqFlag::TmpDir]    ) res << ",\ttmp_dir        = " << mk_py_str(ro.flag_args[+ReqFlag::TmpDir]        ) << '\n' ;
		if (+start.job_space.tmp_view_s  ) res << ",\ttmp_view       = " << mk_py_str(no_slash(start.job_space.tmp_view_s  )) << '\n' ;
		//
		res << ",\tpreamble =\n" << mk_py_str(start.cmd.first ) << '\n' ;
		res << ",\tcmd =\n"      << mk_py_str(start.cmd.second) << '\n' ;
		//
		::pair<::vmap_ss/*set*/,::vector_s/*keep*/> env = _mk_env(job_info) ;
		if (+env.first) {
			res << ",\tenv = {" ;
			First first ;
			for( auto const& [k,v] : env.first ) res << first("\n\t\t",",\t") << mk_py_str(k) <<" : "<< mk_py_str(v) <<"\n\t" ;
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
			for( ::string const& c : start.interpreter ) res << first("",",") << mk_py_str(c) ;
			res << first("",",","") << ")\n" ;
		}
		{	res << ",\tpre_actions = {" ;
			First first ;
			for( auto const& [t,a] : j->pre_actions(match) ) res << first("\n\t\t",",\t") << mk_py_str(t->name()) <<" : "<< mk_py_str(snake(a.tag)) <<"\n\t" ;
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
			for( Dep const& d : j->deps )
				if (d.dflags[Dflag::Static]) res << first("\n\t\t",",\t") << mk_py_str(d->name()) << "\n\t" ;
			res << first("",",","") << ")\n" ;
		}
		{	res << ",\tstatic_targets = (" ;
			First first ;
			for( Target const& t : j->targets )
				if (t.tflags[Tflag::Static]) res << first("\n\t\t",",\t") << mk_py_str(t->name()) << "\n\t" ;
			res << first("",",","") << ")\n" ;
		}
		{	res << ",\tviews = {" ;
			First first1 ;
			for( auto const& [view,descr] : start.job_space.views ) {
				SWEAR(+descr.phys) ;
				res << first1("\n\t\t",",\t") << mk_py_str(view) << " : " ;
				if (+descr.phys.size()==1) {                                // bind case
					SWEAR(!descr.copy_up) ;
					res << mk_py_str(descr.phys[0]) ;
				} else {                                                    // overlay case
					res << '{' ;
					{	res <<"\n\t\t\t"<< mk_py_str("upper") <<" : "<< mk_py_str(descr.phys[0]) <<"\n\t\t" ;
					}
					{	res <<",\t"<< mk_py_str("lower") <<" : (" ;
						First first2 ;
						for( size_t i : iota(1,descr.phys.size()) ) res << first2("",",") << mk_py_str(descr.phys[i]) ;
						res << first2("",",","") << ")\n\t\t" ;
					}
					if (+descr.copy_up) {
						res <<",\t"<< mk_py_str("copy_up") <<" : (" ;
						First first2 ;
						for( ::string const& p : descr.copy_up ) res << first2("",",") << mk_py_str(p) ;
						res << first2("",",","") << ")\n\t\t" ;
					}
					res << "}" ;
				}
				res << "\n\t" ;
			}
			res << "}\n" ;
		}
		res << ")\n" ;
		return res ;
	}

	static Job _job_from_target( Fd fd , ReqOptions const& ro , Node target ) {
		JobTgt job = target->actual_job() ;
		if (!job) goto NoJob ;
		if (job->rule().is_shared()) {
			/**/                              if (target->status()>NodeStatus::Makable) goto NoJob ;
			job = target->conform_job_tgt() ; if (job->rule().is_shared()             ) goto NoJob ;
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
		//
		Job job ;
		if (ecr.as_job()) {
			job = ecr.job() ;
		} else {
			::vector<Node> targets = ecr.targets() ;
			throw_unless( targets.size()==1 , "can only debug a single target" ) ;
			job = _job_from_target(fd,ro,ecr.targets()[0]) ;
		}
		if (!job                                  ) throw "no job found"s                        ;
		if ( Rule r=job->rule() ; r->is_special() ) throw "cannot debug "+r->full_name()+" jobs" ;
		//
		JobInfo job_info = job.job_info() ;
		if (!job_info.start.start) {
			audit( fd , ro , Color::Err , "no info available" ) ;
			return false ;
		}
		//
		::string const& key = ro.flag_args[+ReqFlag::Key] ;
		auto            it  = g_config->dbg_tab.find(key) ;
		throw_unless( it!=g_config->dbg_tab.end() , "unknown debug method ",ro.flag_args[+ReqFlag::Key] ) ;
		throw_unless( +it->second                 , "empty debug method "  ,ro.flag_args[+ReqFlag::Key] ) ;
		::string runner    = split(it->second)[0]                       ;                                                          // allow doc after first word
		::string dbg_dir_s = job->ancillary_file(AncillaryTag::Dbg)+'/' ;
		mk_dir_s(dbg_dir_s) ;
		//
		::string script_file     = dbg_dir_s+"script"       ;
		::string gen_script_file = dbg_dir_s+"gen_script"   ;
		{	::string gen_script ;
			gen_script << "#!" PYTHON "\n"                                                                                       ;
			gen_script << "import sys\n"                                                                                         ;
			gen_script << "import os\n"                                                                                          ;
			gen_script << "sys.path[0:0] = ("<<mk_py_str(*g_lmake_root_s+"lib")<<','<<mk_py_str(no_slash(*g_repo_root_s))<<")\n" ; // repo_root is not in path as script is in LMAKE/debug/<job>
			gen_script << "from "<<runner<<" import gen_script\n"                                                                ;
			gen_script << _mk_gen_script_line(job,ro,job_info,dbg_dir_s,key)                                                     ;
			gen_script << "print( script , file=open("<<mk_py_str(script_file)<<",'w') )\n"                                      ;
			gen_script << "os.chmod("<<mk_py_str(script_file)<<",0o755)\n"                                                       ;
			AcFd(gen_script_file,Fd::Write).write(gen_script) ;
		}                                                                                                                          // ensure gen_script is closed before launching it
		::chmod(gen_script_file.c_str(),0755) ;
		Child child ;
		child.stdin    = {}                ;                                                                                       // no input
		child.cmd_line = {gen_script_file} ;
		child.spawn() ;
		if (!child.wait_ok()) throw "cannot generate debug script "+script_file ;
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
					throw_unless( +j , "job not found" ) ;
					ok = j->forget( ro.flags[ReqFlag::Targets] , ro.flags[ReqFlag::Deps] ) ;
				} else {
					for( Node t : ecr.targets() ) ok &= t->forget( ro.flags[ReqFlag::Targets] , ro.flags[ReqFlag::Deps] ) ;
				}
			break ;
			case ReqKey::Resources : {
				::uset<Rule> refreshed ;
				for( RuleCrc rc : Persistent::rule_crc_lst() ) if ( RuleCrcData& rcd=rc.data() ; rcd.state==RuleCrcState::RsrcsOld) {
					rcd.state = RuleCrcState::RsrcsForgotten ;
					if (refreshed.insert(rcd.rule).second) audit( ecr.out_fd , ro , Color::Note , "refresh "+rcd.rule->full_name() , true/*as_is*/ ) ;
				}
			} break ;
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
					for( Job j : backlog ) audit( fd , *ro , Color::HiddenNote , ""s+'W'+' '+j  ->rule()->name+' '+mk_file(j  ->name()) , false/*as_is*/ , l++ ) ;
					/**/                   audit( fd , *ro , color             , ""s+hdr+' '+job->rule()->name+' '+mk_file(job->name()) , false/*as_is*/ , lvl ) ;
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
		bool              verbose   = ro.flags[ReqFlag::Verbose] ;
		Rule              rule      = job->rule()                ;
		JobInfo           job_info  = job.job_info()             ;
		JobStartRpcReq  & pre_start = job_info.start.pre_start   ;
		JobStartRpcReply& start     = job_info.start.start       ;
		JobEndRpcReq    & end       = job_info.end               ;
		JobDigest       & digest    = end.digest                 ;
		switch (ro.key) {
			case ReqKey::Cmd    :
			case ReqKey::Env    :
			case ReqKey::Info   :
			case ReqKey::Stderr :
			case ReqKey::Stdout :
			case ReqKey::Trace  : {
				if ( +rule && rule->is_special() ) {
					switch (ro.key) {
						case ReqKey::Info   :
						case ReqKey::Stderr : {
							_audit_job( fd , ro , false/*show_deps*/ , false/*hide*/ , job , lvl   ) ;
							audit     ( fd , ro , job->special_stderr() , false/*as_is*/   , lvl+1 ) ;
						} break ;
						case ReqKey::Cmd    :
						case ReqKey::Env    :
						case ReqKey::Stdout :
						case ReqKey::Trace  :
							_audit_job( fd , ro , false/*show_deps*/ , false/*hide*/ , job                , lvl   ) ;
							audit     ( fd , ro , Color::Err , "no "s+ro.key+" available" , true/*as_is*/ , lvl+1 ) ;
						break ;
					DF}
				} else {
					//
					if (pre_start.job) SWEAR(pre_start.job==+job,pre_start.job,+job) ;
					//
					switch (ro.key) {
						case ReqKey::Env : {
							if (!start) { audit( fd , ro , Color::Err , "no info available" , true/*as_is*/ , lvl ) ; break ; }
							::pair<::vmap_ss/*set*/,::vector_s/*keep*/> env = _mk_env(job_info) ;
							size_t                                      w   = 0                 ;
							for( auto     const& [k,v] : env.first  ) w = ::max(w,k.size()) ;
							for( ::string const&  k    : env.second ) w = ::max(w,k.size()) ;
							for( auto     const& [k,v] : env.first  ) audit( fd , ro , widen(k,w)+" : "+v , true/*as_is*/ , lvl ) ;
							for( ::string const&  k    : env.second ) audit( fd , ro , widen(k,w)+" ..."  , true/*as_is*/ , lvl ) ;
						} break ;
						case ReqKey::Cmd :
							if (!start) { audit( fd , ro , Color::Err , "no info available" , true/*as_is*/ , lvl ) ; break ; }
							audit( fd , ro , start.cmd.first+start.cmd.second , true/*as_is*/ , lvl ) ;
						break ;
						case ReqKey::Stdout :
							if (!end) { audit( fd , ro , Color::Err , "no info available" , true/*as_is*/ , lvl ) ; break ; }
							_audit_job( fd , ro , false/*show_deps*/ , false/*hide*/ , job , lvl   ) ;
							audit     ( fd , ro , digest.stdout                            , lvl+1 ) ;
						break ;
						case ReqKey::Stderr :
							if (!( +end || (+start&&verbose) )) { audit( fd , ro , Color::Err , "no info available" , true/*as_is*/ , lvl ) ; break ; }
							_audit_job( fd , ro , false/*show_deps*/ , false/*hide*/ , job , lvl ) ;
							//                                                                       as_is
							if ( +start && verbose ) audit( fd , ro , Color::Note , pre_start.msg  , false , lvl+1 ) ;
							if ( +end   && verbose ) audit( fd , ro , Color::Note , end.msg        , false , lvl+1 ) ;
							if ( +end              ) audit( fd , ro ,               digest.stderr  , true  , lvl+1 ) ;
						break ;
						case ReqKey::Trace : {
							if (!end) { audit( fd , ro , Color::Err , "no info available" , true/*as_is*/ , lvl ) ; break ; }
							sort(end.exec_trace) ;
							::string et ;
							size_t   w  = 0 ; for( ExecTraceEntry const& e : end.exec_trace ) w = ::max(w,e.step.size()) ;
							for( ExecTraceEntry const& e : end.exec_trace ) {
								/**/         et <<      e.date.str(3/*prec*/,true/*in_day*/) ;
								/**/         et <<' '<< widen(e.step,w)                      ;
								if (+e.file) et <<' '<< e.file                               ;
								/**/         et <<'\n'                                       ;
							}
							_audit_job( fd , ro , false/*show_deps*/ , false/*hide*/ , job , lvl   ) ;
							audit     ( fd , ro , et                                       , lvl+1 ) ;
						} break ;
						case ReqKey::Info : {
							struct Entry {
								::string txt     ;
								Color    color   = Color::None ;
								bool     protect = true        ;
							} ;
							bool            porcelaine = ro.flags[ReqFlag::Porcelaine]       ;
							::string        su         = porcelaine ? ""s : ro.startup_dir_s ;
							::vmap_s<Entry> tab        ;
							auto push_entry = [&]( const char* k , ::string const& v , Color c=Color::None , bool protect=true )->void {
								tab.emplace_back( k , Entry{v,c,protect} ) ;
							} ;
							//
							::string ids ;
							if (porcelaine) {
								ids = "{ 'job':"s+(+job) ;
								if (+start) {
									if      (start.small_id             ) ids<<" , 'small':"<<start.small_id     ;
									if      (pre_start.seq_id==SeqId(-1)) ids<<" , 'downloaded_from_cache':True" ;
									else if (pre_start.seq_id!=0        ) ids<<" , 'seq':"  <<pre_start.seq_id   ;
								}
								ids += " }" ;
							} else {
								ids = "job="s+(+job) ;
								if (+start) {
									if      (start.small_id             ) ids<<" , small:"<<start.small_id   ;
									if      (pre_start.seq_id==SeqId(-1)) ids<<" , downloaded_from_cache"    ;
									else if (pre_start.seq_id!=0        ) ids<<" , seq:"  <<pre_start.seq_id ;
								}
							}
							//
							push_entry("ids",ids,Color::None,false/*protect*/) ;
							if ( Node n=job->asking ; +n ) {
								while ( +n->asking && n->asking.is_a<Node>() ) n = Node(n->asking) ;
								if (+n->asking) push_entry("required by",localize(mk_file(Job(n->asking)->name()),su)) ;
								else            push_entry("required by",localize(mk_file(    n         ->name()),su)) ;
							}
							if (+start) {
								JobInfoStart const& rs       = job_info.start          ;
								SubmitAttrs  const& sa       = rs.submit_attrs         ;
								::string            pressure = sa.pressure.short_str() ;
								//
								if ( +sa.reason                               ) push_entry( "reason" , localize(reason_str(sa.reason),su) ) ;
								if ( rs.host && rs.host!=SockFd::LoopBackAddr ) push_entry( "host"   , SockFd::s_host(rs.host)            ) ;
								//
								if (+rs.eta) {
									if (porcelaine) push_entry( "scheduling" , "( "+mk_py_str(rs.eta.str())+" , "+::to_string(double(sa.pressure))+" )"      , Color::None,false/*protect*/ ) ;
									else            push_entry( "scheduling" ,                rs.eta.str() +" - "+                   sa.pressure.short_str()                                ) ;
								}
								//
								if (+start.job_space.chroot_dir_s ) push_entry( "chroot_dir"  , no_slash(start.job_space.chroot_dir_s) ) ;
								if (+start.job_space.repo_view_s  ) push_entry( "repo_view"   , no_slash(start.job_space.repo_view_s ) ) ;
								if (+start.job_space.tmp_view_s   ) push_entry( "tmp_view"    , no_slash(start.job_space.tmp_view_s  ) ) ;
								if (+start.cwd_s                  ) push_entry( "cwd"         , no_slash(start.cwd_s                 ) ) ;
								if ( start.autodep_env.auto_mkdir ) push_entry( "auto_mkdir"  , "true"                                 ) ;
								if ( start.autodep_env.ignore_stat) push_entry( "ignore_stat" , "true"                                 ) ;
								/**/                                push_entry( "autodep"     , snake_str(start.method)                ) ;
								if (+start.timeout                ) push_entry( "timeout"     , start.timeout.short_str()              ) ;
								if ( sa.tag!=BackendTag::Local    ) push_entry( "backend"     , snake_str(sa.tag)                      ) ;
								if ( start.use_script             ) push_entry( "use_script"  , "true"                                 ) ;
							}
							//
							::map_ss allocated_rsrcs = mk_map(job_info.start.rsrcs) ;
							::map_ss required_rsrcs  ;
							try {
								Rule::SimpleMatch match ;
								required_rsrcs = mk_map(rule->submit_rsrcs_attrs.eval(job,match,&::ref(vmap_s<DepDigest>())).rsrcs) ; // dont care about deps
							} catch(::pair_ss const&) {}
							//
							if (+end) {
								// no need to localize phy_tmp_dir as this is an absolute dir
								if (+start.job_space.tmp_view_s) push_entry( "physical tmp dir" , no_slash(end.phy_tmp_dir_s) ) ;
								else                             push_entry( "tmp dir"          , no_slash(end.phy_tmp_dir_s) ) ;
								//
								push_entry( "end date" , digest.end_date.str() ) ;
								if (porcelaine) { //!                                                                     protect
									push_entry( "rc"             , wstatus_str(digest.wstatus)             , Color::None , false ) ;
									push_entry( "cpu time"       , ::to_string(double(digest.stats.cpu  )) , Color::None , false ) ;
									push_entry( "elapsed in job" , ::to_string(double(digest.stats.job  )) , Color::None , false ) ;
									push_entry( "elapsed total"  , ::to_string(double(digest.stats.total)) , Color::None , false ) ;
									push_entry( "used mem"       , ::to_string(       digest.stats.mem   ) , Color::None , false ) ;
									push_entry( "cost"           , ::to_string(double(job->cost         )) , Color::None , false ) ;
								} else {
									::string const& mem_rsrc_str = allocated_rsrcs.contains("mem") ? allocated_rsrcs.at("mem") : required_rsrcs.contains("mem") ? required_rsrcs.at("mem") : ""s ;
									size_t          mem_rsrc     = +mem_rsrc_str?from_string_with_unit(mem_rsrc_str):0                                                                           ;
									bool            overflow     = digest.stats.mem > mem_rsrc                                                                                                   ;
									::string        mem_str      = to_short_string_with_unit(digest.stats.mem)+'B'                                                                               ;
									if ( overflow && mem_rsrc ) mem_str += " > "+mem_rsrc_str+'B' ;
									::string rc_str   = wstatus_str(digest.wstatus) + (wstatus_ok(digest.wstatus)&&+digest.stderr?" (with non-empty stderr)":"") ;
									Color    rc_color = wstatus_ok(digest.wstatus) ? Color::Ok : Color::Err                                                      ;
									if ( rc_color==Color::Ok && +digest.stderr ) rc_color = job->status==Status::Ok ? Color::Warning : Color::Err ;
									push_entry( "rc"             , rc_str                         , rc_color                            ) ;
									push_entry( "cpu time"       , digest.stats.cpu  .short_str()                                       ) ;
									push_entry( "elapsed in job" , digest.stats.job  .short_str()                                       ) ;
									push_entry( "elapsed total"  , digest.stats.total.short_str()                                       ) ;
									push_entry( "used mem"       , mem_str                        , overflow?Color::Warning:Color::None ) ;
									push_entry( "cost"           , job->cost         .short_str()                                       ) ;
								}
								/**/                   push_entry( "total size"      , to_short_string_with_unit(end.total_sz     )+'B' ) ;
								if (end.compressed_sz) push_entry( "compressed size" , to_short_string_with_unit(end.compressed_sz)+'B' ) ;
							}
							//
							if (+pre_start.msg        ) push_entry( "start message" , localize(pre_start.msg,su)                  ) ;
							if (+job_info.start.stderr) push_entry( "start stderr"  , job_info.start.stderr      , Color::Warning ) ;
							if (+end.msg              ) push_entry( "message"       , localize(end      .msg,su)                  ) ;
							// generate output
							if (porcelaine) {
								auto audit_map = [&]( ::string const& k , ::map_ss const& m , bool protect , bool allocated )->void {
									if (!m) return ;
									size_t w   = 0   ; for( auto const& [k,_] : m  ) w = ::max(w,mk_py_str(k).size()) ;
									char   sep = ' ' ;
									audit( fd , ro , mk_py_str(k)+" : {" , true/*as_is*/ , lvl+1 , ',' ) ;
									for( auto const& [k,v] : m ) {
										::string v_str ;
										if      ( !protect                                    ) v_str = v                                     ;
										else if ( allocated && (k=="cpu"||k=="mem"||k=="tmp") ) v_str = ::to_string(from_string_with_unit(v)) ;
										else                                                    v_str = mk_py_str(v)                          ;
										audit( fd , ro , widen(mk_py_str(k),w)+" : "+v_str , true/*as_is*/ , lvl+2 , sep ) ;
										sep = ',' ;
									}
									audit( fd , ro , "}" , true/*as_is*/ , lvl+1 ) ;
								} ;
								size_t   w     = mk_py_str("job").size() ; for( auto const& [k,_] : tab ) w = ::max(w,mk_py_str(k).size()) ;
								::string jn    = job->name()             ;
								::map_ss views ;
								for( auto const& [v,vd] : start.job_space.views ) if (+vd) {
									::string vd_str ;
									if (vd.phys.size()==1) {
										vd_str << mk_py_str(vd.phys[0]) ;
									} else {
										vd_str <<"{ " ;
										{	vd_str << mk_py_str("upper") <<':'<< mk_py_str(vd.phys[0]) ; }
										{	vd_str <<" , "<< mk_py_str("lower") <<':' ;
											First first ;
											vd_str <<'(' ;
											for( size_t i : iota(1,vd.phys.size()) ) vd_str << first("",",") << mk_py_str(vd.phys[i]) ;
											vd_str << first("",",","") <<')' ;
										}
										if (+vd.copy_up) {
											vd_str <<" , "<< mk_py_str("copy_up") <<':' ;
											First first ;
											vd_str << '(' ;
											for( ::string const& cu : vd.copy_up ) vd_str << first("",",") << mk_py_str(cu) ;
											vd_str << first("",",","") <<')' ;
										}
										vd_str <<" }" ;
									}
									views[v] = vd_str ;
								} //!                                                                                                               as_is
								/**/                           audit( fd , ro , widen(mk_py_str("job"),w)+" : "+           mk_py_str(jn   )        , true , lvl+1 , '{' ) ;
								for( auto const& [k,e] : tab ) audit( fd , ro , widen(mk_py_str(k    ),w)+" : "+(e.protect?mk_py_str(e.txt):e.txt) , true , lvl+1 , ',' ) ;
								//                                                                                 protect allocated
								/**/                           audit_map( "views"               , views           , false , false  ) ;
								/**/                           audit_map( "required resources"  , required_rsrcs  , true  , false  ) ;
								/**/                           audit_map( "allocated resources" , allocated_rsrcs , true  , true   ) ;
								/**/                           audit( fd , ro , "}"                                                                , true , lvl         ) ;
							} else {
								size_t w  = 0 ; for( auto const& [k,e ] : tab                   ) if (e.txt.find('\n')==Npos) w  = ::max(w ,k.size()) ;
								size_t w2 = 0 ; for( auto const& [v,vd] : start.job_space.views ) if (+vd                   ) w2 = ::max(w2,v.size()) ;
								_audit_job( fd , ro , false/*show_deps*/ , false/*hide*/ , job , lvl ) ;
								for( auto const& [k,e] : tab ) //!                                                   as_is
									if (e.txt.find('\n')==Npos)   audit( fd , ro , e.color , widen(k,w)+" : "+e.txt , true , lvl+1 ) ;
									else                        { audit( fd , ro , e.color ,       k   +" :"        , true , lvl+1 ) ; audit(fd,ro,e.txt,true/*as_is*/,lvl+2) ; }
								if (w2) {
									audit( fd , ro , "views :" , true/*as_is*/ , lvl+1 ) ;
									for( auto const& [v,vd] : start.job_space.views ) if (+vd) {
										::string vd_str ;
										if (vd.phys.size()==1) {
											vd_str << mk_file(vd.phys[0]) ;
										} else {
											{	vd_str << "upper:" << mk_file(vd.phys[0]) ; }
											{	vd_str <<" , "<< "lower" <<':' ;
												First first ;
												for( size_t i : iota(1,vd.phys.size()) ) vd_str << first("",",") << mk_file(vd.phys[i]) ;
											}
											if (+vd.copy_up) {
												vd_str <<" , "<< "copy_up" <<':' ;
												First first ;
												for( ::string const& cu : vd.copy_up ) vd_str << first("",",") << cu ;
											}
										}
										audit( fd , ro , widen(mk_file(v),w2)+" : "+vd_str , false/*as_is*/ , lvl+2 ) ;
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
									audit( fd , ro , ::move(hdr)+"resources :" , true/*as_is*/ , lvl+1 ) ;
									::string no_msg        ;
									::string required_msg  ;
									::string allocated_msg ;
									if (both) {
										int w3 = 0 ;
										for( auto const& [k,rv] : required_rsrcs  ) if ( auto it=allocated_rsrcs.find(k) ; it==allocated_rsrcs.end() || rv!=it->second ) w3 = ::max(w3,8/*required*/ ) ;
										for( auto const& [k,av] : allocated_rsrcs ) if ( auto it=required_rsrcs .find(k) ; it==required_rsrcs .end() || av!=it->second ) w3 = ::max(w3,9/*allocated*/) ;
										no_msg        = "  "+widen(""         ,w3)+' ' ;
										required_msg  = " ("+widen("required" ,w3)+')' ;
										allocated_msg = " ("+widen("allocated",w3)+')' ;
									}
									for( auto const& [k,rv] : required_rsrcs ) {
										auto it = allocated_rsrcs.find(k) ; //!                                                                         as_is
										if ( it!=allocated_rsrcs.end() && rv==it->second ) audit( fd , ro , widen(k,w2)+no_msg       +" : "+rv         , true , lvl+2 ) ;
										else                                               audit( fd , ro , widen(k,w2)+required_msg +" : "+rv         , true , lvl+2 ) ;
										if ( it!=allocated_rsrcs.end() && rv!=it->second ) audit( fd , ro , widen(k,w2)+allocated_msg+" : "+it->second , true , lvl+2 ) ;
									}
									for( auto const& [k,av] : allocated_rsrcs ) {
										auto it = required_rsrcs.find(k) ;
										if ( it==required_rsrcs.end()                    ) audit( fd , ro , widen(k,w2)+allocated_msg+" : "+av         , true , lvl+2 ) ;
									}
								}
							}
						} break ;
					DF}
				}
			} break ;
			case ReqKey::Bom     : ShowBom    (fd,ro,lvl).show_job(job) ;                                  break ;
			case ReqKey::Running : ShowRunning(fd,ro,lvl).show_job(job) ;                                  break ;
			case ReqKey::Deps    : _audit_job( fd , ro , true/*show_deps*/ , false/*hide*/ , job , lvl ) ; break ;
			case ReqKey::Targets : {
				size_t                w       = 0 ;
				::umap_ss             rev_map ;
				::vmap_s<Re::RegExpr> res     ;
				if (+rule) {
					Rule::SimpleMatch m = job->simple_match() ;
					VarIdx            i = 0                   ;
					for( ::string const& t : m.static_matches() ) {
						::string const& k = rule->matches[i++].first ;
						w          = ::max(w,k.size()) ;
						rev_map[t] = k                 ;
					}
					SWEAR(i==rule->n_statics) ;
					for( ::string const& p : m.star_patterns() ) {
						::pair_s<RuleData::MatchEntry> const& me = rule->matches[i++] ;
						if (me.second.flags.is_target!=Yes) continue ;
						w = ::max( w , me.first.size() ) ;
						res.emplace_back( me.first , Re::RegExpr(p,false/*cache*/) ) ;
					}
				}
				for( Target t : job->targets ) {
					::string pfx ;
					::string tk  ;
					auto     it  = rev_map.find(t->name()) ;
					if (it!=rev_map.end())                                                            tk = it->second ;
					else                   for ( auto const& [k,e] : res ) if (+e.match(t->name())) { tk = k          ; break ; }
					//
					bool exists = t->crc!=Crc::None ;
					/**/                               pfx <<      (!exists?'U':+t->crc?'W':'-')             <<' ' ;
					for( Tflag tf : iota(All<Tflag>) ) pfx <<      (t.tflags[tf]?TflagChars[+tf].second:'-')       ;
					if (+rule)                         pfx <<' '<< widen(tk,w)                                     ;
					//
					_audit_node( fd , ro , verbose , Maybe|!(exists||t.tflags[Tflag::Target])/*hide*/ , pfx , t , lvl ) ;
				}
			} break ;
			default :
				throw "cannot show "s+ro.key+" for job "+mk_file(job->name()) ;
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
		DN}
		if (porcelaine) audit( fd , ro , "{" , true/*as_is*/ ) ;
		for( Node target : targets ) {
			trace("target",target) ;
			DepDepth lvl = 1 ;
			if      (porcelaine      ) audit      ( fd , ro , ""s+sep+' '+mk_py_str(target->name())+" :" , true/*as_is*/ ) ;
			else if (targets.size()>1) _audit_node( fd , ro , true/*always*/ , Maybe/*hide*/ , {} , target               ) ;
			else                       lvl-- ;
			sep = ',' ;
			bool for_job = true ;
			switch (ro.key) {
				case ReqKey::InvDeps    :
				case ReqKey::InvTargets :
				case ReqKey::Running    : for_job = false ; break ;
			DN}
			Job job ;
			if (for_job) {
				job = _job_from_target(fd,ro,target) ;
				if ( !job && ro.key!=ReqKey::Info ) { ok = false ; continue ; }
			}
			switch (ro.key) {
				case ReqKey::Cmd     :
				case ReqKey::Env     :
				case ReqKey::Stderr  :
				case ReqKey::Stdout  :
				case ReqKey::Targets :
				case ReqKey::Trace   :
					_show_job(fd,ro,job,lvl) ;
				break ;
				case ReqKey::Info :
					if (target->status()==NodeStatus::Plain) {
						Job    cj             = target->conform_job_tgt() ;
						size_t w              = 0                         ;
						bool   seen_candidate = false                     ;
						for( Job j : target->conform_job_tgts() ) {
							w               = ::max(w,j->rule()->name.size()) ;
							seen_candidate |= j!=cj                           ;
						}
						for( Job j : target->conform_job_tgts() ) if (j!=job) {
							Rule r = j->rule() ;
							if      (!seen_candidate) audit( fd , ro , Color::Note , "official job " +widen(r->name,w)+" : "+mk_file(j->name()) ) ; // no need to align
							else if (j==cj          ) audit( fd , ro , Color::Note , "official job  "+widen(r->name,w)+" : "+mk_file(j->name()) ) ; // align
							else                      audit( fd , ro , Color::Note , "job candidate "+widen(r->name,w)+" : "+mk_file(j->name()) ) ;
						}
					}
					if (!job) {
						Node n = target ;
						while ( +n->asking && n->asking.is_a<Node>() ) n = Node(n->asking) ;
						if (n!=target) {
							if (+n->asking) audit( fd , ro , "required by : "+mk_file(Job(n->asking)->name()) ) ;
							else            audit( fd , ro , "required by : "+mk_file(    n         ->name()) ) ;
						}
						continue ;
					}
					_show_job(fd,ro,job,lvl) ;
				break ;
				case ReqKey::Deps : {
					bool verbose     = ro.flags[ReqFlag::Verbose] ;
					bool quiet       = ro.flags[ReqFlag::Quiet  ] ;
					bool seen_actual = false                      ;
					if ( target->is_plain() && +target->dir() ) _audit_node( fd , ro , verbose , Maybe/*hide*/ , "U" , target->dir() , lvl ) ;
					for( JobTgt jt : target->conform_job_tgts() ) {
						bool hide      = !jt.produces(target) ;
						bool is_actual = !hide && jt==job     ;
						seen_actual |= is_actual ;
						if ( !quiet && is_actual ) audit     ( fd , ro , Color::Note , "generated by : " , lvl ) ;
						if ( verbose || !hide    ) _audit_job( fd , ro , true/*show_deps*/ , hide , jt   , lvl ) ;
					}
					if (!seen_actual) {
						if (+job) {
							if (!quiet) audit     ( fd , ro , Color::Note , "polluted by : "          , lvl ) ;
							/**/        _audit_job( fd , ro , true/*show_deps*/ , false/*hide*/ , job , lvl ) ;
						} else {
							/**/        audit     ( fd , ro , Color::Note , "no job found"            , lvl ) ;
						}
					}
				} break ;
				case ReqKey::InvDeps :
					for( Job j : Persistent::job_lst() )
						for( Dep const& d : j->deps ) if (d==target) {
							_audit_job( fd , ro , false/*show_deps*/ , false/*hide*/ , j , lvl ) ;
							break ;
						}
				break ;
				case ReqKey::InvTargets :
					for( Job j : Persistent::job_lst() )
						for( Target const& t : j->targets ) if (t==target) {
							_audit_job( fd , ro , false/*show_deps*/ , false/*hide*/ , j , lvl ) ;
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
