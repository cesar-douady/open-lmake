// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh"

#include "re.hh"

#include "autodep/gather.hh"

#include "makefiles.hh"

using namespace Disk ;
using namespace Re   ;
using namespace Time ;
using namespace Py   ;

ENUM( Reason // reason to re-read makefiles
,	None     // must be first to be false
,	Set
,	Cleared
,	Modified
)

namespace Engine::Makefiles {

	static ::pair<vmap_s<FileTag>/*srcs*/,vector_s/*src_dirs_s*/> _gather_srcs( Sequence const& py_srcs , LnkSupport lnk_support , NfsGuard& nfs_guard ) {
		RealPathEnv       rpe        { .lnk_support=lnk_support , .root_dir=no_slash(*g_root_dir_s) } ;
		RealPath          real_path  { rpe                                                          } ;
		::vmap_s<FileTag> srcs       ;
		::vector_s        src_dirs_s ;
		for( Object const& py_src : py_srcs ) {
			::string src = py_src.as_a<Str>() ;
			if (!src) throw "found an empty source"s ;
			bool        is_dir_ = src.back()=='/'                     ;
			const char* src_msg = is_dir_ ? "source dir " : "source " ;
			if (!is_canon(src)) throw src_msg+src+" canonical form is "+mk_canon(src) ;
			//
			if (is_dir_) src.pop_back() ;
			RealPath::SolveReport sr     = real_path.solve(src,true/*no_follow*/) ;
			::string              reason ;
			FileInfo              fi     { nfs_guard.access(src) }                ;
			if (+sr.lnks) {
				/**/                                                                reason = " has symbolic link "+sr.lnks[0]+" in its path"  ;
			} else if (is_dir_) {
				if      ( fi.tag()!=FileTag::Dir                                  ) reason = " is not a directory"                            ;
			} else {
				if      ( sr.file_loc!=FileLoc::Repo                              ) reason = " is not in repo"                                ;
				else if ( !fi                                                     ) reason = " is not a regular file nor a symbolic link"     ;
				else if ( lnk_support==LnkSupport::None && fi.tag()==FileTag::Lnk ) reason = " is a symbolic link and they are not supported" ;
				SWEAR(sr.real==src) ; // src is canonic and there are no links, what may justify real from being different ?
			}
			if (is_dir_) src.push_back('/') ;
			if (+reason) throw src_msg + src + reason ;
			if (is_dir_) src_dirs_s.push_back   (src         ) ;
			else         srcs      .emplace_back(src,fi.tag()) ;
		}
		return {srcs,src_dirs_s} ;
	}

	static ::umap<Crc,RuleData> _gather_rules(Sequence const& py_rules) {
		::umap<Crc,RuleData> rules ;
		::uset_s             names ;
		for( Object const& py_rule : py_rules ) {
			RuleData rd  = py_rule.as_a<Dict>() ;
			Crc      crc = rd.match_crc ;
			if (names.contains(rd.name)) {
				if ( rules.contains(crc) && rules.at(crc).name==rd.name ) throw "rule " + rd.name + " appears twice"      ;
				else                                                      throw "two rules have the same name " + rd.name ;
			}
			if (rules.contains(crc)) throw "rule " + rd.name + " and rule " + rules.at(crc).name + " match identically and are redundant" ;
			names.insert(rd.name) ;
			rules[crc] = ::move(rd) ;
		}
		return rules ;
	}

	// dep file line format :
	// - first char is file existence (+) or non-existence (!)
	// - then file name
	// dep check is satisfied if each dep :
	// - has a date before dep_file's date (if first char is +)
	// - does not exist                    (if first char is !)
	static ::string _chk_deps( ::string const& action , ::string const& startup_dir_s , NfsGuard& nfs_guard ) { // startup_dir_s for diagnostic purpose only
		Trace trace("_chk_deps",action) ;
		//
		::string   deps_file   = AdminDirS+action+"_deps" ;
		Ddate      deps_date   = file_date(deps_file)     ; if (!deps_date) { trace("not_found") ; return action.back()=='s'?"they were never read":"it was never read" ; }
		::ifstream deps_stream { deps_file }              ;
		::string   reason      ;
		for( ::string line ; ::getline(deps_stream,line) ;) {
			bool exists = false/*garbage*/ ;
			switch (line[0]) {
				case '+' : exists = true  ; break ;
				case '!' : exists = false ; break ;
			DF}
			::string dep_name = line.substr(1)                 ;
			FileInfo fi       { (nfs_guard.access(dep_name)) } ;
			if      (  exists && !fi               ) reason = "removed"                                   ;
			else if (  exists && fi.date>deps_date ) reason = "modified"                                  ;     // in case of equality, be optimistic as deps may be modified during the ...
			else if ( !exists && +fi               ) reason = "created"                                   ;     // ... read process (typically .pyc files) and file resolution is such ...
			if      ( +reason                      ) return mk_rel(dep_name,startup_dir_s)+" was "+reason ;     // ...  that such deps may very well end up with same date as deps_file
		}
		trace("ok") ;
		return {} ;
	}

	static ::string _deps_file( ::string const& action , bool new_ ) {
		if (new_) return PrivateAdminDirS+action+"_new_deps" ;
		else      return AdminDirS       +action+"_deps"     ;
	}

	static void _chk_dangling( ::string const& action , bool new_ , ::string const& startup_dir_s ) { // startup_dir_s for diagnostic purpose only
		Trace trace("_chk_dangling",action) ;
		//
		::ifstream deps_stream { _deps_file(action,new_) } ;
		for( ::string line ; ::getline(deps_stream,line) ;) {
			switch (line[0]) {
				case '+' : break ;
				case '!' : continue ;
			DF}
			::string d = line.substr(1) ;
			if (is_abs(d)) continue ;                                                                 // d is outside repo and cannot be dangling, whether it is in a src_dir or not
			Node n{d} ;
			n->set_buildable() ;                                                                      // this is mandatory before is_src_anti() can be called
			if ( !n->is_src_anti() ) throw "dangling makefile : "+mk_rel(d,startup_dir_s) ;
		}
		trace("ok") ;
	}

	static void _gen_deps( ::string const& action , ::vector_s const& deps , ::string const& startup_dir_s ) {
		::string              new_deps_file = _deps_file(action,true /*new*/) ;
		::vmap_s<bool/*abs*/> glb_sds_s     ;
		//
		for( ::string const& sd_s : *g_src_dirs_s )
			if (!is_lcl_s(sd_s)) glb_sds_s.emplace_back(mk_abs(sd_s,*g_root_dir_s),is_abs_s(sd_s)) ;
		//
		{	OFStream os { new_deps_file } ;               // ensure os is closed, or at least it must be flushed before calling _chk_dangling
			for( ::string d : deps ) {
				SWEAR(+d) ;
				FileInfo fi{d} ;
				if (is_abs(d)) {
					for( auto const& [sd_s,a] : glb_sds_s ) {
						if (!(d+'/').starts_with(sd_s)) continue ;
						if (!a                        ) d = mk_rel(d,*g_root_dir_s) ;
						break ;
					}
				}
				os << (+fi?'+':'!') << d <<'\n' ;
			}
		}
		_chk_dangling(action,true/*new*/,startup_dir_s) ; // ensure deps have been pushed to disk (stream closed or flushed)
	}

	static void _stamp_deps(::string const& action) {
		swear_prod( ::rename( _deps_file(action,true/*new*/).c_str() , _deps_file(action,false/*new*/).c_str() )==0 , "stamp deps for ",action) ;
	}

	static ::pair<Ptr<Dict>,::vector_s/*deps*/> _read_makefiles( ::string const& action , ::string const& module ) {
		//
		static RegExpr pyc_re { R"(((.*/)?)(?:__pycache__/)?(\w+)(?:\.\w+-\d+)?\.pyc)" , true/*fast*/ } ; // dir_s is \1, module is \3, matches both python 2 & 3
		//
		Gather   gather ;
		::string data   = PrivateAdminDirS+action+"_data.py" ;
		gather.autodep_env.src_dirs_s = {"/"}                                                                         ;
		gather.autodep_env.root_dir   = no_slash(*g_root_dir_s)                                                       ;
		gather.cmd_line               = { PYTHON , *g_lmake_dir_s+"_lib/read_makefiles.py" , data , action , module } ;
		gather.child_stdin            = Child::NoneFd                                                                 ;
		Trace trace("_read_makefiles",action,module,Pdate(New)) ;
		//
		::string sav_ld_library_path ;
		if (PY_LD_LIBRARY_PATH[0]!=0) {
			sav_ld_library_path = get_env("LD_LIBRARY_PATH") ;
			if (+sav_ld_library_path) set_env( "LD_LIBRARY_PATH" , sav_ld_library_path+':'+PY_LD_LIBRARY_PATH ) ;
			else                      set_env( "LD_LIBRARY_PATH" ,                         PY_LD_LIBRARY_PATH ) ;
		}
		//              vvvvvvvvvvvvvvvvvvv
		Status status = gather.exec_child() ;
		//              ^^^^^^^^^^^^^^^^^^^
		if (PY_LD_LIBRARY_PATH[0]!=0) set_env( "LD_LIBRARY_PATH" , sav_ld_library_path ) ;
		//
		if (status!=Status::Ok) throw "cannot read " + action + (+gather.msg?" : ":"") + localize(gather.msg) ;
		//
		::string   content = read_content(data) ;
		::vector_s deps    ; deps.reserve(gather.accesses.size()) ;
		::uset_s   dep_set ;
		for( auto const& [d,ai] : gather.accesses ) {
			if (ai.digest.write!=No) continue ;
			::string py ;
			if ( Match m = pyc_re.match(d) ; +m ) py = ::string(m[1/*dir_s*/])+::string(m[3/*module*/])+".py" ;
			if (+py) { trace("dep",d,"->",py) ; if (dep_set.insert(py).second) deps.push_back(py) ; }
			else     { trace("dep",d        ) ; if (dep_set.insert(d ).second) deps.push_back(d ) ; }
		}
		try {
			Ptr<Dict> res = py_eval(content) ;
			trace("done",Pdate(New)) ;
			return { res , deps } ;
		} catch (::string const& e) { FAIL( "error while reading makefile digest :\n" , e ) ; }
	}

	static ::pair_s<bool/*done*/> _refresh_config( Config& config , Ptr<Dict>& py_info , ::vector_s& deps , ::string const& startup_dir_s , NfsGuard& nfs_guard ) {
		Trace trace("refresh_config") ;
		::string reason = _chk_deps( "config" , startup_dir_s , nfs_guard ) ;
		if (!reason) return {{},false/*done*/} ;
		//                  vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		tie(py_info,deps) = _read_makefiles( "config" , "Lmakefile" ) ;
		//                  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		try                      { config = (*py_info)["config"].as_a<Dict>() ;    }
		catch(::string const& e) { throw "while processing config :\n"+indent(e) ; }
		//
		return { reason , true/*done*/ } ;
	}

	// /!\ the 2 following functions are mostly identical, one for rules, the other for sources, but are difficult to share, modifications must be done in both simultaneously

	static ::pair_s<bool/*done*/> _refresh_srcs(
		::vmap_s<FileTag>& srcs
	,	::vector_s       & src_dirs_s
	,	::vector_s       & deps
	,	Reason             new_
	,	Dict const*        py_info
	,	::string const&    startup_dir_s
	,	NfsGuard&          nfs_guard
	) {
		Trace trace("_refresh_srcs") ;
		if ( !g_config->srcs_module && !py_info && !new_ ) return {{},false/*done*/} ; // config has not been read
		::string  reason      ;
		Ptr<Dict> py_new_info ;
		if (+g_config->srcs_module) {
			if (+new_  ) reason = "config.sources_module was "s+snake(new_)          ;
			else         reason = _chk_deps( "sources" , startup_dir_s , nfs_guard ) ;
			if (!reason) return {{},false/*done*/} ;
			//                      vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			tie(py_new_info,deps) = _read_makefiles( "srcs" , g_config->srcs_module ) ;
			//                      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			py_info = py_new_info ;
		}
		try                      { tie(srcs,src_dirs_s) = _gather_srcs( (*py_info)["manifest"s].as_a<Sequence>() , g_config->lnk_support , nfs_guard ) ; }
		catch(::string const& e) { throw "while processing sources :\n"+indent(e) ;                                                                      }
		return {reason,true/*done*/} ;
	}

	static ::pair_s<bool/*done*/> _refresh_rules(
		::umap<Crc,RuleData>& rules
	,	::vector_s&           deps
	,	Reason                new_
	,	Dict const*           py_info
	,	::string const&       startup_dir_s
	,	NfsGuard&             nfs_guard
) {
		Trace trace("_refresh_rules") ;
		if ( !g_config->rules_module && !py_info && !new_ ) return {{},false/*done*/} ; // config has not been read
		::string  reason      ;
		Ptr<Dict> py_new_info ;
		// rules depend on source dirs as deps are adapted if they lie outside repo
		if (+g_config->rules_module) {
			if (+new_  ) reason = "config.rules_module was "s+snake(new_)          ;
			else         reason = _chk_deps( "rules" , startup_dir_s , nfs_guard ) ;
			if (!reason) return {{},false/*done*/} ;
			//                      vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			tie(py_new_info,deps) = _read_makefiles( "rules" , g_config->rules_module ) ;
			//                      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			py_info = py_new_info ;
		}
		try                      { rules = _gather_rules((*py_info)["rules"s].as_a<Sequence>()) ; }
		catch(::string const& e) { throw "while processing rules :\n"+indent(e) ;                 }
		return {reason,true/*done*/} ;
	}

	::string/*msg*/ _refresh( bool rescue , bool refresh_ , bool dynamic , ::string const& startup_dir_s ) {
		Trace trace("_refresh",STR(rescue),STR(refresh_),STR(dynamic),startup_dir_s) ;
		if (!refresh_) {
			SWEAR(!dynamic) ;
			//          vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			Persistent::new_config( Config() , dynamic , rescue ) ;
			//          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			return {} ;
		}
		Gil        gil         ;
		NfsGuard   nfs_guard   { false/*reliable_dir*/ } ;                                         // until we have config info, protect against NFS
		::vector_s config_deps ;
		::vector_s rules_deps  ;
		::vector_s srcs_deps   ;
		Config     config      ;
		Ptr<Dict>  py_info     ;
		//
		::pair_s<bool/*done*/> config_digest = _refresh_config( config , py_info , config_deps , startup_dir_s , nfs_guard ) ;
		//
		Reason new_srcs  = Reason::None ;
		Reason new_rules = Reason::None ;
		auto diff_config      = [&]( Config const& old , Config const& new_ )->void {
			if ( !old.booted || !new_.booted         ) return ;                                    // only record diffs, i.e. when both exist
			//
			if ( old.srcs_module !=new_.srcs_module  ) new_srcs  = !old.srcs_module  ? Reason::Set : !new_.srcs_module  ? Reason::Cleared : Reason::Modified ;
			if ( old.rules_module!=new_.rules_module ) new_rules = !old.rules_module ? Reason::Set : !new_.rules_module ? Reason::Cleared : Reason::Modified ;
		} ;
		try {
			Gil::Anti no_gil { gil } ;                                                             // release gil as new_config needs Backend which is of lower priority
			//          vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			Persistent::new_config( ::move(config) , dynamic , rescue , diff_config ) ;
			//          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		} catch (::string const& e) {
			throw "cannot dynamically read config (because "+config_digest.first+") : "+e ;
		}
		nfs_guard.reliable_dirs = g_config->reliable_dirs ;                                        // now that config is loaded, we can optimize protection against NFS
		//
		// /!\ sources must be processed first as source dirs influence rules
		//
		::vmap_s<FileTag>      srcs        ;
		::vector_s             src_dirs_s  ;
		::pair_s<bool/*done*/> srcs_digest = _refresh_srcs( srcs , src_dirs_s , srcs_deps  , new_srcs , py_info , startup_dir_s , nfs_guard ) ;
		bool invalidate_src = srcs_digest.second ;
		if (invalidate_src) {
			try {
				Gil::Anti no_gil { gil } ;
				//                            vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				invalidate_src &= Persistent::new_srcs( ::move(srcs) , ::move(src_dirs_s) , dynamic ) ;
			} catch (::string const& e) { //! ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				// if srcs_digest is empty, sources were in config
				throw "cannot "s+(dynamic?"dynamically ":"")+"read sources (because "+(+srcs_digest.first?srcs_digest.first:config_digest.first)+") : "+e ;
			}
		}
		//
		::umap<Crc,RuleData> rules ;
		::pair_s<bool/*done*/> rules_digest = _refresh_rules( rules , rules_deps , new_rules , py_info , startup_dir_s , nfs_guard ) ;
		bool invalidate_rule = rules_digest.second ;
		if (invalidate_rule) {
			try {
				Gil::Anti no_gil { gil } ;                                                         // release gil as new_rules acquires it when needed
				//                             vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				invalidate_rule &= Persistent::new_rules( ::move(rules) , dynamic ) ;
			} catch (::string const& e) { //!  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				// if rules_digest is empty, rules were in config
				throw "cannot dynamically read rules (because " + (+rules_digest.first?rules_digest.first:config_digest.first) + ") : " + e ;
			}
		}
		if ( invalidate_src || invalidate_rule ) Persistent::invalidate_match() ;
		//
		if      (config_digest.second) _gen_deps    ( "config"  , config_deps  , startup_dir_s ) ;
		else if (srcs_digest  .second) _chk_dangling( "config"  , false/*new*/ , startup_dir_s ) ; // if sources have changed, some deps may have becom dangling
		if      (srcs_digest  .second) _gen_deps    ( "sources" , srcs_deps    , startup_dir_s ) ;
		if      (rules_digest .second) _gen_deps    ( "rules"   , rules_deps   , startup_dir_s ) ;
		else if (srcs_digest  .second) _chk_dangling( "rules"   , false/*new*/ , startup_dir_s ) ; // .
		//
		::string msg ;
		if (+config_digest.first) msg<<"read config because " <<config_digest.first<<'\n' ;
		if (+srcs_digest  .first) msg<<"read sources because "<<srcs_digest  .first<<'\n' ;
		if (+rules_digest .first) msg<<"read rules because "  <<rules_digest .first<<'\n' ;
		//
		if (config_digest.second) _stamp_deps("config" ) ;                                         // stamp deps once all error cases have been cleared
		if (srcs_digest  .second) _stamp_deps("sources") ;                                         // .
		if (rules_digest .second) _stamp_deps("rules"  ) ;                                         // .
		//
		return msg ;
	}

	::string/*msg*/ refresh        ( bool rescue , bool refresh_ ) { return _refresh( rescue , refresh_ , false/*dynamic*/ , *g_startup_dir_s ) ; }
	::string/*msg*/ dynamic_refresh(::string const& startup_dir_s) { return _refresh( false  , true     , true /*dynamic*/ , startup_dir_s    ) ; }

}
