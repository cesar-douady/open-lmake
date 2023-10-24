// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "lmakeserver/core.hh"

using namespace Engine ;

static void _out( ::string const& jn , ::string const& r , ::string const& n ) {
	::string cn = mk_c_str(n) ;                                                                                 // ensure n is non-ambiguous
	::cout << ::setw(13)<<jn <<" : "<< ::setw(8)<<r  <<" : "<< ::string_view(cn).substr(1,cn.size()-2) <<'\n' ; // suppress useless " around n
}

int main( int argc , char* /*argv*/[] ) {
	//
	if (argc!=1) exit(2,"must be called without arg") ;
	app_init(true/*search_root*/,true/*cd_root*/) ;
	Py::init() ;
	//
	EngineStore::s_keep_config(false/*rescue*/) ;
	//
	for( const Rule r : g_store.rule_lst() ) _out( {}           , to_string(r                ) , r->name  ) ;
	for( const Job  j : g_store.job_lst () ) _out( to_string(j) , to_string(j->rule          ) , j.name() ) ;
	for( const Node n : g_store.node_lst() ) _out( to_string(n) , to_string(n->actual_job_tgt) , n.name() ) ;
	//
	g_store.chk() ;
	//
	return 0 ;
}
