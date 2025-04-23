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

	size_t Enable::reply_len() const { return 1 ; } // just a bool

	Enable::Reply Enable::process(Record& r) const {
		bool res = r.enable ;
		if (enable!=Maybe) r.enable = enable==Yes ;
		return res ;
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

	size_t Solve::reply_len() const { return JobExecRpcReq::MaxSz ; } // 100 is plenty for overhead

	Solve::Reply Solve::process(Record& r) const {
		Solve::Reply res { r , file , no_follow , read , create , comment } ;
		res.file_info = FileInfo(r.s_repo_root_fd(),res.real) ;               // file info must be probed in process as we are protected against recording
		return res ;
	}

}
