// This file is part of the lmake distribution (https://wgit.doliam.net/cdy/lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "lmakeserver/core.hh"

using namespace Engine ;

static void _out( ::string const& jn , ::string const& r , ::string const& n ) {
	::cout << ::setw(13)<<jn <<" : "<< ::setw(8)<<r  <<" : "<< n <<'\n' ;
}

int main( int argc , char* /*argv*/[] ) {
	//
	if (argc!=1) exit(2,"must be called without arg") ;
	app_init(true/*search_root*/,true/*cd_root*/) ;
	::cout << left ;
	//
	EngineStore::s_keep_makefiles() ;
	//
	for( const Rule r : g_store.rule_lst() ) _out( {}           , to_string(r      ) , r->name       ) ;
	for( const Job  j : g_store.job_lst () ) _out( to_string(j) , to_string(j->rule) , j.user_name() ) ;
	for( const Node n : g_store.node_lst() ) _out( to_string(n) , n.shared()?"!":""  , n.name     () ) ;
	//
	return 0 ;
}
