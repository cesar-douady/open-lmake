// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
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
	::ostream& operator<<( ::ostream& os , Enable const& e ) {
		return os<<"Enable("<<e.enable<<')' ;
	}
	size_t Enable::reply_len() const { return 1 ; } // just a bool
	Enable::Reply Enable::process(Record& r) const {
		bool res = r.enable ;
		if (enable!=Maybe) r.enable = enable==Yes ;
		return res ;
	}

	//
	// Solve
	//
	::ostream& operator<<( ::ostream& os , Solve const& s ) {
		/**/              os << "Solve(" << s.file ;
		if ( s.no_follow) os << ",no_follow"       ;
		if ( s.read     ) os << ",read"            ;
		if ( s.write    ) os << ",write"           ;
		if ( s.create   ) os << ",create"          ;
		if (+s.comment  ) os << ','<<s.comment     ;
		return            os << ')'                ;
	}
	::ostream& operator<<( ::ostream& os , Solve::Reply const& r ) {
		return os<<"Reply("<<r.real<<','<<r.file_info<<','<<r.file_loc<<','<<r.accesses<<')' ;
	}
	size_t Solve::reply_len() const { return PATH_MAX+100 ; } // 100 is plenty for overhead
	Solve::Reply Solve::process(Record& r) const {
		Reply         res ;
		Record::Solve s   { r , file , no_follow , read , create , comment } ;
		if ( read && write && +s.real0 ) throw comment+" : cannot read from "+s.real+" and write to "+s.real0 ;
		//
		if      (read ) { res.real     = ::move(s.real        ) ; res.file_info = FileInfo(r.s_root_fd(),res.real) ; }
		else if (write)   res.real     = ::move(s.real_write()) ;
		/**/              res.file_loc = s.file_loc             ;
		/**/              res.accesses = s.accesses             ;
		return res ;
	}

}
