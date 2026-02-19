// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
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

enum class Action : uint8_t {
	Config
,	Rules
,	Sources
// aliases
,	Plural = Rules // >=Plural means messages must be made plural
} ;

namespace Engine::Makefiles {

	struct Deps {
		::vector_s             files    ;
		::vmap_s<::optional_s> user_env ;
	} ;

	static constexpr const char* EnvironFile  = ADMIN_DIR_S "environ"   ; // provided to user, contains only variables used in Lmakefile.py
	static constexpr const char* ManifestFile = ADMIN_DIR_S "manifest"  ; // provided to user, contains the list of source files

	static ::string _g_tmp_dir_s    = cat(AdminDirS,"lmakefile_tmp/") ;
	static ::string _g_user_env_str ;

	::umap_ss clean_env(bool under_lmake_ok) {
		::umap_ss res = mk_environ() ;
		if ( !under_lmake_ok && res.contains("LMAKE_AUTODEP_ENV") ) exit(Rc::Usage,"cannot run lmake under lmake") ;
		::clearenv() ;
		::string repo_root = no_slash(*g_repo_root_s) ;
		uid_t    uid       = ::getuid()               ;
		set_env( "HOME"            ,     repo_root                        ) ;
		set_env( "LD_LIBRARY_PATH" ,     PY_LD_LIBRARY_PATH               ) ;
		set_env( "PATH"            , cat(*g_lmake_root_s,"bin:",STD_PATH) ) ;
		set_env( "PWD"             ,     repo_root                        ) ;
		set_env( "PYTHONPATH"      , cat(*g_lmake_root_s,"lib:"         ) ) ;
		set_env( "SHLVL"           ,     "1"                              ) ;
		set_env( "UID"             , cat(uid                            ) ) ;
		set_env( "USER"            ,     ::getpwuid(uid)->pw_name       ) ;
		return res ;
	}

	static ::string _deps_file( Action action , bool new_=false ) {
		if (new_) return cat(PrivateAdminDirS,action,"_new_deps") ;
		else      return cat(AdminDirS       ,action,"_deps"    ) ;
	}

	// dep file line format :
	// - first dep is special, marked with *, and provide lmake_root
	// - first char is file existence (+) or non-existence (!)
	// - then file name
	// dep check is satisfied if each dep :
	// - has a date before dep_file's date (if first char is +)
	// - does not exist                    (if first char is !)
	static ::string _chk_deps( Action action , ::umap_ss const& user_env , ::string const& startup_dir_s ) { // startup_dir_s for diagnostic purpose only
		Trace trace("_chk_deps",action) ;
		//
		::string deps_file = _deps_file(action)       ;
		Ddate    deps_date = FileInfo(deps_file).date ; if (!deps_date) { trace("not_found") ; return action>=Action::Plural ? "they were never read" : "it was never read" ; }
		::string reason    ;
		//
		::vector_s deps = AcFd(deps_file,{.err_ok=true}).read_lines(false/*partial_ok*/) ;
		for( ::string const& line : deps ) {
			SWEAR(+line) ;
			::string d = line.substr(1) ;
			switch (line[0]) {
				case '#' :                                                                                                                           break ; // comment
				case '*' :                                         if (d!=*g_lmake_root_s                          ) return "lmake root changed" ;   break ;
				case '~' :                                         if (d!=*g_repo_root_s                           ) return "repo root changed"  ;   break ;
				case '^' : if (action==Action::Config) { Gil gil ; if (!+py_run(parse_printable(d))->get_item("ok")) return "system tag changed" ; } break ;
				case '+' : {
					FileInfo fi { d } ;
					if (!fi.exists()     ) return cat(mk_rel(d,startup_dir_s)," was removed" ) ;
					if (fi.date>deps_date) return cat(mk_rel(d,startup_dir_s)," was modified") ; // in case of equality, be optimistic as deps may be modified during the read process ...
				} break ;                                                                        // ... (typically .pyc files) and file resolution is such that such deps may very well ...
				case '!' : {                                                                     // ... end up with same date as deps_file
					FileInfo fi { d } ;
					if (fi.exists()) return cat(mk_rel(d,startup_dir_s)," was created") ;
				} break ;
				case '=' : {
					size_t   pos = line.find('=',1)     ;
					::string key = line.substr(1,pos-1) ;
					auto     it  = user_env.find(key)   ;
					if      ( pos==Npos && it!=user_env.end()             ) return cat("environment variable ",key," appeared"   ) ;
					else if ( pos!=Npos && it==user_env.end()             ) return cat("environment variable ",key," disappeared") ;
					else if ( pos!=Npos && it->second!=line.substr(pos+1) ) return cat("environment variable ",key," changed"    ) ;
				} break ;
			DF}                                                                                  // NO_COV
		}
		trace("ok") ;
		return {} ;
	}

	static void _recall_env( ::umap_ss&/*out*/ user_env , Action action ) {
		Trace trace("_recall_env",action) ;
		//
		::vector_s deps = AcFd(_deps_file(action),{.err_ok=true}).read_lines(false/*partial_ok*/) ;
		for( ::string const& line : deps ) {
			SWEAR(+line) ;
			/**/                            if (line[0]!='=') continue ; // not an env var definition
			size_t pos = line.find('=',1) ; if (pos==Npos   ) continue ; // if no variable, nothing to recall
			user_env[line.substr(1,pos-1)] = line.substr(pos+1) ;        // lien contains =<key>=<value>
		}
		trace("ok",user_env.size()) ;
	}

	static void _chk_dangling( Action action , bool new_ , ::string const& startup_dir_s ) { // startup_dir_s for diagnostic purpose only
		Trace trace("_chk_dangling",action) ;
		//
		::vector_s deps = AcFd(_deps_file(action,new_),{.err_ok=true}).read_lines(false/*partial_ok*/) ;
		for( ::string const& line : deps ) {
			if (line[0]!='+') continue ;                                                     // not an existing file
			::string d = line.substr(1) ;
			if (is_abs(d)) continue ;                                                        // d is outside repo and cannot be dangling, whether it is in a src_dir or not
			Node n { New , d } ;
			n->set_buildable() ;                                                                                           // this is mandatory before is_src_anti() can be called
			if ( !n->is_src_anti() ) throw cat("while reading ",action,", dangling makefile : ",mk_rel(d,startup_dir_s)) ;
		}
		trace("ok") ;
	}

	static void _gen_deps( Action action , Deps const& deps , ::string const& startup_dir_s ) { // startup_dir_s for diagnostic purpose only
		SWEAR(+deps.files) ;                                                                    // there must at least be Lmakefile.py
		::string              new_deps_file = _deps_file(action,true/*new*/) ;
		::vmap_s<bool/*abs*/> glb_sds_s     ;
		//
		for( ::string const& sd_s : Record::s_autodep_env().src_dirs_s )
			if (!is_lcl(sd_s)) glb_sds_s.emplace_back(mk_glb(sd_s,*g_repo_root_s),is_abs(sd_s)) ;
		//
		::string deps_str ;
		/**/                        deps_str << "# * : lmake root"                                                                   <<'\n' ;
		/**/                        deps_str << "# ~ : repo root"                                                                    <<'\n' ;
		if (action==Action::Config) deps_str << "# ^ : system tag"                                                                   <<'\n' ;
		/**/                        deps_str << "# ! : file does not exist"                                                          <<'\n' ;
		/**/                        deps_str << "# + : file exists and date is compared with last read date"                         <<'\n' ;
		/**/                        deps_str << "# = : env variable (no value if not found in environ)"                              <<'\n' ;
		/**/                        deps_str << '*'<<*g_lmake_root_s                                                                 <<'\n' ;
		/**/                        deps_str << '~'<<*g_repo_root_s                                                                  <<'\n' ;
		if (action==Action::Config) deps_str << '^'<<mk_printable(g_config->system_tag+"ok=system_tag=="+g_config->system_tag_val()) <<'\n' ;
		for( ::string const& d : deps.files ) {
			SWEAR(+d) ;
			deps_str << ( FileInfo(d).exists() ? '+' : '!' ) ;
			if ( is_abs(d) && ::any_of( glb_sds_s , [&](auto const& sd_s_a) { return !sd_s_a.second && lies_within(d,sd_s_a.first) ; } ) ) deps_str << mk_lcl(d,*g_repo_root_s) ;
			else                                                                                                                           deps_str <<        d                 ;
			deps_str <<'\n' ;
		}
		for( auto const& [key,val] : deps.user_env ) {
			SWEAR(+key) ;
			if (+val) deps_str <<'='<<key<<'='<<*val<<'\n' ;
			else      deps_str <<'='<<key           <<'\n' ;
		}
		AcFd( new_deps_file , {O_WRONLY|O_TRUNC|O_CREAT} ).write( deps_str ) ;
		//
		_chk_dangling( action , true/*new*/ , startup_dir_s ) ;
	}

	static void _stamp_deps(Action action) {
		try                       { rename( _deps_file(action,true/*new*/) , _deps_file(action,false/*new*/)/*dst*/ ) ; }
		catch (::string const& e) { fail_prod("cannot stamp deps : ",e) ;                                               }
	}

	static RegExpr const* pyc_re = nullptr ;

	// msg may be updated even if throwing
	static void _read_makefile( ::string&/*out*/ msg , Ptr<Dict>&/*out*/ py_info , Deps&/*out*/ deps , ::string const& action , ::string const& sub_repos ) {
		Trace trace("_read_makefile",action,Pdate(New)) ;
		//
		::string data_file = cat(PrivateAdminDirS,action,"_data.py") ;
		Gather   gather    ;
		::string tmp_dir_s = cat(_g_tmp_dir_s,action,'/')            ;
		//
		//
		gather.autodep_env                = Record::s_autodep_env()                                                                                                   ;
		gather.autodep_env.fqdn           = fqdn()                                                                                                                    ;
		gather.autodep_env.src_dirs_s     = {"/"}                                                                                                                     ;
		gather.autodep_env.deps_in_system = true                                                                                                                      ; // we want all deps
		gather.cmd_line                   = { PYTHON , *g_lmake_root_s+"_lib/read_makefiles.py" , data_file , _g_user_env_str , cat('.',action,".top.") , sub_repos } ;
		gather.lmake_root_s               = *g_lmake_root_s                                                                                                           ;
		gather.child_stdin                = Child::NoneFd                                                                                                             ;
		gather.child_stdout               = Child::PipeFd                                                                                                             ;
		gather.child_stderr               = Child::JoinFd                                                                                                             ;
		//
		{	struct SavTmpDir {
				SavTmpDir (::string const& val) { set_env( "TMPDIR" , val ) ; }
				~SavTmpDir(                   ) { del_env( "TMPDIR"       ) ; }
			} ;
			SavPyLdLibraryPath spllp       ;
			SavTmpDir          sav_tmp_dir { no_slash(*g_repo_root_s+tmp_dir_s) } ;
			mk_dir_empty_s(tmp_dir_s) ;                                             // leave tmp dir after execution for debug purpose as we have no keep-tmp flags
			//              vvvvvvvvvvvvvvvvvvv
			Status status = gather.exec_child() ;
			//              ^^^^^^^^^^^^^^^^^^^
			msg += gather.stdout ;
			if (status!=Status::Ok) {
				if (+gather.msg) throw ::pair( cat("cannot read ",action," : ",localize(gather.msg)) , Rc::BadMakefile ) ;
				else             throw ::pair( cat("cannot read ",action                           ) , Rc::BadMakefile ) ;
			}
		}
		//
		deps.files.reserve(gather.accesses.size()) ;
		::string   deps_str = AcFd(data_file).read() ;
		::uset_s   dep_set  ;
		try                       { py_info = py_eval(deps_str) ; }
		catch (::string const& e) { FAIL(e) ;                     }                 // NO_COV
		for( auto& [d,ai] : gather.accesses ) {
			/**/             if ( ai.first_write()<Pdate::Future )   continue ;
			trace("dep",d) ; if ( Match m=pyc_re->match(d) ; +m  ) { d = cat(m.group(d,1/*dir_s*/),m.group(d,2/*module*/),".py") ; trace("dep_py",d) ; }
			/**/             if ( dep_set.insert(d).second       )   deps.files.push_back(::move(d)) ;
		}
		if (py_info->contains("user_environ")) {
			for( auto const& [py_key,py_val] : py_info->get_item("user_environ").as_a<Dict>() )
				if (&py_val==&None) deps.user_env.emplace_back( py_key.as_a<Str>() , ::optional_s()     ) ;
				else                deps.user_env.emplace_back( py_key.as_a<Str>() , py_val.as_a<Str>() ) ;
			py_info->del_item("user_environ") ;
		}
		trace("done",Pdate(New)) ;
	}

	// msg may be updated even if throwing
	// startup_dir_s is for diagnostic purpose only
	static bool/*done*/ _refresh_config( ::string&/*out*/ msg , Config&/*out*/ config , Ptr<Dict>&/*out*/ py_info , Deps&/*out*/ deps , ::umap_ss const& user_env , ::string const& startup_dir_s ) {
		Trace trace("refresh_config") ;
		::string reason = _chk_deps( Action::Config , user_env , startup_dir_s ) ; if (!reason) return false/*done*/ ;
		//
		msg << "read config because "<<reason<<'\n' ;
		Gil gil ;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		_read_makefile( /*out*/msg , /*out*/py_info , /*out*/deps , "config" , "..."/*sub_repos*/ ) ; // discover sub-repos while recursing into them
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		try                      { config = (*py_info)["config"].as_a<Dict>() ;                                     }
		catch(::string const& e) { throw ::pair( cat("while processing config :\n",indent(e)) , Rc::BadMakefile ) ; }
		config.rules_action = py_info->get_item("rules_action"  ).as_a<Str>() ;
		config.srcs_action  = py_info->get_item("sources_action").as_a<Str>() ;
		//
		return true/*done*/ ;
	}

	template<Action A,class T> static Bool3/*done*/ _refresh_rules_srcs(                                             // Maybe means not split
		::string&/*out*/ msg                                                                                         // msg may be updated even if throwing
	,	T       &/*out*/ res
	,	Deps    &/*out*/ deps
	,	Bool3            changed                                                                                     // Maybe means new, Yes means existence of module/callable changed
	,	Dict      const* py_info
	,	::umap_ss const& user_env
	,	::string  const& startup_dir_s                                                                               // startup_dir_s for diagnostic purpose only
	) {
		::string const& config_action = A==Action::Rules ? g_config->rules_action : g_config->srcs_action ;
		Trace trace("_refresh_rules_srcs",A,changed,config_action) ;
		if ( !config_action && !py_info && changed==No ) return Maybe/*done*/ ;                                      // sources has not been read
		::string  reason      ;
		Gil       gil         ;                                                                                      // ensure Gil is taken when py_new_info is destroyed
		Ptr<Dict> py_new_info ;
		if (+config_action) {
			switch (changed) {
				case Yes   :
					if      (config_action.find("import"  )!=Npos) reason = "module "   ;
					else if (config_action.find("callable")!=Npos) reason = "function " ;
					reason << "Lmakefile."<<A<<" appeared" ;
				break ;
				case Maybe :
					reason = cat("Lmakefile.",A) ;
					if      (config_action.find("import"  )!=Npos) reason = "module "   +reason+" was never imported" ;
					else if (config_action.find("callable")!=Npos) reason = "function " +reason+"() was never called" ;
					else if (config_action.find("dflt"    )!=Npos) reason = "default sources were never read"         ;
					else                                           reason =              reason+" was never read"     ;
				break ;
				case No    :
					reason = _chk_deps( A , user_env , startup_dir_s ) ;
					if (!reason) return No/*done*/ ;
				break ;
			}
			SWEAR(+reason) ;
			::string sub_repos_s ;
			First    first       ;
			/**/                                                sub_repos_s << "("                            ;
			for( ::string const& sr_s : g_config->sub_repos_s ) sub_repos_s <<first("",",")<< mk_py_str(sr_s) ;      // use sub-repos list discovered during config
			/**/                                                sub_repos_s <<first("",",","")<<')'           ;      // singletons must have a terminating ','
			msg<<"read "<<A<<" because "<<reason<<'\n' ;
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			_read_makefile( /*out*/msg , /*out*/py_new_info , /*out*/deps , cat(A,'.',config_action) , sub_repos_s ) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			py_info = py_new_info ;
		}
		try                      { res = (*py_info)[snake_str(A)].as_a<typename T::PyType>() ;                     }
		catch(::string const& e) { throw ::pair( cat("while processing ",A+" :\n",indent(e)) , Rc::BadMakefile ) ; }
		return Maybe|+reason/*done*/ ;                                                                               // cannot be split without reason
	}

	// msg may be updated even if throwing
	// startup_dir_s is for diagnostic purpose only
	static void _refresh( ::string&/*out*/ msg , bool rescue , bool refresh_ , ::umap_ss const& user_env , ::string const& startup_dir_s ) {
		Trace trace("_refresh",STR(rescue),STR(refresh_),startup_dir_s) ;
		static bool s_first_time = true ; bool first_time = s_first_time ; s_first_time = false ;
		//
		const char* dynamically = first_time ? "" : "dynamically " ;
		if (!refresh_) {
			SWEAR(first_time) ;
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			Persistent::new_config( Config() , rescue ) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			return ;
		}
		Deps               config_deps ;
		Deps               rules_deps  ;
		Deps               srcs_deps   ;
		Config             config      ;
		WithGil<Ptr<Dict>> py_info     ;
		//
		if (first_time) {
			{	First first ;
				_g_user_env_str << "{ " ;
				for( auto const& [k,v] : user_env )
					_g_user_env_str << first(""," , ")<< mk_py_str(k) <<":"<< mk_py_str(v) ;
				_g_user_env_str << " }" ;
			}
			AcFd( EnvironFile  , {O_RDONLY|O_CREAT} ) ;                                            // these are sources, they must exist
			AcFd( ManifestFile , {O_RDONLY|O_CREAT} ) ;                                            // .
		}
		//
		bool/*done*/ config_digest = _refresh_config( /*out*/msg , /*out*/config , /*out*/py_info , /*out*/config_deps , user_env , startup_dir_s ) ;
		//
		Bool3 changed_srcs       = No    ;
		Bool3 changed_rules      = No    ;
		bool  invalidate         = false ;                                                         // invalidate because of config
		bool  changed_extra_srcs = false ;
		bool  doing_ancillaries  = false ;
		auto diff_config = [&]( Config const& old , Config const& new_ ) {
			if (+new_) {                                                                           // no new config means keep old config, no modification
				changed_srcs       = +old ? No|(old.srcs_action   !=new_.srcs_action   ) : Maybe ; // Maybe means new
				changed_rules      = +old ? No|(old.rules_action  !=new_.rules_action  ) : Maybe ; // Maybe means new
				invalidate         =            old.sub_repos_s   !=new_.sub_repos_s             ; // this changes matching exceptions, which means it changes matching
				changed_extra_srcs =            old.extra_manifest!=new_.extra_manifest          ;
			}
			if (!first_time) {               // fast path : on first time, we do not know if we are ever going to launch jobs, dont spend time configuring
				static bool s_done = false ;
				Config const& cfg =  +new_ ? new_ : old ;
				doing_ancillaries = true ;
				if ( !s_done || (+new_&&old.backends!=new_.backends) ) Backends::Backend        ::s_config(cfg.backends) ; // no new_ means keep old config
				if ( !s_done || (+new_&&old.caches  !=new_.caches  ) ) Cache   ::CacheServerSide::s_config(cfg.caches  ) ; // .
				doing_ancillaries = false ;
				s_done            = true  ;
			}
		} ; //!                         vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		try                           { Persistent::new_config( ::move(config) , rescue , diff_config ) ;                                                      }
		//                              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		catch (::string     const& e) { if (doing_ancillaries) throw ; else throw         cat("cannot ",dynamically,"update config : ",e      )              ; }
		catch (::pair_s<Rc> const& e) { if (doing_ancillaries) throw ; else throw ::pair( cat("cannot ",dynamically,"update config : ",e.first) , e.second ) ; }
		//
		// /!\ sources must be processed first as source dirs influence rules
		//
		Sources       srcs        ;
		Bool3/*done*/ srcs_digest = _refresh_rules_srcs<Action::Sources>( /*out*/msg , /*out*/srcs , /*out*/srcs_deps , changed_srcs , py_info , user_env , startup_dir_s ) ;   // Maybe means not split
		bool          new_srcs    = srcs_digest==Yes || (srcs_digest==Maybe&&config_digest) || changed_extra_srcs                                                           ;
		if (new_srcs) {
			for( ::string const& s : g_config->extra_manifest ) srcs.push_back(s) ;
			//                                            vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			try                           { invalidate |= Persistent::new_srcs( ::move(srcs) , ManifestFile ) ;                 }
			//                                            ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			catch (::string     const& e) { throw         cat("cannot ",dynamically,"update sources : ",e      )              ; }
			catch (::pair_s<Rc> const& e) { throw ::pair( cat("cannot ",dynamically,"update sources : ",e.first) , e.second ) ; }
		}
		Rules         rules        ;
		Bool3/*done*/ rules_digest = _refresh_rules_srcs<Action::Rules>( /*out*/msg , /*out*/rules , /*out*/rules_deps , changed_rules , py_info , user_env , startup_dir_s ) ; // Maybe means not split
		bool          new_rules    = rules_digest==Yes || (rules_digest==Maybe&&config_digest)                                                                                ;
		if (new_rules) //!                                vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			try                           { invalidate |= Persistent::new_rules( ::move(rules) ) ;                            }
			//                                            ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			catch (::string     const& e) { throw         cat("cannot ",dynamically,"update rules : ",e      )              ; }
			catch (::pair_s<Rc> const& e) { throw ::pair( cat("cannot ",dynamically,"update rules : ",e.first) , e.second ) ; }
		//
		if (invalidate) Persistent::invalidate_match() ;
		//
		if      ( config_digest                 ) _gen_deps    ( Action::Config  , config_deps  , startup_dir_s ) ;
		else if (                      new_srcs ) _chk_dangling( Action::Config  , false/*new*/ , startup_dir_s ) ; // if sources have changed, some deps may have become dangling
		if      ( srcs_digest ==Yes             ) _gen_deps    ( Action::Sources , srcs_deps    , startup_dir_s ) ;
		else if ( srcs_digest ==No  && new_srcs ) _chk_dangling( Action::Sources , false/*new*/ , startup_dir_s ) ; // .
		if      ( rules_digest==Yes             ) _gen_deps    ( Action::Rules   , rules_deps   , startup_dir_s ) ;
		else if ( rules_digest==No  && new_srcs ) _chk_dangling( Action::Rules   , false/*new*/ , startup_dir_s ) ; // .
		//
		// once all error cases have been cleared, stamp deps and generate environ file for user
		if ( config_digest || srcs_digest==Yes || rules_digest==Yes ) {
			::umap_ss ue ;
			if (config_digest    ) { _stamp_deps(Action::Config ) ; for( auto const& [k,v] : config_deps.user_env ) if (+v) ue[k] = *v ; } else _recall_env(ue,Action::Config ) ;
			if (srcs_digest ==Yes) { _stamp_deps(Action::Sources) ; for( auto const& [k,v] : srcs_deps  .user_env ) if (+v) ue[k] = *v ; } else _recall_env(ue,Action::Sources) ;
			if (rules_digest==Yes) { _stamp_deps(Action::Rules  ) ; for( auto const& [k,v] : rules_deps .user_env ) if (+v) ue[k] = *v ; } else _recall_env(ue,Action::Rules  ) ;
			::string user_env_str ;
			for( auto const& [k,v] : ue ) user_env_str << k<<'='<<mk_printable(v)<<'\n' ;
			AcFd( EnvironFile , {.flags=O_WRONLY|O_TRUNC} ).write(user_env_str) ;
		}
		trace("done") ;
	}

	// msg may be updated even if throwing
	// startup_dir_s is for diagnostic purpose only
	void refresh( ::string&/*out*/ msg , ::umap_ss const& env , bool rescue , bool refresh_ , ::string const& startup_dir_s ) {
		Trace trace("refresh",STR(rescue),STR(refresh_)) ;
		static bool     s_first_time   = true                                  ; bool first_time = s_first_time ; s_first_time = false ;
		static ::string reg_exprs_file = cat(PrivateAdminDirS,"regexpr_cache") ;
		//
		if (first_time) {
			AcFd fd { reg_exprs_file ,{.err_ok=true} } ;
			if (+fd)
				try         { deserialize( ::string_view(fd.read()) , RegExpr::s_cache ) ;                                      } // load from persistent cache
				catch (...) { Fd::Stderr.write(cat("cannot read reg expr cache (no consequences) from ",reg_exprs_file,'\n')) ; } // perf only, ignore errors (e.g. first time)
		}
		//
		// ensure this regexpr is always set, even when useless to avoid cache instability depending on whether makefiles have been read or not
		pyc_re = new RegExpr{ R"(((?:.*/)?)(?:(?:__pycache__/)?)(\w+)(?:(?:\.\w+-\d+)?)\.pyc)" , true/*cache*/ } ;                // dir_s is \1, module is \2, matches both python 2 & 3
		//
		_refresh( /*out*/msg , rescue , refresh_ , env , startup_dir_s ) ;
		//
		if ( first_time && !RegExpr::s_cache.steady() )
			try         { AcFd( reg_exprs_file , {O_WRONLY|O_TRUNC|O_CREAT} ).write( serialize(RegExpr::s_cache) ) ;       }      // update persistent cache
			catch (...) { Fd::Stderr.write(cat("cannot write reg expr cache (no consequences) to ",reg_exprs_file,'\n')) ; }      // perf only, ignore errors (e.g. read-only)
		trace("done",msg) ;
	}

}
