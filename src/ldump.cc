// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "lmakeserver/core.hh"

using namespace Engine ;

static void _out( ::string const& jn , ::string const& r , ::string const& n ) {
	::cout << ::setw(13)<<jn <<" : "<< ::setw(20)<<r  <<" : "<< mk_printable(n) <<'\n' ; // suppress useless " around n
}

int main( int argc , char* /*argv*/[] ) {
	//
	if (argc!=1) exit(2,"must be called without arg") ;
	app_init(true/*search_root*/,true/*cd_root*/) ;
	Py::init() ;
	//
	Persistent::new_config({}/*config*/,false/*dynamic*/) ;
	//
	for( const Rule r : Persistent::rule_lst() ) _out( {}           , to_string(r                  ) , r->name   ) ;
	for( const Job  j : Persistent::job_lst () ) _out( to_string(j) , to_string(j->rule            ) , j->name() ) ;
	for( const Node n : Persistent::node_lst() ) _out( to_string(n) , to_string(n->actual_job_tgt()) , n->name() ) ;
	//
	Persistent::chk() ;
	//
	return 0 ;
}
