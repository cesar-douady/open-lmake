// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "lmakeserver/core.hh" // /!\ must be first to include Python.h first

#include "app.hh"

using namespace Engine ;

static void _out( ::string const& jn , ::string const& r , ::string const& n ) {
	Fd::Stdout.write( widen(jn,13) +" : "+ widen(r,13) +" : "+ mk_printable(n) +'\n' ) ;
}

int main( int argc , char* /*argv*/[] ) {
	if (argc!=1) exit(Rc::Usage,"must be called without arg") ;
	app_init(true/*read_only_ok*/) ;
	Py::init(*g_lmake_root_s) ;
	//
	try                       { Persistent::new_config({}/*config*/,false/*dyn*/) ; }
	catch (::string const& e) { exit(Rc::Format,e) ;                                }
	//
	for( const Rule r : Persistent::rule_lst(true/*with_shared*/) )             _out( cat(r->special) , cat(r        ) , r->full_name() ) ;
	for( const Job  j : Persistent::job_lst (                   ) ) { j.chk() ; _out( cat(j         ) , cat(j->rule()) , j->name()      ) ; }
	for( const Node n : Persistent::node_lst(                   ) ) {
		n.chk() ;
		switch (n->buildable) {
			case Buildable::DynAnti   :
			case Buildable::Anti      :
			case Buildable::SrcDir    :
			case Buildable::No        :
			case Buildable::SubSrcDir :
			case Buildable::Src       :
			case Buildable::Decode    :
			case Buildable::Encode    :
			case Buildable::SubSrc    :
			case Buildable::Loop      : _out( cat(n) , snake_str(n->buildable) , n->name() ) ; break ;
			case Buildable::Maybe     :
			case Buildable::Yes       :
			case Buildable::DynSrc    :
			case Buildable::Unknown   : _out( cat(n) , cat(n->actual_job())    , n->name() ) ; break ;
		}
	}
	//
	try                       { Persistent::chk() ; }
	catch (::string const& e) { exit(Rc::Fail,e)  ; }
}
