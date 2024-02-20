// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"
#include "re.hh"
#include "time.hh"

#include "autodep/gather_deps.hh"

#include "core.hh"

#include "makefiles.hh"

using namespace Disk ;
using namespace Re   ;
using namespace Time ;
using namespace Py   ;

namespace Engine::Makefiles {

	static ::pair<vmap_s<FileTag>/*srcs*/,vector_s/*src_dirs_s*/> _gather_srcs( Sequence const& py_srcs , LnkSupport lnk_support , NfsGuard& nfs_guard ) {
		RealPath          real_path  {{ .lnk_support=lnk_support , .root_dir=*g_root_dir }} ;
		::vmap_s<FileTag> srcs       ;
		::vector_s        src_dirs_s ;
		for( Str const* py_src : py_srcs ) {
			::string src = *py_src ;
			if (!src) throw "found an empty source"s ;
			//
			bool is_dir_ = src.back()=='/' ;
			if (is_dir_) src.pop_back() ;
			RealPath::SolveReport sr     = real_path.solve(src,true/*no_follow*/) ;
			::string              reason ;
			FileInfo              fi     { nfs_guard.access(src) }                ;
			if (+sr.lnks) {
				/**/                                                      reason = to_string(" has symbolic link ",sr.lnks[0]," in its path") ;
			} else if (is_dir_) {
				if      ( !is_canon(src)                                ) reason =           " is not canonical"                              ;
				else if ( src==".." || src.ends_with("/..")             ) reason =           " is a directory of the repo"                    ;
				else if ( fi.tag!=FileTag::Dir                          ) reason =           " is not a directory"                            ;
			} else {
				if      ( sr.kind!=Kind::Repo                           ) reason =           " is not in repository"                          ;
				else if ( sr.real!=src                                  ) reason = to_string(" canonical form is ",sr.real)                   ;
				else if ( lnk_support==LnkSupport::None && !fi.is_reg() ) reason =           " is not a regular file"                         ;
				else if ( lnk_support!=LnkSupport::None && !fi          ) reason =           " is not a regular file nor a symbolic link"     ;
			}
			if (+reason) throw to_string( is_dir_?"source dir ":"source " , src , reason ) ;
			if (is_dir_) src_dirs_s.push_back   (src+'/') ;
			else         srcs      .emplace_back(src    ,fi.tag) ;
		}
		return {srcs ,src_dirs_s} ;
	}

	static ::umap<Crc,RuleData> _gather_rules(Sequence const& py_rules) {
		::umap<Crc,RuleData> rules ;
		::uset_s             names ;
		for( Dict const* py_rule : py_rules ) {
			RuleData rd  = py_rule->as_a<Dict>() ;
			Crc      crc = rd.match_crc ;
			if (names.contains(rd.name)) {
				if ( rules.contains(crc) && rules.at(crc).name==rd.name ) throw to_string("rule " , rd.name , " appears twice"      ) ;
				else                                                      throw to_string("two rules have the same name " , rd.name ) ;
			}
			if (rules.contains(crc)) throw to_string( "rule " , rd.name , " and rule " , rules.at(crc).name , " match identically and are redundant" ) ;
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
	static ::string _chk_deps( ::string const& action , ::string const& startup_dir_s , NfsGuard& nfs_guard ) {        // startup_dir_s for diagnostic purpose only
		Trace trace("_chk_deps",action) ;
		//
		::string   deps_file   = to_string(AdminDir,'/',action,"_deps") ;
		Ddate      deps_date   = file_date(deps_file)                   ; if (!deps_date) { trace("not_found") ; return action.back()=='s'?"they were never read":"it was never read" ; }
		::ifstream deps_stream { deps_file }                            ;
		::string   reason      ;
		for( ::string line ; ::getline(deps_stream,line) ;) {
			bool exists = false/*garbage*/ ;
			switch (line[0]) {
				case '+' : exists = true  ; break ;
				case '!' : exists = false ; break ;
				default  : FAIL(line[0]) ;
			}
			::string dep_name = line.substr(1)                 ;
			FileInfo fi       { (nfs_guard.access(dep_name)) } ;
			if      (  exists && !fi               ) reason = "removed"                                              ;
			else if (  exists && fi.date>deps_date ) reason = "modified"                                             ; // in case of equality, be optimistic as deps may be modified during the ...
			else if ( !exists && +fi               ) reason = "created"                                              ; // ... read process (typically .pyc files) and file resolution is such ...
			if      ( +reason                      ) return to_string(mk_rel(dep_name,startup_dir_s)," was ",reason) ; // ...  that such deps may very well end up with same date as deps_file
		}
		trace("ok") ;
		return {} ;
	}

	static inline ::string _deps_file( ::string const& action , bool new_ ) {
		if (new_) return to_string(PrivateAdminDir,'/',action,"_new_deps") ;
		else      return to_string(AdminDir       ,'/',action,"_deps"    ) ;
	}

	static void _chk_dangling( ::string const& action , bool new_ , ::string const& startup_dir_s ) {  // startup_dir_s for diagnostic purpose only
		Trace trace("_chk_dangling",action) ;
		//
		::ifstream deps_stream { _deps_file(action,new_) } ;
		for( ::string line ; ::getline(deps_stream,line) ;) {
			switch (line[0]) {
				case '+' : break ;
				case '!' : continue ;
				default  : FAIL(line[0]) ;
			}
			::string d = line.substr(1) ;
			if (is_abs(d)) continue ;                                                                  // d is outside repo and cannot be dangling, whether it is in a src_dir or not
			Node n{d} ;
			n->set_buildable() ;                                                                       // this is mandatory before is_src_anti() can be called
			if ( !n->is_src_anti() ) throw to_string("dangling makefile : ",mk_rel(d,startup_dir_s)) ;
		}
		trace("ok") ;
	}

	static void _gen_deps( ::string const& action , ::vector_s const& deps , ::string const& startup_dir_s ) {
		::string              root_dir_s    = *g_root_dir+'/'                 ;
		::string              new_deps_file = _deps_file(action,true /*new*/) ;
		::vmap_s<bool/*abs*/> glb_sds_s     ;
		//
		for( ::string const& sd_s : g_src_dirs_s )
			if (!is_lcl_s(sd_s)) glb_sds_s.emplace_back(mk_abs(sd_s,*g_root_dir),is_abs_s(sd_s)) ;
		//
		{	OFStream os { ::dir_guard(new_deps_file) } ;  // ensure os is closed, or at least it must be flushed before calling _chk_dangling
			for( ::string d : deps ) {
				SWEAR(+d) ;
				FileInfo fi{d} ;
				if (is_abs(d)) {
					for( auto const& [sd_s,a] : glb_sds_s ) {
						if (!(d+'/').starts_with(sd_s)) continue ;
						if (!a                        ) d = mk_rel(d,root_dir_s) ;
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
		static RegExpr pyc_re { R"(((.*/)?)(?:__pycache__/)?(\w+)(?:\.\w+-\d+)?\.pyc)" , true/*fast*/ } ;  // dir_s is \1, module is \3, matches both python 2 & 3
		//
		GatherDeps gather_deps { New }                                                                        ; gather_deps.autodep_env.src_dirs_s = {"/"} ;
		::string   data        = to_string(PrivateAdminDir,'/',action,"_data.py")                             ; dir_guard(data) ;
		::vector_s cmd_line    = { PYTHON , *g_lmake_dir+"/_lib/read_makefiles.py" , data , action , module } ;
		Trace trace("_read_makefiles",action,module,Pdate::s_now()) ;
		//
		::string sav_ld_library_path ;
		if (PYTHON_LD_LIBRARY_PATH[0]!=0) {
			sav_ld_library_path = get_env("LD_LIBRARY_PATH") ;
			if (+sav_ld_library_path) set_env( "LD_LIBRARY_PATH" , to_string(sav_ld_library_path,':',PYTHON_LD_LIBRARY_PATH) ) ;
			else                      set_env( "LD_LIBRARY_PATH" ,                                   PYTHON_LD_LIBRARY_PATH  ) ;
		}
		//              vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		Status status = gather_deps.exec_child( cmd_line , Child::None/*stdin*/ , Fd::Stderr/*stdout*/ ) ; // redirect stdout to stderr as our stdout may be used to communicate with client
		//              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		if (PYTHON_LD_LIBRARY_PATH[0]!=0) set_env( "LD_LIBRARY_PATH" , sav_ld_library_path ) ;
		//
		if (status!=Status::Ok) throw to_string( "cannot read " , action , +gather_deps.msg?" : ":"" , localize(gather_deps.msg) ) ;
		//
		::string   content = read_content(data) ;
		::vector_s deps    ; deps.reserve(gather_deps.accesses.size()) ;
		::uset_s   dep_set ;
		for( auto const& [d,ai] : gather_deps.accesses ) {
			if (!ai.digest.idle()) continue ;
			::string py ;
			if ( Match m = pyc_re.match(d) ; +m ) py = ::string(m[1/*dir_s*/])+::string(m[3/*module*/])+".py" ;
			if (+py) { trace("dep",d,"->",py) ; if (dep_set.insert(py).second) deps.push_back(py) ; }
			else     { trace("dep",d        ) ; if (dep_set.insert(d ).second) deps.push_back(d ) ; }
		}
		try {
			Ptr<Dict> res = py_eval(content) ;
			trace("done",Pdate::s_now()) ;
			return { res , deps } ;
		} catch (::string const& e) { FAIL( "error while reading makefile digest :\n" , e ) ; }
	}

	static ::pair_s<bool/*done*/> _refresh_config( Config& config , Ptr<Dict>& py_info , ::vector_s& deps , ::string const& startup_dir_s , NfsGuard& nfs_guard ) {
		Trace trace("refresh_config") ;
		::string reason = _chk_deps( "config" , startup_dir_s , nfs_guard ) ;
		if (!reason) return {{},false/*done*/} ;
		//
		//                  vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		tie(py_info,deps) = _read_makefiles( "config" , "Lmakefile" ) ;
		//                  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		try                      { config = (*py_info)["config"].as_a<Dict>() ;               }
		catch(::string const& e) { throw to_string("while processing config :\n",indent(e)) ; }
		//
		return { "read config because "+reason , true/*done*/ } ;
	}

	ENUM( Reason // reason to re-read makefiles
	,	None     // must be first to be false
	,	Set
	,	Cleared
	,	Modified
	)

	// /!\ the 2 following functions are mostly identical, one for rules, the other for sources, but are difficult to share, modifications must be done in both simultaneously

	static ::pair_s<bool/*done*/> _refresh_srcs(
		::vmap_s<FileTag>& srcs
	,	::vector_s       & src_dirs_s
	,	::vector_s       & deps
	,	Reason             new_
	,	bool               dynamic
	,	Dict const*        py_info
	,	::string const&    startup_dir_s
	,	NfsGuard&          nfs_guard
	) {
		Trace trace("_refresh_srcs") ;
		::string  reason      ;
		Ptr<Dict> py_new_info ;
		if (!g_config.srcs_module) {
			if (!py_info) return {{},false/*done*/}                              ; // config has not been read
			if (dynamic ) throw "cannot dynamically read sources within config"s ;
			else          goto Load                                              ;
		}
		if (+new_  ) reason = to_string("config.sources_module was "s,new_)      ;
		else         reason = _chk_deps( "sources" , startup_dir_s , nfs_guard ) ;
		if (!reason) return {{},false/*done*/}                                               ;
		if (dynamic) throw to_string("cannot dynamically read sources (because ",reason,')') ;
		/**/         reason = "read sources because " + reason ;
		//                      vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		tie(py_new_info,deps) = _read_makefiles( "srcs" , g_config.srcs_module ) ;
		//                      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		py_info = py_new_info ;
	Load :
		try                      { tie(srcs,src_dirs_s) = _gather_srcs( (*py_info)["manifest"s].as_a<Sequence>() , g_config.lnk_support , nfs_guard ) ; }
		catch(::string const& e) { throw to_string("while processing sources :\n",indent(e)) ;                                                          }
		return {reason,true/*done*/} ;
	}

	static ::pair_s<bool/*done*/> _refresh_rules(
		::umap<Crc,RuleData>& rules
	,	::vector_s&           deps
	,	Reason                new_
	,	bool                  dynamic
	,	Dict const*           py_info
	,	::string const&       startup_dir_s
	,	NfsGuard&             nfs_guard
) {
		Trace trace("_refresh_rules") ;
		::string  reason      ;
		Ptr<Dict> py_new_info ;
		// rules depend on source dirs as deps are adapted if they lie outside repo
		if (!g_config.rules_module) {
			if ( !py_info && !new_ ) return {{},false/*done*/}                            ; // config has not been read
			if ( dynamic           ) throw "cannot dynamically read rules within config"s ;
			else                     goto Load                                            ;
		}
		if (+new_  ) reason = to_string("config.rules_module was "s,new_)      ;
		else         reason = _chk_deps( "rules" , startup_dir_s , nfs_guard ) ;
		if (!reason) return {{},false/*done*/}                                             ;
		if (dynamic) throw to_string("cannot dynamically read rules (because ",reason,')') ;
		/**/         reason = "read rules because " + reason ;
		//                      vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		tie(py_new_info,deps) = _read_makefiles( "rules" , g_config.rules_module ) ;
		//                      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		py_info = py_new_info ;
	Load :
		try                      { rules = _gather_rules((*py_info)["rules"s].as_a<Sequence>()) ; }
		catch(::string const& e) { throw to_string("while processing rules :\n",indent(e)) ;      }
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
		NfsGuard   nfs_guard   { false/*reliable_dir*/ } ;                                         // until we have config info, protect against NFS
		Gil        gil         ;
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
		//          vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		Persistent::new_config( ::move(config) , dynamic , rescue , diff_config ) ;
		//          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		nfs_guard.reliable_dirs = g_config.reliable_dirs ;                                         // now that config is loaded, we can optimize protection against NFS
		//
		// /!\ sources must be processed first as source dirs influence rules
		//
		::vmap_s<FileTag>      srcs        ;
		::vector_s             src_dirs_s  ;
		::pair_s<bool/*done*/> srcs_digest = _refresh_srcs( srcs , src_dirs_s , srcs_deps  , new_srcs  , dynamic , py_info , startup_dir_s , nfs_guard ) ;
		//                                                      vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		bool invalidate_src = srcs_digest.second && Persistent::new_srcs( ::move(srcs) , ::move(src_dirs_s) ) ;
		//                                                      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		//
		::umap<Crc,RuleData> rules ;
		::pair_s<bool/*done*/> rules_digest = _refresh_rules( rules , rules_deps , new_rules , dynamic , py_info , startup_dir_s , nfs_guard ) ;
		//                                                        vvvvvvvvvvvvvvvvvvvvvvvv
		bool invalidate_rule = rules_digest.second && Persistent::new_rules(::move(rules)) ;
		//                                                        ^^^^^^^^^^^^^^^^^^^^^^^^
		if ( invalidate_src || invalidate_rule ) Persistent::invalidate_match() ;
		//
		if      (config_digest.second) _gen_deps    ( "config"  , config_deps  , startup_dir_s ) ;
		else if (srcs_digest  .second) _chk_dangling( "config"  , false/*new*/ , startup_dir_s ) ; // if sources have changed, some deps may have becom dangling
		if      (srcs_digest  .second) _gen_deps    ( "sources" , srcs_deps    , startup_dir_s ) ;
		if      (rules_digest .second) _gen_deps    ( "rules"   , rules_deps   , startup_dir_s ) ;
		else if (srcs_digest  .second) _chk_dangling( "rules"   , false/*new*/ , startup_dir_s ) ; // .
		//
		::string msg ;
		if (+config_digest.first) append_to_string(msg,config_digest.first,'\n') ;
		if (+srcs_digest  .first) append_to_string(msg,srcs_digest  .first,'\n') ;
		if (+rules_digest .first) append_to_string(msg,rules_digest .first,'\n') ;
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
