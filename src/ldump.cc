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
	set_env("GMON_OUT_PREFIX","gmon.out.ldump") ; // in case profiling is used, ensure unique gmon.out
	//
	if (argc!=1) exit(Rc::Usage,"must be called without arg") ;
	app_init(true/*read_only_ok*/) ;
	Py::init(*g_lmake_dir_s) ;
	//
	try                       { Persistent::new_config({}/*config*/,false/*dynamic*/) ; }
	catch (::string const& e) { exit(Rc::Format,e) ;                                    }
	//
	for( const Rule r : Persistent::rule_lst() )             _out( {}            , fmt_string(r      ) , r->name   ) ;
	for( const Job  j : Persistent::job_lst () ) { j.chk() ; _out( fmt_string(j) , fmt_string(j->rule) , j->name() ) ; }
	for( const Node n : Persistent::node_lst() ) {
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
			case Buildable::Loop      : _out( fmt_string(n) , snake_str(n->buildable)     , n->name() ) ; break ;
			case Buildable::Maybe     :
			case Buildable::Yes       :
			case Buildable::DynSrc    :
			case Buildable::Unknown   : _out( fmt_string(n) , fmt_string(n->actual_job()) , n->name() ) ; break ;
		}
	}
	//
	Persistent::chk() ;
	//
	return 0 ;
}
