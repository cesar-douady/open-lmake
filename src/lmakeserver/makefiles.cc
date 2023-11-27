// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"
#include "time.hh"

#include "autodep/gather_deps.hh"

#include "core.hh"

#include "makefiles.hh"

using namespace Disk ;
using namespace Time ;

namespace Engine::Makefiles {

	struct SrcDirsS {
		::vmap_s<bool/*is_abs*/> glb ; // source dirs outside repo in an absolute form
		::vector_s               lcl ; // source dirs inside  repo
	} ;

	static inline SrcDirsS _split_src_dirs_s(::vector_s const& src_dirs_s) {
		SrcDirsS res ;
		for( ::string const& sd_s : src_dirs_s )
			if (is_lcl_s(sd_s)) res.lcl.push_back   (       sd_s                            ) ;
			else                res.glb.emplace_back(mk_abs(sd_s,*g_root_dir),is_abs_s(sd_s)) ;
		return res ;
	}

	static ::umap<Crc,RuleData> _gather_rules(Py::Sequence const& py_rules) {
		::umap<Crc,RuleData> rules ;
		::uset_s             names ;
		for( Py::Object py_obj : py_rules ) {
			RuleData rd  = Py::Dict(py_obj) ;
			Crc      crc = rd.match_crc     ;
			if (names.contains(rd.name)) {
				if ( rules.contains(crc) && rules.at(crc).name==rd.name ) throw to_string("rule " , rd.name , " appears twice"      ) ;
				else                                                      throw to_string("two rules have the same name " , rd.name ) ;
			}
			if (rules.contains(crc)) throw to_string( "rule " , rd.name , " and rule " , rules.at(crc).name , " match identically and are redundant" ) ;
			names.insert(rd.name) ;
			rules[crc] = ::move(rd) ;
		}
		for (::string const& sd_s : g_config.src_dirs_s ) {
			RuleData rd { Special::GenericSrc , sd_s } ;
			rules[rd.match_crc] = ::move(rd) ;
		}
		return rules ;
	}

	static ::vector_s _gather_srcs(Py::Sequence const& py_srcs) {
		RealPath   real_path      {{ .lnk_support=g_config.lnk_support , .root_dir=*g_root_dir }} ;
		::vector_s srcs           ;
		::vector_s lcl_src_dirs_s = _split_src_dirs_s(g_config.src_dirs_s).lcl                    ;
		for( Py::Object py_obj : py_srcs ) {
			::string src ;
			try                       { src = Py::String(py_obj) ; }
			catch(::Py::Exception& e) { throw e.errorValue() ;     }
			if (src.empty()) throw "found an empty source"s ;
			RealPath::SolveReport sr = real_path.solve(src,true/*no_follow*/) ;
			//
			if (!sr.lnks.empty()   ) throw to_string("source ",src," : found a link in its path : ",sr.lnks[0]) ;
			if (sr.kind!=Kind::Repo) throw to_string("source ",src," : not in repository"                     ) ;
			if (sr.real!=src       ) throw to_string("source ",src," : canonical form is "         ,sr.real   ) ;
			if (g_config.lnk_support==LnkSupport::None) { if (!is_reg   (src)) throw to_string("source ",src," is not a regular file"           ) ; }
			else                                        { if (!is_target(src)) throw to_string("source ",src," is not a regular file nor a link") ; }
			::string src_s = src+'/' ;
			for ( ::string const& sd_s : lcl_src_dirs_s ) {
				if (!src_s.starts_with(sd_s) ) continue ;
				if (src_s.size()==sd_s.size()) throw to_string(src," is both a source and a source dir"                           ) ;
				else                           throw to_string("source ",src," is within source dir ",sd_s.substr(0,sd_s.size()-1)) ;
			}
			srcs.emplace_back(src) ;
		}
		return srcs ;

	}

	// dep file line format :
	// - first char is file existence (+) or non-existence (!)
	// - then file name
	// dep check is satisfied if each dep :
	// - has a date before dep_file's date (if first char is +)
	// - does not exist                    (if first char is !)
	static ::string _chk_deps( ::string const& action , ::string const& startup_dir_s ) { // startup_dir_s for diagnostic purpose only
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
			::string     dep_name = line.substr(1) ;
			FileInfoDate fid      { dep_name }     ;
			if      (  exists && !fid               ) reason = "removed"                                              ;
			else if (  exists && fid.date>deps_date ) reason = "modified"                                             ; // in case of equality, be optimistic as deps may be modified during the ...
			else if ( !exists && +fid               ) reason = "created"                                              ; // ... read process (typically .pyc files) and file resolution is such ...
			if      ( !reason.empty()               ) return to_string(mk_rel(dep_name,startup_dir_s)," was ",reason) ; // ...  that such deps may very well end up with same date as deps_file
		}
		trace("ok") ;
		return {} ;
	}

	static inline ::string _deps_file( ::string const& action , bool new_ ) {
		if (new_) return to_string(PrivateAdminDir,'/',action,"_new_deps") ;
		else      return to_string(AdminDir       ,'/',action,"_deps"    ) ;
	}

	static void _chk_dangling( ::string const& action , bool new_ , ::uset_s srcs , ::string const& startup_dir_s ) { // startup_dir_s for diagnostic purpose only
		Trace trace("_chk_dangling",action) ;
		//
		::string   deps_file   = _deps_file(action,new_) ;
		::ifstream deps_stream { deps_file }             ;
		::vector_s lcl_src_dirs_s = _split_src_dirs_s(g_config.src_dirs_s).lcl ;
		for( ::string line ; ::getline(deps_stream,line) ;) {
			switch (line[0]) {
				case '+' : break ;
				case '!' : continue ;
				default  : FAIL(line[0]) ;
			}
			::string d = line.substr(1) ;
			if (is_abs(d)) continue ;
			if (srcs.empty())                             { if (Node(d)->is_src()  ) goto NotDangling ; } // if srcs is empty, we hve not read sources and store is up to date for sources
			else                                          { if (srcs.contains(d)   ) goto NotDangling ; }
			for ( ::string const& sd_s : lcl_src_dirs_s )   if (d.starts_with(sd_s)) goto NotDangling ;
			throw to_string("dangling makefile : ",mk_rel(d,startup_dir_s)) ;
		NotDangling : ;
		}
		trace("ok") ;
	}

	static void _gen_deps( ::string const& action , ::vector_s const& deps , ::uset_s srcs , ::string const& startup_dir_s ) {
		::string root_dir_s    = *g_root_dir+'/'                        ;
		::string deps_file     = _deps_file(action,false/*new*/)        ;
		::string new_deps_file = _deps_file(action,true /*new*/)        ;
		SrcDirsS sds_s         = _split_src_dirs_s(g_config.src_dirs_s) ;
		OFStream os            { ::dir_guard(new_deps_file) }           ;
		{	for( ::string d : deps ) {
				SWEAR(!d.empty()) ;
				FileInfo fi{d} ;
				if (is_abs(d)) {
					for( auto const& [sd_s,ia] : sds_s.glb ) {
						if (!(d+'/').starts_with(sd_s)) continue ;
						if (!ia                       ) d = mk_rel(d,root_dir_s) ;
						break ;
					}
				}
				os << (+fi?'+':'!') << d <<'\n' ;
			}
		}
		_chk_dangling(action,true/*new*/,srcs,startup_dir_s) ;                 // ensure deps have been pushed to disk (stream closed or flushed)
	}

	static void _stamp_deps(::string const& action) {
		swear_prod( ::rename( _deps_file(action,true/*new*/).c_str() , _deps_file(action,false/*new*/).c_str() )==0 , "stamp deps for ",action) ;
	}

	static ::pair<PyObject*,::vector_s/*deps*/> _read_makefiles( ::string const& action , ::string const& module ) {
		static Py::Pattern pyc_re1 { "(?P<dir>(.*/)?)(?P<module>\\w+)\\.pyc"                         } ;
		static Py::Pattern pyc_re2 { "(?P<dir>(.*/)?)__pycache__/(?P<module>\\w+)\\.\\w+-\\d+\\.pyc" } ;
		[[maybe_unused]] static bool        boosted = (Py::boost(pyc_re1),Py::boost(pyc_re2),true)                      ; // avoid deallocation problems at exit
		//
		GatherDeps  gather_deps { New }                                                                        ; gather_deps.autodep_env.src_dirs_s = {"/"} ;
		::string    data        = to_string(PrivateAdminDir,'/',action,"_data.py")                             ;
		::vector_s  cmd_line    = { PYTHON , *g_lmake_dir+"/_lib/read_makefiles.py" , data , action , module } ;
		Trace trace("_read_makefiles",action,module,Pdate::s_now()) ;
		//
		::string sav_ld_library_path ;
		if (PYTHON_LD_LIBRARY_PATH[0]!=0) {
			sav_ld_library_path = get_env("LD_LIBRARY_PATH") ;
			if (!sav_ld_library_path.empty()) set_env( "LD_LIBRARY_PATH" , to_string(sav_ld_library_path,':',PYTHON_LD_LIBRARY_PATH) ) ;
			else                              set_env( "LD_LIBRARY_PATH" ,                                   PYTHON_LD_LIBRARY_PATH  ) ;
		}
		//              vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		Status status = gather_deps.exec_child( cmd_line , Child::None/*stdin*/ , Fd::Stderr/*stdout*/ ) ; // redirect stdout to stderr as our stdout may be used communicate with client
		//              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		if (PYTHON_LD_LIBRARY_PATH[0]!=0) set_env( "LD_LIBRARY_PATH" , sav_ld_library_path ) ;
		//
		if (status!=Status::Ok) throw "cannot read "+action ;
		//
		::string   content = read_content(data) ;
		::vector_s deps    ; deps.reserve(gather_deps.accesses.size()) ;
		for( auto const& [d,ai] : gather_deps.accesses ) {
			if (!ai.digest.idle()) continue ;
			Py::Match m = pyc_re1.match(d) ; if (!m) m = pyc_re2.match(d) ;
			if (+m) { ::string py = to_string(m["dir"],m["module"],".py") ; trace("dep",d,"->",py) ; deps.push_back(py) ; } // special case to manage pyc
			else    {                                                       trace("dep",d        ) ; deps.push_back(d ) ; }
		}
		PyObject* eval_env = PyDict_New() ;
		PyDict_SetItemString( eval_env , "inf"          , *Py::Float(Infinity) ) ;
		PyDict_SetItemString( eval_env , "nan"          , *Py::Float(nan("") ) ) ;
		PyDict_SetItemString( eval_env , "__builtins__" , PyEval_GetBuiltins() ) ;          // Python3.6 does not provide it for us
		PyObject* py_info = PyRun_String(content.c_str(),Py_eval_input,eval_env,eval_env) ;
		Py_DECREF(eval_env) ;
		SWEAR( py_info , "error while reading makefile digest :\n" , Py::err_str() ) ;
		trace("done",Pdate::s_now()) ;
		return { py_info , deps } ;
	}

	static ::pair_s<bool/*done*/> _refresh_config( Config& config , PyObject*& py_info , ::vector_s& deps , ::string const& startup_dir_s ) {
		Trace trace("refresh_config") ;
		::string reason = _chk_deps( "config" , startup_dir_s ) ;
		if (reason.empty()) return {{},false/*done*/} ;
		//
		//                  vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		tie(py_info,deps) = _read_makefiles( "config" , "Lmakefile" ) ;
		//                  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		try                      { config = Py::Mapping(Py::Dict(py_info)["config"]) ;                             }
		catch(::string const& e) { Py_DECREF(py_info) ; throw to_string("while processing config :\n",indent(e)) ; }
		//
		return { "read config because "+reason , true/*done*/ } ;
	}

	// /!\ the 2 following functions are mostly identical, one for rules, the other for sources, but are difficult to share, modifications must be done in both simultaneously

	static ::pair_s<bool/*done*/> _refresh_rules( ::umap<Crc,RuleData>& rules , ::vector_s& deps , const char* new_rules_module , bool dynamic , PyObject* py_info , ::string const& startup_dir_s ) {
		Trace trace("_refresh_rules") ;
		::string reason ;
		if (g_config.rules_module.empty()) {
			if (!py_info) return {{},false/*done*/}              ;             // config has not been read
			if (dynamic ) throw "cannot dynamically read rules"s ;
			else          goto Load                              ;
		}
		if (new_rules_module) reason = "config.rules_module was "s+new_rules_module ;
		else                  reason = _chk_deps( "rules" , startup_dir_s )     ;
		if (reason.empty()           ) return {{},false/*done*/}                                             ;
		if (dynamic                  ) throw to_string("cannot dynamically read rules (because ",reason,')') ;
		reason = "read rules because " + reason ;
		//                  vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		tie(py_info,deps) = _read_makefiles( "rules" , g_config.rules_module ) ;
		//                  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	Load :
		try                      { rules = _gather_rules(Py::Sequence(Py::Dict(py_info)["rules"])) ; }
		catch(::string const& e) { throw to_string("while processing rules :\n",indent(e)) ;         }
		return {reason,true/*done*/} ;
	}
	static ::pair_s<bool/*done*/> _refresh_srcs( ::vector_s& srcs , ::vector_s& deps , const char* new_srcs_module , bool dynamic , PyObject* py_info , ::string const& startup_dir_s ) {
		Trace trace("_refresh_srcs") ;
		::string reason ;
		if (g_config.srcs_module.empty()) {
			if (!py_info) return {{},false/*done*/}                ;           // config has not been read
			if (dynamic ) throw "cannot dynamically read sources"s ;
			else          goto Load                                ;
		}
		if (new_srcs_module) reason = "config.sources_module was "s+new_srcs_module ;
		else                 reason = _chk_deps( "sources" , startup_dir_s )    ;
		if (reason.empty()          ) return {{},false/*done*/}                                               ;
		if (dynamic                 ) throw to_string("cannot dynamically read sources (because ",reason,')') ;
		reason = "read sources because " + reason ;
		//                  vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		tie(py_info,deps) = _read_makefiles( "srcs" , g_config.srcs_module ) ;
		//                  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	Load :
		try                      { srcs = _gather_srcs(Py::Sequence(Py::Dict(py_info)["manifest"])) ; }
		catch(::string const& e) { throw to_string("while processing sources :\n",indent(e)) ;        }
		if (srcs.empty())          throw "no sources found"                                  ;
		return {reason,true/*done*/} ;
	}

	::string/*msg*/ _refresh( bool chk , bool refresh_ , bool dynamic , ::string const& startup_dir_s ) {
		Trace trace("_refresh",STR(chk),STR(refresh_),STR(dynamic),startup_dir_s) ;
		if (!refresh_) {
			EngineStore::s_new_config( Config() , false , chk ) ;
			return {} ;
		}
		Py::Gil                gil           ;
		::vector_s             config_deps   ;
		::vector_s             rules_deps    ;
		::vector_s             srcs_deps     ;
		Config                 config        ;
		PyObject*              py_info       = nullptr                                                           ;
		::pair_s<bool/*done*/> config_digest = _refresh_config( config , py_info , config_deps , startup_dir_s ) ;
		//
		try {
			const char* new_rules_module = nullptr ;
			const char* new_srcs_module  = nullptr ;
			auto diff_config      = [&]( Config const& old , Config const& new_ )->void {
				if ( !old.booted || !new_.booted         ) return ;                                                                                                   // only record diffs
				if ( old.rules_module!=new_.rules_module ) new_rules_module = old.rules_module.empty() ? "set" : new_.rules_module.empty() ? "cleared" : "modified" ;
				if ( old.srcs_module !=new_.srcs_module  ) new_srcs_module  = old.srcs_module .empty() ? "set" : new_.srcs_module .empty() ? "cleared" : "modified" ;
			} ;
			//           vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			EngineStore::s_new_config( ::move(config) , dynamic , chk , diff_config ) ;
			//           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			//
			::umap<Crc,RuleData> rules ; ::pair_s<bool/*done*/> rules_digest = _refresh_rules( rules , rules_deps , new_rules_module , dynamic , py_info , startup_dir_s ) ;
			::vector_s           srcs  ; ::pair_s<bool/*done*/> srcs_digest  = _refresh_srcs ( srcs  , srcs_deps  , new_srcs_module  , dynamic , py_info , startup_dir_s ) ;
			//
			::uset_s src_set = mk_uset(srcs) ;
			if      (config_digest.second) _gen_deps    ( "config"  , config_deps  , src_set , startup_dir_s ) ;
			else if (srcs_digest  .second) _chk_dangling( "config"  , false/*new*/ , src_set , startup_dir_s ) ; // if sources have changed, some deps may have becom dangling
			if      (rules_digest .second) _gen_deps    ( "rules"   , rules_deps   , src_set , startup_dir_s ) ;
			else if (srcs_digest  .second) _chk_dangling( "rules"   , false/*new*/ , src_set , startup_dir_s ) ; // .
			if      (srcs_digest  .second) _gen_deps    ( "sources" , srcs_deps    , src_set , startup_dir_s ) ;
			//
			::string msg ;
			if (!config_digest.first.empty()) append_to_string(msg,config_digest.first,'\n') ;
			if (!rules_digest .first.empty()) append_to_string(msg,rules_digest .first,'\n') ;
			if (!srcs_digest  .first.empty()) append_to_string(msg,srcs_digest  .first,'\n') ;
			//
			//                                                         vvvvvvvvvvvvvvvvvvvvvvvvvv
			bool invalidate_rule = rules_digest.second && EngineStore::s_new_rules(::move(rules)) ;
			bool invalidate_src  = srcs_digest .second && EngineStore::s_new_srcs (::move(srcs) ) ;
			//                                                         ^^^^^^^^^^^^^^^^^^^^^^^^^^
			if ( invalidate_src || invalidate_rule ) EngineStore::s_invalidate_match() ;
			//
			if (config_digest.second) _stamp_deps("config" ) ;                 // stamp deps once all error cases have been cleared
			if (rules_digest .second) _stamp_deps("rules"  ) ;                 // .
			if (srcs_digest  .second) _stamp_deps("sources") ;                 // .
			//
			Py_DecRef(py_info) ;
			return msg ;
		} catch (::string const&) {
			Py_DecRef(py_info) ;
			throw ;
		}
	}

	::string/*msg*/ refresh        ( bool chk , bool refresh_    ) { return _refresh( chk   , refresh_ , false/*dynamic*/ , *g_startup_dir_s ) ; }
	::string/*msg*/ dynamic_refresh(::string const& startup_dir_s) { return _refresh( false , true     , true /*dynamic*/ , startup_dir_s    ) ; }

}
