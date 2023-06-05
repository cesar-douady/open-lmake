// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
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

namespace Engine {

	::string Makefiles::s_makefiles    = AdminDir+"/makefiles"s    ;
	::string Makefiles::s_no_makefiles = AdminDir+"/no_makefiles"s ;
	::string Makefiles::s_config_file  ;

	static ::umap<Crc,RuleData> _gather_rules(Py::Sequence const& py_rules) {
		::umap<Crc,RuleData> rules ;
		::uset_s             names ;
		for( Py::Object py_obj : py_rules ) {
			RuleData rule = Py::Dict(py_obj) ;
			Crc      crc  = rule.match_crc() ;
			if (names.contains(rule.name)) {
				if ( rules.contains(crc) && rules.at(crc).name==rule.name ) throw to_string("rule " , rule.name , " appears twice"     ) ;
				else                                                        throw to_string("two rules have the same name " , rule.name ) ;
			}
			if (rules.contains(crc)) throw to_string( "rule " , rule.name , " and rule " , rules.at(crc).name , " match identically and are redundant" ) ;
			names.insert(rule.name) ;
			rules[crc] = ::move(rule) ;
		}
		return rules ;
	}

	static ::vector_s _gather_srcs( Py::Sequence const& py_srcs , ServerConfig const& config ) {
		RealPath   real_path { config.lnk_support                  } ;
		Save       save      { g_config.path_max , config.path_max } ;  // need g_config.path_max to create nodes, so set it temporarily
		::vector_s srcs      ;
		for( Py::Object py_obj : py_srcs ) {
			::string src ;
			try                       { src = Py::String(py_obj) ; }
			catch(::Py::Exception& e) { throw e.errorValue() ;     }
			if (src.empty()) throw "found an empty source"s ;
			RealPath::SolveReport rp = real_path.solve(src,true/*no_follow*/) ;
			//
			if (!rp.lnks.empty()) throw to_string("source ",src," : found a link in its path : ",rp.lnks[0]) ;
			if (rp.real!=src    ) throw to_string("source ",src," : canonical form is "         ,rp.real   ) ;
			if (config.lnk_support==LnkSupport::None) { if (!is_reg   (src)) throw to_string("source ",src," is not a regular file"           ) ; }
			else                                      { if (!is_target(src)) throw to_string("source ",src," is not a regular file nor a link") ; }
			srcs.emplace_back(src) ;
		}
		return srcs ;
	}

	bool/*ok*/ Makefiles::_s_chk_makefiles(DiskDate& latest_makefile) {
		DiskDate   makefiles_date   = file_date(s_makefiles) ;              // ensure we gather correct date with NFS
		::ifstream makefiles_stream { s_makefiles }          ;
		Trace trace("s_chk_makefiles",makefiles_date) ;
		if (is_reg(s_no_makefiles)) { trace("found"    ,s_no_makefiles) ; return false ; } // s_no_makefile_file is a marker that says s_makefiles is invalid (so that is can written early)
		if (!makefiles_stream     ) { trace("not_found",s_makefiles   ) ; return false ; }
		char line[PATH_MAX+1] ;                                                            // account for first char (+ or !)
		while (makefiles_stream.getline(line,sizeof(line))) {
			SWEAR( line[0]=='+' || line[0]=='!' ) ;
			bool         exists    = line[0]=='+' ;
			FileInfoDate file_info { line+1 } ;
			latest_makefile = ::max(latest_makefile,file_info.date) ;
			if (exists) {
				if ( !file_info                     ) { trace("missing" ,line+1) ; return false ; }
				if ( file_info.date>=makefiles_date ) { trace("modified",line+1) ; return false ; } // in case of equality, be pessimistic
			} else {
				if ( +file_info                     ) { trace("appeared",line+1) ; return false ; }
			}
		}
		trace("ok") ;
		return true ;
	}

	static ::pair<vector_s/*deps*/,string/*stdout*/> _read_makefiles() {
		Py::Pattern pyc_re{"(P?<dir>([^/]+/)*)__pycache__/(P?<module>\\w+)\\.\\w+-\\d+\\.pyc"} ;
		GatherDeps gather_deps   { New }                           ;
		::string   makefile_data = AdminDir + "/makefile_data.py"s ;
		Trace trace("_read_makefiles",ProcessDate::s_now()) ;
		//              vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv   // redirect stdout to stderr ...
		Status status = gather_deps.exec_child( { PYTHON , *g_lmake_dir+"/_lib/read_makefiles.py" , makefile_data } , Child::None/*stdin*/ , Fd::Stderr/*stdout*/ ) ; // as our stdout may be used ...
		//              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^   // communicate with client
		if (status!=Status::Ok) throw "cannot read makefiles"s ;
		//
		::string content = read_content(makefile_data) ;
		::vector_s deps ; deps.reserve(gather_deps.accesses.size()) ;
		for( auto const& [d,ai] : gather_deps.accesses ) {
			if (ai.write!=No) continue ;
			Py::Match m = pyc_re.match(d) ;
			if (+m) { ::string py = to_string(m["dir"],m["module"],".py") ; trace("dep",d,"->",py) ; deps.push_back(py) ; } // special case to manage pyc
			else    {                                                       trace("dep",d        ) ; deps.push_back(d ) ; }
		}
		trace("done",ProcessDate::s_now()) ;
		return { deps , content } ;
	}

	void Makefiles::s_refresh_makefiles() {
		Trace trace("s_refresh_makefiles") ;
		DiskDate latest_makefile ;
		if (_s_chk_makefiles(latest_makefile)) {
			SWEAR(g_config.lnk_support!=LnkSupport::Unknown) ;                 // ensure a config has been read
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			EngineStore::s_keep_makefiles() ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		} else {
			//                     vvvvvvvvvvvvvvvvv
			auto [deps,info_str] = _read_makefiles() ;
			//                     ^^^^^^^^^^^^^^^^^
			// we should write deps once makefiles info is correctly store as this implies that makefiles will not be read again unless they are modified
			// but because file date grantularity is a few ms, it is better to write this info as early as possible to have a better date check to detect modifications.
			// so we create a no_makefiles file that we unlink once everything is ok.
			do {
				OFStream no_makefiles_stream{s_no_makefiles} ;                        // create marker
				OFStream makefiles_stream   {s_makefiles   } ;
				for( ::string const& f : deps ) makefiles_stream << (+FileInfo(f)?'+':'!') << f <<'\n' ;
			} while (file_date(s_makefiles)<=latest_makefile) ;                                          // ensure date comparison with < (as opposed to <=) will lead correct result
			// update config
			::string             local_admin_dir  ;
			::string             remote_admin_dir ;
			ServerConfig         config           ;
			::umap<Crc,RuleData> rules            ;
			::vector_s           srcs             ;
			::string             step             ;
			try {
				PyObject* eval_env = PyDict_New() ;
				PyDict_SetItemString( eval_env , "inf"          , *Py::Float(Infinity) ) ;
				PyDict_SetItemString( eval_env , "nan"          , *Py::Float(nan("") ) ) ;
				PyDict_SetItemString( eval_env , "__builtins__" , PyEval_GetBuiltins() ) ;                                       // Python3.6 does not provide it for us
				Py::Dict info = Py::Object( PyRun_String(info_str.c_str(),Py_eval_input,eval_env,eval_env) , true/*clobber*/ ) ;
				Py_DECREF(eval_env) ;
				step = "local_admin_dir"  ; local_admin_dir  =                Py::String  (info[step  ])            ;
				step = "remote_admin_dir" ; remote_admin_dir =                Py::String  (info[step  ])            ;
				step = "config"           ; config           =                Py::Mapping (info[step  ])            ;
				step = "rules"            ; rules            = _gather_rules( Py::Sequence(info[step  ])          ) ;
				step = "sources"          ; srcs             = _gather_srcs ( Py::Sequence(info["srcs"]) , config ) ;
				{	::uset_s src_set = mk_uset(srcs) ;
					for( ::string const& f : deps ) if ( !src_set.contains(f) && +FileInfo(f) ) throw to_string("dangling makefile : ",f) ;
				}
			} catch(::string& e) {
				if (!step.empty()) e = to_string("while processing ",step," :\n",indent(e)) ;
				throw e ;
			} catch(Py::Exception& e) {
				if (!step.empty()) ::cerr<<"while processing "<<step<<" : "<<endl ;
				PyErr_Print() ;
				throw ""s ;
			}
			//           vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			EngineStore::s_new_makefiles( local_admin_dir , remote_admin_dir , ::move(config) , ::move(rules) , ::move(srcs) ) ;
			//           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			unlink(s_no_makefiles) ;                                                                                           // now that everything is ok, we can suppress marker file
		}
		Backend::s_config(g_config.backends) ;
	}

}
