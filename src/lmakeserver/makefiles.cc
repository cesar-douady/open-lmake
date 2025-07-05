// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh" // /!\ must be first to include Python.h first

#include <pwd.h>

#include "re.hh"
#include "autodep/gather.hh"
#include "makefiles.hh"

using namespace Disk ;
using namespace Re   ;
using namespace Time ;
using namespace Py   ;

namespace Engine::Makefiles {

	struct Deps {
		::vector_s                         files    ;
		::vmap_s<::pair_s<bool/*exists*/>> user_env ;
	} ;

	static constexpr const char* PrivateEnvironFile = PRIVATE_ADMIN_DIR_S "environ"  ; // provided to Lmakefile.py so it can initialize lmake.user_environ
	static constexpr const char* EnvironFile        = ADMIN_DIR_S         "environ"  ; // provided to user, contains only variables used in Lmakefile.py
	static constexpr const char* ManifestFile       = ADMIN_DIR_S         "manifest" ; // provided to user, contains the list of source files

	static ::string _deps_file(::string const& action) { return AdminDirS+action+"_deps" ; }

	static ::map_ss _g_env       ;
	static ::string _g_tmp_dir_s = cat(AdminDirS,"lmakefile_tmp/") ;

	// dep file line format :
	// - first dep is special, marked with *, and provide lmake_root
	// - first char is file existence (+) or non-existence (!)
	// - then file name
	// dep check is satisfied if each dep :
	// - has a date before dep_file's date (if first char is +)
	// - does not exist                    (if first char is !)
	static ::string _chk_deps( ::string const& action , ::umap_ss const& user_env , ::string const& startup_dir_s , FileSync file_sync=FileSync::Dflt ) { // startup_dir_s for diagnostic purpose only
		Trace trace("_chk_deps",action) ;
		//
		NfsGuard   nfs_guard { file_sync }                                    ;
		::string   deps_file = _deps_file(action)                             ;
		Ddate      deps_date = file_date(deps_file)                           ; if (!deps_date) { trace("not_found") ; return action.back()=='s'?"they were never read":"it was never read" ; }
		::vector_s deps      = AcFd(deps_file).read_lines(true/*no_file_ok*/) ;
		::string   reason    ;
		for( ::string const& line : deps ) {
			SWEAR(+line) ;
			::string d = line.substr(1) ;
			switch (line[0]) {
				case '#' :                                                       break ;         // comment
				case '*' : if (d!=*g_lmake_root_s) return "lmake root changed" ; break ;
				case '+' : {
					FileInfo fi { nfs_guard.access(d) } ;
					if (!fi.exists()     ) return cat(mk_rel(d,startup_dir_s)," was removed" ) ;
					if (fi.date>deps_date) return cat(mk_rel(d,startup_dir_s)," was modified") ; // in case of equality, be optimistic as deps may be modified during the read process ...
				} break ;                                                                        // ... (typically .pyc files) and file resolution is such that such deps may very well ...
				case '!' : {                                                                     // ... end up with same date as deps_file
					FileInfo fi { nfs_guard.access(d) } ;
					if (fi.exists()) return cat(mk_rel(d,startup_dir_s)," was created") ;
				} break ;
				case '=' : {
					size_t   pos = line.find('=',1)     ;
					::string key = line.substr(1,pos-1) ;
					auto     it  = user_env.find(key)   ;
					if (pos==Npos) {
						if (it!=user_env.end()            ) return cat("environment variable ",key," appeared"   ) ;
					} else {
						if (it==user_env.end()            ) return cat("environment variable ",key," disappeared") ;
						if (it->second!=line.substr(pos+1)) return cat("environment variable ",key," changed"    ) ;
					}
				} break ;
			DF}                                                                                  // NO_COV
		}
		trace("ok") ;
		return {} ;
	}
	static void _recall_env( ::umap_ss&/*out*/ user_env , ::string const& action ) {
		Trace trace("_recall_env",action) ;
		//
		::vector_s deps = AcFd(_deps_file(action)).read_lines(true/*no_file_ok*/) ;
		for( ::string const& line : deps ) {
			SWEAR(+line) ;
			/**/                            if (line[0]!='=') continue ;                         // not an env var definition
			size_t pos = line.find('=',1) ; if (pos==Npos   ) continue ;                         // if no variable, nothing to recall
			user_env[line.substr(1,pos-1)] = line.substr(pos+1) ;                                // lien contains =<key>=<value>
		}
		trace("ok",user_env.size()) ;
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
			Node n { New , d } ;
			n->set_buildable() ;                                                                                      // this is mandatory before is_src_anti() can be called
			if ( !n->is_src_anti() ) throw "while reading "+action+", dangling makefile : "+mk_rel(d,startup_dir_s) ;
		}
		trace("ok") ;
	}

	static void _gen_deps( ::string const& action , Deps const& deps , ::string const& startup_dir_s ) {
		SWEAR(+deps.files) ;                                                                             // there must at least be Lmakefile.py
		::string              new_deps_file = _deps_file(action,true /*new*/) ;
		::vmap_s<bool/*abs*/> glb_sds_s     ;
		//
		for( ::string const& sd_s : *g_src_dirs_s )
			if (!is_lcl_s(sd_s)) glb_sds_s.emplace_back(mk_abs(sd_s,*g_repo_root_s),is_abs_s(sd_s)) ;
		//
		::string deps_str =
			"# * : lmake root\n"
			"# ! : file does not exist\n"
			"# + : file exists and date is compared with last read date\n"
			"# = : env variable (no value if not found in environ)\n"
			"*"+*g_lmake_root_s+'\n'
		;
		for( ::string const& d : deps.files ) {
			SWEAR(+d) ;
			if (d==PrivateEnvironFile) continue ;                                                        // PrivateEnvironFile is generated before reading makefile
			char pfx = FileInfo(d).exists() ? '+' : '!' ;
			if (is_abs(d)) {
				for( auto const& [sd_s,a] : glb_sds_s ) {
					if (!(d+'/').starts_with(sd_s))                                                        continue     ;
					if (!a                        ) { deps_str << pfx << mk_rel(d,*g_repo_root_s) <<'\n' ; goto NextDep ; }
					break ;
				}
			}
			deps_str << pfx << d <<'\n' ;
		NextDep : ;
		}
		for( auto const& [key,val_exists] : deps.user_env ) {
			SWEAR(+key) ;
			if (val_exists.second) deps_str <<'='<<key<<'='<<val_exists.first<<'\n' ;
			else                   deps_str <<'='<<key                       <<'\n' ;
		}
		AcFd(new_deps_file,FdAction::Create).write(deps_str) ;
		//
		_chk_dangling(action,true/*new*/,startup_dir_s) ;
	}

	static void _stamp_deps(::string const& action) {
		swear_prod( ::rename( _deps_file(action,true/*new*/).c_str() , _deps_file(action,false/*new*/).c_str() )==0 , "stamp deps for" , action ) ;
	}

	static RegExpr const* pyc_re = nullptr ;

	// msg may be updated even if throwing
	static void _read_makefile( ::string&/*out*/ msg , Ptr<Dict>&/*out*/ py_info , Deps&/*out*/ deps , ::string const& action , ::string const& sub_repos ) {
		Trace trace("_read_makefile",action,Pdate(New)) ;
		//
		::string data_file = PrivateAdminDirS+action+"_data.py" ;
		Gather   gather    ;
		::string tmp_dir_s = _g_tmp_dir_s+action+'/'            ;
		//
		_g_env["TMPDIR"] = no_slash(cat(*g_repo_root_s,tmp_dir_s)) ;
		mk_dir_empty_s(tmp_dir_s) ;                                  // leave tmp dir after execution for debug purpose as we have no keep-tmp flags
		//
		gather.autodep_env.src_dirs_s  = {"/"}                                                                                                                   ;
		gather.autodep_env.repo_root_s = *g_repo_root_s                                                                                                          ;
		gather.cmd_line                = { PYTHON , *g_lmake_root_s+"_lib/read_makefiles.py" , data_file , PrivateEnvironFile , '.'+action+".top." , sub_repos } ;
		gather.env                     = &_g_env                                                                                                                 ;
		gather.child_stdin             = Child::NoneFd                                                                                                           ;
		gather.child_stdout            = Child::PipeFd                                                                                                           ;
		gather.child_stderr            = Child::JoinFd                                                                                                           ;
		//
		{	SavPyLdLibraryPath spllp ;
			//              vvvvvvvvvvvvvvvvvvv
			Status status = gather.exec_child() ;
			//              ^^^^^^^^^^^^^^^^^^^
			msg += gather.stdout ;
			if (status!=Status::Ok) {
				if (+gather.msg) throw cat("cannot read ",action," : ",localize(gather.msg)) ;
				else             throw cat("cannot read ",action                           ) ;
			}
		}
		//
		deps.files.reserve(gather.accesses.size()) ;
		::string   deps_str = AcFd(data_file).read() ;
		::uset_s   dep_set  ;
		try                       { py_info = py_eval(deps_str) ; }
		catch (::string const& e) { FAIL(e) ;                     }  // NO_COV
		for( auto const& [d,ai] : gather.accesses ) {
			if (ai.first_write()<Pdate::Future) continue ;
			::string py ; if ( Match m = pyc_re->match(d) ; +m ) py = cat( m.group(d,1/*dir_s*/) , m.group(d,2/*module*/) , ".py" ) ;
			if (+py) { trace("dep",d,"->",py) ; if (dep_set.insert(py).second) deps.files.push_back(py) ; }
			else     { trace("dep",d        ) ; if (dep_set.insert(d ).second) deps.files.push_back(d ) ; }
		}
		if (py_info->contains("user_environ")) {
			for( auto const& [py_key,py_val] : py_info->get_item("user_environ").as_a<Dict>() ) //!                     exists
				if (&py_val==&None) deps.user_env.emplace_back( py_key.as_a<Str>() , ::pair(""s                         ,false)) ;
				else                deps.user_env.emplace_back( py_key.as_a<Str>() , ::pair(::string(py_val.as_a<Str>()),true )) ;
			py_info->del_item("user_environ") ;
		}
		trace("done",Pdate(New)) ;
	}

	// msg may be updated even if throwing
	static bool/*done*/ _refresh_config( ::string&/*out*/ msg , Config&/*out*/ config , Ptr<Dict>&/*out*/ py_info , Deps&/*out*/ deps , ::umap_ss const& user_env , ::string const& startup_dir_s ) {
		Trace trace("refresh_config") ;
		::string reason = _chk_deps( "config" , user_env , startup_dir_s ) ;
		if (!reason) return false/*done*/ ;
		msg << "read config because "<<reason<<'\n' ;
		Gil gil ;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		_read_makefile( /*out*/msg , /*out*/py_info , /*out*/deps , "config","..."/*sub_repos*/ ) ; // discover sub-repos while recursing into them
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		try                      { config = (*py_info)["config"].as_a<Dict>() ;    }
		catch(::string const& e) { throw "while processing config :\n"+indent(e) ; }
		config.rules_action = py_info->get_item("rules_action"  ).as_a<Str>() ;
		config.srcs_action  = py_info->get_item("sources_action").as_a<Str>() ;
		//
		return true/*done*/ ;
	}

	template<bool IsRules,class T> static Bool3/*done*/ _refresh_rules_srcs(                                    // Maybe means not split
		::string&/*out*/ msg                                                                                    // msg may be updated even if throwing
	,	T       &/*out*/ res
	,	Deps    &/*out*/ deps
	,	Bool3            changed                                                                                // Maybe means new, Yes means existence of module/callable changed
	,	Dict      const* py_info
	,	::umap_ss const& user_env
	,	::string  const& startup_dir_s
	) {
		::string const& action = IsRules ? g_config->rules_action : g_config->srcs_action ;
		Trace trace("_refresh_rules_srcs",STR(IsRules),changed,action) ;
		if ( !action && !py_info && changed==No ) return Maybe/*done*/ ;                                        // sources has not been read
		::string  reason      ;
		Gil       gil         ;                                                                                 // ensure Gil is taken when py_new_info is destroyed
		Ptr<Dict> py_new_info ;
		::string  kind        = IsRules ? "rules" : "sources" ;
		if (+action) {
			switch (changed) {
				case Yes   :
					if      (action.find("import"  )!=Npos) reason = "module "   ;
					else if (action.find("callable")!=Npos) reason = "function " ;
					reason += "Lmakefile."+kind+" appeared" ;
				break ;
				case Maybe :
					reason = "Lmakefile."+kind ;
					if      (action.find("import"  )!=Npos) reason = "module "   +reason+" was never imported" ;
					else if (action.find("callable")!=Npos) reason = "function " +reason+"() was never called" ;
					else if (action.find("dflt"    )!=Npos) reason = "default sources were never read"         ;
					else                                    reason =              reason+" was never read"     ;
				break ;
				case No    :
					reason = _chk_deps( kind , user_env , startup_dir_s , g_config->file_sync ) ;
					if (!reason) return No/*done*/ ;
				break ;
			}
			SWEAR(+reason) ;
			::string sub_repos_s ;
			First    first       ;
			/**/                                                sub_repos_s << "("                            ;
			for( ::string const& sr_s : g_config->sub_repos_s ) sub_repos_s <<first("",",")<< mk_py_str(sr_s) ; // use sub-repos list discovered during config
			/**/                                                sub_repos_s <<first("",",","")<<')'           ; // singletons must have a terminating ','
			msg<<"read "<<kind<<" because "<<reason<<'\n' ;
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			_read_makefile( /*out*/msg , /*out*/py_new_info , /*out*/deps , kind+'.'+action , sub_repos_s ) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			py_info = py_new_info ;
		}
		try                      { res = (*py_info)[kind].as_a<typename T::PyType>() ; }
		catch(::string const& e) { throw "while processing "+kind+" :\n"+indent(e) ;   }
		return Maybe|+reason/*done*/ ;                                                                          // cannot be split without reason
	}

	void _refresh( ::string&/*out*/ msg , bool rescue , bool refresh_ , bool dyn , ::umap_ss const& user_env , ::string const& startup_dir_s ) { // msg may be updated even if throwing
		Trace trace("_refresh",STR(rescue),STR(refresh_),STR(dyn),startup_dir_s) ;
		if (!refresh_) {
			SWEAR(!dyn) ;
			//          vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			Persistent::new_config( Config() , dyn , rescue ) ;
			//          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			return ;
		}
		Deps               config_deps ;
		Deps               rules_deps  ;
		Deps               srcs_deps   ;
		Config             config      ;
		WithGil<Ptr<Dict>> py_info     ;
		//
		if (!dyn) {
			{	::string user_env_str ;
				First    first        ;
				size_t   w            = 0 ;
				for( auto const& [k,v] : user_env ) w = ::max(w,mk_py_str(k).size()) ;
				user_env_str << '{' ;
				for( auto const& [k,v] : user_env )
					user_env_str << first("",",")<<'\t'<< widen(mk_py_str(k),w) <<" : "<< mk_py_str(v) << '\n' ;
				user_env_str << "}\n" ;
				AcFd( PrivateEnvironFile , FdAction::Create ).write(user_env_str) ;
			}
			/**/                          _g_env["HOME"           ] = no_slash(*g_repo_root_s)      ;
			if (PY_LD_LIBRARY_PATH[0]!=0) _g_env["LD_LIBRARY_PATH"] = PY_LD_LIBRARY_PATH            ;
			/**/                          _g_env["PATH"           ] = STD_PATH                      ;
			/**/                          _g_env["UID"            ] = to_string(getuid())           ;
			/**/                          _g_env["USER"           ] = ::getpwuid(getuid())->pw_name ;
			//
			if (!FileInfo(EnvironFile ).exists()) AcFd(EnvironFile ,FdAction::Create) ; // these are sources, they must exist
			if (!FileInfo(ManifestFile).exists()) AcFd(ManifestFile,FdAction::Create) ; // .
		}
		//
		bool/*done*/ config_digest = _refresh_config( /*out*/msg , /*out*/config , /*out*/py_info , /*out*/config_deps , user_env , startup_dir_s ) ;
		//
		Bool3 changed_srcs  = No    ;
		Bool3 changed_rules = No    ;
		bool  invalidate    = false ;                                                  // invalidate because of config
		auto diff_config = [&]( Config const& old , Config const& new_ )->void {
			if (!old) {                                                                // no old config means first time, all is new
				changed_srcs  = Maybe ;                                                // Maybe means new
				changed_rules = Maybe ;                                                // .
				invalidate    = true  ;
				return ;
			}
			if (!new_) return ;                                                        // no new config means we keep old config, no modification
			//
			changed_srcs  |= old.srcs_action !=new_.srcs_action  ;
			changed_rules |= old.rules_action!=new_.rules_action ;
			invalidate    |= old.sub_repos_s !=new_.sub_repos_s  ;                     // this changes matching exceptions, which means it changes matching
		} ;
		try {
			//          vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			Persistent::new_config( ::move(config) , dyn , rescue , diff_config ) ;
			//          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		} catch (::string const& e) {
			throw "cannot "s+(dyn?"dynamically ":"")+"update config : "+e ;
		}
		//
		// /!\ sources must be processed first as source dirs influence rules
		//
		Sources       srcs        ; //!                IsRule   out   out    out
		Bool3/*done*/ srcs_digest = _refresh_rules_srcs<false>( msg , srcs , srcs_deps , changed_srcs , py_info , user_env , startup_dir_s ) ;    // Maybe means not split
		bool          new_srcs    = srcs_digest==Yes || (srcs_digest==Maybe&&config_digest)                                                  ;
		if (new_srcs) //!                                         vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			try                       { invalidate |= Persistent::new_srcs( ::move(srcs) , dyn , ManifestFile ) ; }
			//                                                    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			catch (::string const& e) { throw "cannot "s+(dyn?"dynamically ":"")+"update sources : "+e ;          }
		//
		Rules         rules        ; //!                IsRule  out   out     out
		Bool3/*done*/ rules_digest = _refresh_rules_srcs<true>( msg , rules , rules_deps , changed_rules , py_info , user_env , startup_dir_s ) ; // Maybe means not split
		bool          new_rules    = rules_digest==Yes || (rules_digest==Maybe&&config_digest)                                                  ;
		if (new_rules) //!                                        vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			try                       { invalidate |= Persistent::new_rules( ::move(rules) , dyn ) ;   }
			//                                                    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			catch (::string const& e) { throw "cannot "s+(dyn?"dynamically ":"")+"update rules : "+e ; }
		//
		if (invalidate) Persistent::invalidate_match() ;
		//
		if      ( config_digest                 ) _gen_deps    ( "config"  , config_deps  , startup_dir_s ) ;
		else if (                      new_srcs ) _chk_dangling( "config"  , false/*new*/ , startup_dir_s ) ; // if sources have changed, some deps may have become dangling
		if      ( srcs_digest ==Yes             ) _gen_deps    ( "sources" , srcs_deps    , startup_dir_s ) ;
		else if ( srcs_digest ==No  && new_srcs ) FAIL() ;
		if      ( rules_digest==Yes             ) _gen_deps    ( "rules"   , rules_deps   , startup_dir_s ) ;
		else if ( rules_digest==No  && new_srcs ) _chk_dangling( "rules"   , false/*new*/ , startup_dir_s ) ; // .
		//
		// once all error cases have been cleared, stamp deps and generate environ file for user
		if ( config_digest || srcs_digest==Yes || rules_digest==Yes ) {
			::umap_ss ue ;
			if (config_digest    ) { _stamp_deps("config" ) ; for( auto const& [k,v] : config_deps.user_env ) if (v.second) ue[k] = v.first ; } else _recall_env(ue,"config") ;
			if (srcs_digest ==Yes) { _stamp_deps("sources") ; for( auto const& [k,v] : srcs_deps  .user_env ) if (v.second) ue[k] = v.first ; } else _recall_env(ue,"srcs"  ) ;
			if (rules_digest==Yes) { _stamp_deps("rules"  ) ; for( auto const& [k,v] : rules_deps .user_env ) if (v.second) ue[k] = v.first ; } else _recall_env(ue,"rules" ) ;
			::string user_env_str ;
			for( auto const& [k,v] : ue ) user_env_str << k<<'='<<mk_printable(v)<<'\n' ;
			AcFd( EnvironFile , FdAction::Write ).write(user_env_str) ;
		}
	}

	void refresh( ::string&/*out*/ msg , bool rescue , bool refresh_ ) {                                             // msg may be updated even if throwing
		::string reg_exprs_file = PRIVATE_ADMIN_DIR_S "regexpr_cache" ;
		try         { deserialize( ::string_view(AcFd(reg_exprs_file).read()) , RegExpr::s_cache ) ; }               // load from persistent cache
		catch (...) {                                                                                }               // perf only, dont care of errors (e.g. first time)
		//
		// ensure this regexpr is always set, even when useless to avoid cache instability depending on whether makefiles have been read or not
		pyc_re = new RegExpr{ R"(((?:.*/)?)(?:(?:__pycache__/)?)(\w+)(?:(?:\.\w+-\d+)?)\.pyc)" , true/*cache*/ } ;   // dir_s is \1, module is \2, matches both python 2 & 3
		//
		_refresh( msg , rescue , refresh_ , false/*dyn*/ , mk_environ() , *g_startup_dir_s ) ;
		//
		if (!RegExpr::s_cache.steady()) {
			try         { AcFd( dir_guard(reg_exprs_file) , FdAction::Write ).write(serialize(RegExpr::s_cache)) ; } // update persistent cache
			catch (...) {                                                                                          } // perf only, dont care of errors (e.g. we are read-only)
		}
	}
	void dyn_refresh( ::string&/*out*/ msg , ::umap_ss const& env , ::string const& startup_dir_s ) {                // msg may be updated even if throwing
		_refresh( msg , false/*rescue*/  , true/*refresh*/ , true/*dyn*/ , env , startup_dir_s ) ;
	}

}
