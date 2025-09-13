// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "backdoor.hh"

namespace Backdoor {

	::umap_s<Func> const& get_func_tab() {
		static ::umap_s<Func> s_tab = {
			{ Enable::Cmd , func<Enable> }
		,	{ Solve ::Cmd , func<Solve > }
		} ;
		return s_tab ;
	}

	//
	// Enable
	//

	::string& operator+=( ::string& os , Enable const& e ) { // START_OF_NO_COV
		return os<<"Enable("<<e.enable<<')' ;
	}                                                        // END_OF_NO_COV

	Enable::Reply Enable::process(Record& r) const {
		bool res = r.enable ;
		if (enable!=Maybe) {
			r.enable = enable==Yes ;
			Record::s_enable_was_modified = true ; // if autodep=ptrace, managing enable is quite expansive and is only done if enable was manipulated, so it must be aware
		}
		return res ;
	}

	::string Enable::descr() const {
		switch (enable) {
			case No    : return "disable autodep"   ;
			case Yes   : return "enable autodep"    ;
			case Maybe : return "get autodep state" ;
		DF}
	}

	//
	// Solve
	//
	::string& operator+=( ::string& os , Solve const& s ) { // START_OF_NO_COV
		/**/              os << "Solve(" << s.file ;
		if ( s.no_follow) os << ",no_follow"       ;
		if ( s.read     ) os << ",read"            ;
		if ( s.write    ) os << ",write"           ;
		if ( s.create   ) os << ",create"          ;
		if (+s.comment  ) os << ','<<s.comment     ;
		return            os << ')'                ;
	}                                                       // END_OF_NO_COV

	Solve::Reply Solve::process(Record& r) const {
		Solve::Reply res { r , file , no_follow , read , create , comment } ;
		res.file_info = FileInfo(r.s_repo_root_fd(),res.real) ;               // file info must be probed in process as we are protected against recording
		return res ;
	}

	::string Solve::descr() const {
		return cat("solve ",file) ;
	}

}
