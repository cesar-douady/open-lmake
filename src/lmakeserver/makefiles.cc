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
			RuleData rd  = Py::Dict(py_obj) ;
			Crc      crc = rd.match_crc     ;
			if (names.contains(rd.name)) {
				if ( rules.contains(crc) && rules.at(crc).name==rd.name ) throw to_string("rule " , rd.name , " appears twice"     ) ;
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

	static ::vector_s _gather_srcs( Py::Sequence const& py_srcs ) {
		RealPath   real_path { g_config.lnk_support } ;
		::vector_s srcs      ;
		for( Py::Object py_obj : py_srcs ) {
			::string src ;
			try                       { src = Py::String(py_obj) ; }
			catch(::Py::Exception& e) { throw e.errorValue() ;     }
			if (src.empty()) throw "found an empty source"s ;
			RealPath::SolveReport rp = real_path.solve(src,true/*no_follow*/) ;
			//
			if (!rp.lnks.empty()) throw to_string("source ",src," : found a link in its path : ",rp.lnks[0]) ;
			if (!rp.in_repo     ) throw to_string("source ",src," : not in reposiroty"                     ) ;
			if (rp.real!=src    ) throw to_string("source ",src," : canonical form is "         ,rp.real   ) ;
			if (g_config.lnk_support==LnkSupport::None) { if (!is_reg   (src)) throw to_string("source ",src," is not a regular file"           ) ; }
			else                                        { if (!is_target(src)) throw to_string("source ",src," is not a regular file nor a link") ; }
			srcs.emplace_back(src) ;
		}
		return srcs ;
	}

	::string/*reason to re-read*/ Makefiles::_s_chk_makefiles(DiskDate& latest_makefile) {
		DiskDate   makefiles_date   = file_date(s_makefiles) ;                             // ensure we gather correct date with NFS
		::ifstream makefiles_stream { s_makefiles }          ;
		Trace trace("_s_chk_makefiles",makefiles_date) ;
		if (is_reg(s_no_makefiles)) { trace("found"    ,s_no_makefiles) ; return "last makefiles read process was interrupted" ; } // s_no_makefile_file is a marker that says s_makefiles is invalid
		if (!makefiles_stream     ) { trace("not_found",s_makefiles   ) ; return "makefiles were never read"                   ; }
		char line[PATH_MAX+1] ;                                                                                                    // account for first char (+ or !)
		while (makefiles_stream.getline(line,sizeof(line))) {
			SWEAR( line[0]=='+' || line[0]=='!' ) ;
			bool         exists    = line[0]=='+' ;
			FileInfoDate file_info { line+1 } ;
			latest_makefile = ::max(latest_makefile,file_info.date) ;
			if (exists) {
				if ( !file_info                     ) { trace("missing" ,line+1) ; return to_string(line+1," was removed" ) ; }
				if ( file_info.date>=makefiles_date ) { trace("modified",line+1) ; return to_string(line+1," was modified") ; } // in case of equality, be pessimistic
			} else {
				if ( +file_info                     ) { trace("appeared",line+1) ; return to_string(line+1," was created" ) ; }
			}
		}
		trace("ok") ;
		return {} ;
	}

	static ::pair<vector_s/*deps*/,string/*stdout*/> _read_makefiles() {
		Py::Gil     gil           ;
		Py::Pattern pyc_re1       { "(?P<dir>(.*/)?)(?P<module>\\w+)\\.pyc"                         }   ;
		Py::Pattern pyc_re2       { "(?P<dir>(.*/)?)__pycache__/(?P<module>\\w+)\\.\\w+-\\d+\\.pyc" }   ;
		GatherDeps  gather_deps   { New }                                                               ;
		::string    makefile_data = AdminDir + "/makefile_data.py"s                                     ;
		::vector_s  cmd_line      = { PYTHON , *g_lmake_dir+"/_lib/read_makefiles.py" , makefile_data } ;
		Trace trace("_read_makefiles",ProcessDate::s_now()) ;
		gather_deps.autodep_env.src_dirs_s = {"/"} ;
		//              vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		Status status = gather_deps.exec_child( cmd_line , Child::None/*stdin*/ , Fd::Stderr/*stdout*/ ) ; // redirect stdout to stderr as our stdout may be used communicate with client
		//              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		if (status!=Status::Ok) throw "cannot read makefiles"s ;
		//
		::string content = read_content(makefile_data) ;
		::vector_s deps ; deps.reserve(gather_deps.accesses.size()) ;
		for( auto const& [d,ai] : gather_deps.accesses ) {
			if (!ai.info.idle()) continue ;
			Py::Match m = pyc_re1.match(d) ; if (!m) m = pyc_re2.match(d) ;
			if (+m) { ::string py = to_string(m["dir"],m["module"],".py") ; trace("dep",d,"->",py) ; deps.push_back(py) ; } // special case to manage pyc
			else    {                                                       trace("dep",d        ) ; deps.push_back(d ) ; }
		}
		trace("done",ProcessDate::s_now()) ;
		return { deps , content } ;
	}

	void Makefiles::s_refresh_makefiles(bool chk) {
		Trace trace("s_refresh_makefiles") ;
		DiskDate latest_makefile ;
		::string reason = _s_chk_makefiles(latest_makefile) ;
		if (reason.empty()) {
			SWEAR(g_config.lnk_support!=LnkSupport::Unknown) ;                 // ensure a config has been read
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			EngineStore::s_keep_config(chk) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		} else {
			::cerr << "read makefiles because " << reason << '\n' ;
			//                     vvvvvvvvvvvvvvvvv
			auto [deps,info_str] = _read_makefiles() ;
			//                     ^^^^^^^^^^^^^^^^^
			Py::Gil   gil      ;
			PyObject* eval_env = PyDict_New() ;
			PyDict_SetItemString( eval_env , "inf"          , *Py::Float(Infinity) ) ;
			PyDict_SetItemString( eval_env , "nan"          , *Py::Float(nan("") ) ) ;
			PyDict_SetItemString( eval_env , "__builtins__" , PyEval_GetBuiltins() ) ;           // Python3.6 does not provide it for us
			PyObject* py_info = PyRun_String(info_str.c_str(),Py_eval_input,eval_env,eval_env) ;
			Py_DECREF(eval_env) ;
			if (!py_info) exit( 2 , "error while reading makefile digest :\n" , Py::err_str() ) ;
			Py::Dict   info         = Py::Object( py_info , true/*clobber*/ ) ;
			::string   root_dir_s   = *g_root_dir+'/'                         ;
			// compile config early as we need it to generate the list of makefiles
			Config config ;
			try                      { config = Py::Mapping(info["config"]) ;                     }
			catch(::string const& e) { throw to_string("while processing config :\n",indent(e)) ; }
			::vmap_s<bool/*is_abs*/> glb_src_dirs_s ;                                               // source dirs outside repo in an absolute form
			::vector_s               lcl_src_dirs_s ;                                               // source dirs inside  repo
			for( ::string const& sd_s : config.src_dirs_s ) {
				if (is_lcl_s(sd_s)) lcl_src_dirs_s.push_back   (       sd_s                            ) ;
				else                glb_src_dirs_s.emplace_back(mk_abs(sd_s,*g_root_dir),is_abs_s(sd_s)) ;
			}
			// we should write deps once makefiles info is correctly stored as this implies that makefiles will not be read again unless they are modified
			// but because file date grantularity is a few ms, it is better to write this info as early as possible to have a better date check to detect modifications.
			// so we create a no_makefiles file that we unlink once everything is ok.
			do {
				OFStream no_makefiles_stream{s_no_makefiles} ;                 // create marker
				OFStream makefiles_stream   {s_makefiles   } ;
				auto gen_deps = [&](::string const& d)->void { makefiles_stream << (+FileInfo(d)?'+':'!') << d <<'\n' ; } ;
				for( ::string const& d : deps ) {
					SWEAR(!d.empty()) ;
					if (!is_abs(d)) {
						gen_deps(d) ;
						continue ;
					}
					for( auto const& [sd_s,ia] : glb_src_dirs_s )
						if ((d+'/').starts_with(sd_s)) {
							if (ia) gen_deps(       d            ) ;
							else    gen_deps(mk_rel(d,root_dir_s)) ;
							break ;
						}
				}
			} while (file_date(s_makefiles)<=latest_makefile) ;                // ensure date comparison with < (as opposed to <=) will lead correct result
			// update config
			::umap<Crc,RuleData> rules            ;
			::vector_s           srcs             ;
			::string             step             ;
			try {
				//           vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				EngineStore::s_new_config( ::move(config) , chk ) ;
				//           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				step = "rules"   ; rules = _gather_rules( Py::Sequence(info[step  ]) ) ;
				step = "sources" ; srcs  = _gather_srcs ( Py::Sequence(info["srcs"]) ) ;
				for( ::string const& f : srcs ) {
					::string f_s = f+'/' ;
					for ( ::string const& sd_s : lcl_src_dirs_s ) {
						if (!f_s.starts_with(sd_s)) continue ;
						if (f_s.size()==sd_s.size()) throw to_string(f," is both a source and a source dir"                                           ) ;
						else                         throw to_string("source ",f," is withing source dir ",sd_s=="/"?sd_s:sd_s.substr(0,sd_s.size()-1)) ;
					}
				}
				// check deps are not dangling
				{	::uset_s src_set = mk_uset(srcs) ;
					for( ::string const& f : deps ) {
						/**/                                          if (is_abs(f)          ) goto Continue ;
						/**/                                          if (src_set.contains(f)) goto Continue ;
						/**/                                          if (!FileInfo(f)       ) goto Continue ;
						for ( ::string const& sd_s : lcl_src_dirs_s ) if (f.starts_with(sd_s)) goto Continue ;
						throw to_string("dangling makefile : ",f) ;
					Continue : ;
					}
				}
			} catch(::string& e) {
				if (!step.empty()) e = to_string("while processing ",step," :\n",indent(e)) ;
				throw e ;
			}
			//           vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			EngineStore::s_new_makefiles( ::move(rules) , ::move(srcs) ) ;
			//           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			unlink(s_no_makefiles) ;                                           // now that everything is ok, we can suppress marker file
		}
	}

}
