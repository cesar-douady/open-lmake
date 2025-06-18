// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "lmakeserver/core.hh" // /!\ must be first to include Python.h first

#include "app.hh"

using namespace Engine ;

int main( int argc , char* /*argv*/[] ) {
	//
	size_t n_rules[N<Special>   ] = {} ;
	size_t n_jobs [2/*has_rule*/] = {} ;
	size_t n_deps [N<Buildable> ] = {} ;
	size_t n_nodes[N<Buildable> ] = {} ;
	//
	if (argc!=1) exit(Rc::Usage,"must be called without arg") ;
	app_init(true/*read_only_ok*/) ;
	Py::init(*g_lmake_root_s) ;
	//
	try                       { Persistent::new_config({}/*config*/,false/*dyn*/) ; }
	catch (::string const& e) { exit(Rc::Format,e) ;                                }
	//
	for( const Rule r : Persistent::rule_lst(true/*with_shared*/) ) {
		n_rules[+r->special]++ ;
	}
	for( const Job j : Persistent::job_lst() ) {
		/**/                                            n_jobs[bool(+j->rule())]++ ;
		for ( [[maybe_unused]] Dep const& d : j->deps ) n_deps[+d->buildable   ]++ ;
	}
	for( const Node n : Persistent::node_lst() ) {
		n_nodes[+n->buildable]++ ;
	}
	//
	::vmap_s<size_t> out_tab ;
	for( Special   s : iota(All<Special  >) ) out_tab.emplace_back( cat("rules ",s                           ) , n_rules[+s] ) ;
	for( bool      r : {false,true}         ) out_tab.emplace_back( cat("jobs " ,(r?"with":"without")," rule") , n_jobs [r ] ) ;
	for( Buildable b : iota(All<Buildable>) ) out_tab.emplace_back( cat("nodes ",b                           ) , n_nodes[+b] ) ;
	for( Buildable b : iota(All<Buildable>) ) out_tab.emplace_back( cat("deps " ,b                           ) , n_deps [+b] ) ;
	//
	::string out ;
	size_t   wk  = 0 ;
	size_t   wv  = 0 ;
	for( auto const& [k,v] : out_tab ) if (v) wk = ::max(wk,k     .size()) ;
	for( auto const& [k,v] : out_tab ) if (v) wv = ::max(wv,cat(v).size()) ;
	for( auto const& [k,v] : out_tab ) if (v) out << widen(k,wk) <<" : "<< widen(cat(v),wv,true/*right*/) <<'\n' ;
	Fd::Stdout.write(out) ;
	//
	return 0 ;
}
