// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "lmake_server/core.hh" // /!\ must be first to include Python.h first

#include "repo.hh"

using namespace Engine ;

static void _out( ::string const& jn , ::string const& r , ::string const& n ) {
	Fd::Stdout.write( widen(jn,13) +" : "+ widen(r,13) +" : "+ mk_printable(n) +'\n' ) ;
}

int main( int argc , char* /*argv*/[] ) {
	if (argc!=1) exit(Rc::Usage,"must be called without arg") ;
	repo_app_init() ;
	Py::init(*g_lmake_root_s) ;
	//
	try                       { Persistent::new_config({}/*config*/,false/*dyn*/) ; }
	catch (::string const& e) { exit(Rc::BadState,e) ;                              }
	//
	for( const Rule r : Persistent::rule_lst(true/*with_shared*/) )             _out( snake_str(r->special) , cat(r        ) , r->user_name() ) ;
	for( const Job  j : Persistent::job_lst (                   ) ) { j.chk() ; _out( cat(j)                , cat(j->rule()) , j->name()      ) ; }
	for( const Node n : Persistent::node_lst(                   ) ) {
		n.chk() ;
		if      ( n->buildable==Buildable::Unknown                                      ) _out( cat(n) , cat(n->actual_job,'?'           ) , n->name() ) ;
		else if ( n->has_job(false/*reliable*/)==Yes                                    ) _out( cat(n) , cat(n->actual_job               ) , n->name() ) ; // just dump existing info
		else if ( n->has_job(false/*reliable*/)==Maybe && n->rule_tgts.view().size()==1 ) _out( cat(n) , cat(n->rule_tgts.view()[0]->rule) , n->name() ) ; // .
		else                                                             _out( cat(n) , cat(n->buildable                ) , n->name() ) ;
	}
	//
	try                       { Persistent::chk() ; }
	catch (::string const& e) { exit(Rc::Fail,e)  ; }
}
