// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <pwd.h>

#include "core.hh" // must be first to include Python.h first
#include "re.hh"
#include "autodep/gather.hh"
#include "makefiles.hh"

#if IS_DARWIN
	#include <crt_externs.h>
#endif

using namespace Disk ;
using namespace Re   ;
using namespace Time ;
using namespace Py   ;

namespace Engine::Makefiles {

	static constexpr const char* EnvironFile = ADMIN_DIR_S "environ" ;

	static ::map_ss _g_env ;

	// dep file line format :
	// - first dep is special, marked with *, and provide lmake_root
	// - first char is file existence (+) or non-existence (!)
	// - then file name
	// dep check is satisfied if each dep :
	// - has a date before dep_file's date (if first char is +)
	// - does not exist                    (if first char is !)
	static ::string _chk_deps( ::string const& action , ::string const& startup_dir_s , bool reliable_dirs=false ) { // startup_dir_s for diagnostic purpose only
		Trace trace("_chk_deps",action) ;
		//
		NfsGuard   nfs_guard { reliable_dirs }                                ;
		::string   deps_file = AdminDirS+action+"_deps"                       ;
		Ddate      deps_date = file_date(deps_file)                           ; if (!deps_date) { trace("not_found") ; return action.back()=='s'?"they were never read":"it was never read" ; }
		::vector_s deps      = AcFd(deps_file).read_lines(true/*no_file_ok*/) ;
		::string   reason    ;
		for( ::string const& line : deps ) {
			SWEAR(+line) ;
			::string dep_name     = line.substr(1)                 ;
			FileInfo fi           { (nfs_guard.access(dep_name)) } ;
			::string rel_dep_name = mk_rel(dep_name,startup_dir_s) ;
			switch (line[0]) {
				case '#' :                                                                      break ;              // comment
				case '*' : if (dep_name!=*g_lmake_root_s) return "lmake root changed"         ; break ;
				case '+' : if (!fi                      ) return rel_dep_name+" was removed"  ;
				/**/       if (fi.date>deps_date        ) return rel_dep_name+" was modified" ; break ;              // in case of equality, be optimistic as deps may be modified during the ...
				case '!' : if (+fi                      ) return rel_dep_name+" was created"  ; break ;              // ... read process (typically .pyc files) and file resolution is such ...
			DF}                                                                                                      // ...  that such deps may very well end up with same date as deps_file
		}
		trace("ok") ;
		return {} ;
	}

	static ::string _deps_file( ::string const& action , bool new_ ) {
		if (new_) return PrivateAdminDirS+action+"_new_deps" ;
		else      return AdminDirS       +action+"_deps"     ;
	}

	static void _chk_dangling( ::string const& action , bool new_ , ::string const& startup_dir_s ) {                 // startup_dir_s for diagnostic purpose only
		Trace trace("_chk_dangling",action) ;
		//
		::vector_s deps = AcFd(_deps_file(action,new_)).read_lines(true/*no_file_ok*/) ;
		for( ::string const& line : deps ) {
			if (line[0]!='+') continue ;                                                                              // not an existing file
			::string d = line.substr(1) ;
			if (is_abs(d)) continue ;                                                                                 // d is outside repo and cannot be dangling, whether it is in a src_dir or not
			Node n{d} ;
			n->set_buildable() ;                                                                                      // this is mandatory before is_src_anti() can be called
			if ( !n->is_src_anti() ) throw "while reading "+action+", dangling makefile : "+mk_rel(d,startup_dir_s) ;
		}
		trace("ok") ;
	}

	static void _gen_deps( ::string const& action , ::vector_s const& deps , ::string const& startup_dir_s ) {
		SWEAR(+deps) ;                                                                                         // there must at least be Lmakefile.py
		::string              new_deps_file = _deps_file(action,true /*new*/) ;
		::vmap_s<bool/*abs*/> glb_sds_s     ;
		//
		for( ::string const& sd_s : *g_src_dirs_s )
			if (!is_lcl_s(sd_s)) glb_sds_s.emplace_back(mk_abs(sd_s,*g_repo_root_s),is_abs_s(sd_s)) ;
		//
		{	::string content =
				"# * : lmake root\n"
				"# ! : file does not exist\n"
				"# + : file exists and date is compared with last read date\n"
				"*"+*g_lmake_root_s+'\n'
			;
			for( ::string d : deps ) {
				SWEAR(+d) ;
				FileInfo fi{d} ;
				if (is_abs(d)) {
					for( auto const& [sd_s,a] : glb_sds_s ) {
						if (!(d+'/').starts_with(sd_s)) continue ;
						if (!a                        ) d = mk_rel(d,*g_repo_root_s) ;
						break ;
					}
				}
				content << (+fi?'+':'!') << d <<'\n' ;
			}
			AcFd(new_deps_file,Fd::Write).write(content) ;
		}
		_chk_dangling(action,true/*new*/,startup_dir_s) ;
	}

	static void _stamp_deps(::string const& action) {
		swear_prod( ::rename( _deps_file(action,true/*new*/).c_str() , _deps_file(action,false/*new*/).c_str() )==0 , "stamp deps for" , action ) ;
	}

	static RegExpr const* pyc_re = nullptr ;

	static ::pair<Ptr<Dict>,::vector_s/*deps*/> _read_makefile( ::string const& action , ::string const& sub_repos ) {
		Trace trace("_read_makefile",action,Pdate(New)) ;
		//
		::string data   = PrivateAdminDirS+action+"_data.py" ;
		Gather   gather ;
		gather.autodep_env.src_dirs_s  = {"/"}                                                                                                       ;
		gather.autodep_env.repo_root_s = *g_repo_root_s                                                                                              ;
		gather.cmd_line                = { PYTHON , *g_lmake_root_s+"_lib/read_makefiles.py" , data , EnvironFile , '/'+action+"/top/" , sub_repos } ;
		gather.child_stdin             = Child::NoneFd                                                                                               ;
		gather.env                     = &_g_env                                                                                                     ;
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
		::string   content = AcFd(data).read() ;
		::vector_s deps    ;                     deps.reserve(gather.accesses.size()) ;
		::uset_s   dep_set ;
		for( auto const& [d,ai] : gather.accesses ) {
			if (ai.digest.write!=No) continue ;
			::string py ; if ( Match m = pyc_re->match(d) ; +m ) py = ::string(m[1/*dir_s*/])+::string(m[2/*module*/])+".py" ;
			if (+py) { trace("dep",d,"->",py) ; if (dep_set.insert(py).second) deps.push_back(py) ; }
			else     { trace("dep",d        ) ; if (dep_set.insert(d ).second) deps.push_back(d ) ; }
		}
		try {
			Ptr<Dict> res = py_eval(content) ;
			trace("done",Pdate(New)) ;
			return { res , deps } ;
		} catch (::string const& e) { FAIL( "error while reading makefile digest :\n" , e ) ; }
	}

	static ::pair_s<bool/*done*/> _refresh_config( Config& config , Ptr<Dict>& py_info , ::vector_s& deps , ::string const& startup_dir_s ) {
		Trace trace("refresh_config") ;
		::string reason    = _chk_deps( "config" , startup_dir_s , false/*reliable_dirs*/ ) ; // until we have config info, protect against NFS
		if (!reason) return {{},false/*done*/} ;
		//                  vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		tie(py_info,deps) = _read_makefile("config","..."/*sub_repos*/) ;                     // discover sub-repos while recursing into them
		//                  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		try                      { config = (*py_info)["config"].as_a<Dict>() ;    }
		catch(::string const& e) { throw "while processing config :\n"+indent(e) ; }
		config.has_split_rules = !py_info->contains("rules"   ) ;
		config.has_split_srcs  = !py_info->contains("manifest") ;
		//
		return { reason , true/*done*/ } ;
	}

	template<bool IsRules,class T> static ::pair_s<Bool3/*done*/> _refresh_rules_srcs(                          // Maybe means not split
		T&              res
	,	::vector_s&     deps
	,	Bool3           changed                                                                                 // Maybe means new, Yes means existence of module/callable changed
	,	Dict const*     py_info
	,	::string const& startup_dir_s
	) {
		bool has_split = IsRules ? g_config->has_split_rules : g_config->has_split_srcs ;
		Trace trace("_refresh_rules_srcs",STR(IsRules),changed,STR(has_split)) ;
		if ( !has_split && !py_info && changed==No ) return {{},Maybe/*done*/} ;                                // sources has not been read
		::string  reason      ;
		Ptr<Dict> py_new_info ;
		::string  kind        = IsRules ? "rules" : "sources" ;
		if (has_split) {
			switch (changed) {
				case Yes   : reason = kind+" module/callable appeared"       ; break ;
				case Maybe : reason = kind+" module/callable was never read" ; break ;
				case No    :
					reason = _chk_deps( kind , startup_dir_s , g_config->reliable_dirs ) ;
					if (!reason) return {{},No/*done*/} ;
				break ;
			}
			SWEAR(+reason) ;
			::string sub_repos_s ;
			First    first       ;
			/**/                                                sub_repos_s << "("                            ;
			for( ::string const& sr_s : g_config->sub_repos_s ) sub_repos_s <<first("",",")<< mk_py_str(sr_s) ; // use sub-repos list discovered during config
			/**/                                                sub_repos_s <<first("",",","")<<')'           ; // singletons must have a terminating ','
			//                      vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			tie(py_new_info,deps) = _read_makefile(kind,sub_repos_s) ;
			//                      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			py_info = py_new_info ;
		}
		try {
			if constexpr (IsRules) for( Object const& py_rule : (*py_info)["rules"   ].as_a<Sequence>() ) res.push_back(py_rule.as_a<Dict>()) ;
			else                   for( Object const& py_src  : (*py_info)["manifest"].as_a<Sequence>() ) res.push_back(py_src .as_a<Str >()) ;
		} catch(::string const& e) {
			throw "while processing "+kind+" :\n"+indent(e) ;
		}
		return {reason,Maybe|+reason/*done*/} ;                                                                 // cannot be split without reason
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
		::vector_s config_deps ;
		::vector_s rules_deps  ;
		::vector_s srcs_deps   ;
		Config     config      ;
		Ptr<Dict>  py_info     ;
		//
		if (!dynamic) {
			{	::string content ;
				First    first   ;
				size_t   w       = 0 ;
				#if IS_DARWIN
					 char** env = *_NSGetEnviron() ;
				#else
					 char** env = environ ;
				#endif
				for( char** e=env ; *e ; e++ ) if ( const char* eq = ::strchr(*e,'=') ) w = ::max(w,size_t(eq-*e)) ;
				content << '{' ;
				for( char** e=env ; *e ; e++ )
					if ( const char* eq = ::strchr(*e,'=') )
						content << first("",",")<<'\t'<< widen(mk_py_str(::string_view(*e,eq-*e)),w) <<" : "<< mk_py_str(::string(eq+1)) << '\n' ;
				content << "}\n" ;
				AcFd( EnvironFile , Fd::Write ).write(content) ;
			}
			/**/                          _g_env["HOME"           ] = no_slash(*g_repo_root_s)      ;
			/**/                          _g_env["PATH"           ] = STD_PATH                      ;
			/**/                          _g_env["UID"            ] = to_string(getuid())           ;
			/**/                          _g_env["USER"           ] = ::getpwuid(getuid())->pw_name ;
			if (PY_LD_LIBRARY_PATH[0]!=0) _g_env["LD_LIBRARY_PATH"] = PY_LD_LIBRARY_PATH            ;
		}
		//
		::pair_s<bool/*done*/> config_digest = _refresh_config( config , py_info , config_deps , startup_dir_s ) ;
		//
		Bool3 changed_srcs  = No    ;
		Bool3 changed_rules = No    ;
		bool  invalidate    = false ;                                            // invalidate because of config
		auto diff_config = [&]( Config const& old , Config const& new_ )->void {
			if (!old.booted) {                                                   // no old config means first time, all is new
				changed_srcs  = Maybe ;                                          // Maybe means new
				changed_rules = Maybe ;                                          // .
				invalidate    = true  ;
				return ;
			}
			if (!new_.booted) return ;                                           // no new config means we keep old config, no modification
			//
			changed_srcs  |= old.has_split_srcs !=new_.has_split_srcs  ;
			changed_rules |= old.has_split_rules!=new_.has_split_rules ;
			invalidate    |= old.sub_repos_s!=new_.sub_repos_s         ;         // this changes matching exceptions, which means it changes matching
		} ;
		try {
			NoGil no_gil { gil } ;                                               // release gil as new_config needs Backend which is of lower priority
			//          vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			Persistent::new_config( ::move(config) , dynamic , rescue , diff_config ) ;
			//          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		} catch (::string const& e) {
			throw "cannot "s+(dynamic?"dynamically ":"")+"read config (because "+config_digest.first+") : "+e ;
		}
		//
		// /!\ sources must be processed first as source dirs influence rules
		//
		::vector_s              srcs        ;
		::pair_s<Bool3/*done*/> srcs_digest = _refresh_rules_srcs<false/*IsRules*/>( srcs , srcs_deps , changed_srcs , py_info , startup_dir_s ) ;    // Maybe means not split
		bool                    new_srcs    = srcs_digest.second==Yes || (srcs_digest.second==Maybe&&config_digest.second)                       ;
		if (new_srcs) {
			try {
				NoGil no_gil { gil } ;
				//                        vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				invalidate |= Persistent::new_srcs( ::move(srcs) , dynamic ) ;
			} catch (::string const& e) { //!^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				throw "cannot "s+(dynamic?"dynamically ":"")+"read sources (because "+(srcs_digest.second==Yes?srcs_digest.first:config_digest.first)+") : "+e ;
			}
		}
		//
		::vector<RuleData>      rules        ;
		::pair_s<Bool3/*done*/> rules_digest = _refresh_rules_srcs<true/*IsRules*/>( rules , rules_deps , changed_rules , py_info , startup_dir_s ) ; // Maybe means not split
		bool                    new_rules    = rules_digest.second==Yes || (rules_digest.second==Maybe&&config_digest.second)                       ;
		if (new_rules) {
			try {
				NoGil no_gil { gil } ;                                                                                // release gil as new_rules acquires it when needed
				//                       vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				invalidate = Persistent::new_rules( ::move(rules) , dynamic ) ;
			} catch (::string const& e) { //! ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				// if rules_digest is empty, rules were in config
				if (dynamic) throw "cannot dynamically read rules (because " + (+rules_digest.first?rules_digest.first:config_digest.first) + ") : " + e ;
				else         throw "cannot read rules : "                                                                                            + e ;
			}
		}
		if (invalidate) Persistent::invalidate_match() ;
		//
		if      ( config_digest.second                  ) _gen_deps    ( "config"  , config_deps  , startup_dir_s ) ;
		else if (                              new_srcs ) _chk_dangling( "config"  , false/*new*/ , startup_dir_s ) ; // if sources have changed, some deps may have become dangling
		if      ( srcs_digest  .second==Yes             ) _gen_deps    ( "sources" , srcs_deps    , startup_dir_s ) ;
		else if ( srcs_digest  .second==No  && new_srcs ) FAIL() ;
		if      ( rules_digest .second==Yes             ) _gen_deps    ( "rules"   , rules_deps   , startup_dir_s ) ;
		else if ( rules_digest .second==No  && new_srcs ) _chk_dangling( "rules"   , false/*new*/ , startup_dir_s ) ; // .
		//
		::string msg ;
		if (+config_digest.first) msg<<"read config because " <<config_digest.first<<'\n' ;
		if (+srcs_digest  .first) msg<<"read sources because "<<srcs_digest  .first<<'\n' ;
		if (+rules_digest .first) msg<<"read rules because "  <<rules_digest .first<<'\n' ;
		//
		if (config_digest.second     ) _stamp_deps("config" ) ;                                                       // stamp deps once all error cases have been cleared
		if (srcs_digest  .second==Yes) _stamp_deps("sources") ;                                                       // .
		if (rules_digest .second==Yes) _stamp_deps("rules"  ) ;                                                       // .
		//
		return msg ;
	}

	::string/*msg*/ refresh( bool rescue , bool refresh_ ) {
		::string reg_exprs_file = PRIVATE_ADMIN_DIR_S "regexpr_cache" ;
		try         { deserialize( ::string_view(AcFd(reg_exprs_file).read()) , RegExpr::s_cache ) ; }         // load from persistent cache
		catch (...) {                                                                                }         // perf only, dont care of errors (e.g. first time)
		//
		// ensure this regexpr is always set, even when useless to avoid cache instability depending on whether makefiles have been read or not
		pyc_re = new RegExpr{R"(((?:.*/)?)(?:__pycache__/)?(\w+)(?:(?:\.\w+-\d+)?)\.pyc)"s} ;                  // dir_s is \1, module is \2, matches both python 2 & 3
		//
		::string res = _refresh( rescue , refresh_ , false/*dynamic*/ , *g_startup_dir_s ) ;
		//
		if (!RegExpr::s_cache.steady()) {
			try         { AcFd( dir_guard(reg_exprs_file) , Fd::Write ).write(serialize(RegExpr::s_cache)) ; } // update persistent cache
			catch (...) {                                                                                    } // perf only, dont care of errors (e.g. we are read-only)
		}
		return res ;
	}
	::string/*msg*/ dynamic_refresh(::string const& startup_dir_s) {
		return _refresh( false/*rescue*/  , true/*refresh*/ , true/*dynamic*/ , startup_dir_s ) ;
	}

}
