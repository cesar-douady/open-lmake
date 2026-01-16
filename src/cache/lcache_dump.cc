// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "py.hh" // /!\ must be included first as Python.h must be included first

#include "app.hh"
#include "hash.hh"
#include "process.hh"

#include "cache_utils.hh"
#include "engine.hh"
#include "rpc_cache.hh"

using namespace Cache ;
using namespace Disk  ;
using namespace Hash  ;

SmallIds<CacheUploadKey> _g_upload_keys  ;
::vector<DiskSz>         _g_reserved_szs ; // indexed by upload_key

int main( int argc , char** ) {
	if (argc!=1) exit(Rc::Usage,"must be called without arg") ;
	app_init({
		.cd_root      = false // launch at root
	,	.chk_version  = Yes
	,	.clean_msg    = cache_clean_msg()
	,	.read_only_ok = true
	,	.root_mrkrs   = { cat(AdminDirS,"config.py") }
	,	.version      = Version::Cache
	}) ;
	Py::init(*g_lmake_root_s) ;
	cache_init( false/*rescue*/ , true/*read_only*/ ) ;
	//
	Fd::Stdout.write(cat("total_sz : ",CrunData ::s_hdr().total_sz,'\n')) ;
	//
	Fd::Stdout.write("# id          :  ref_count : name\n") ;
	for( Ckey k : lst<Ckey>() )
		Fd::Stdout.write(cat( //!        width right
			/**/    widen(cat(k         ),13       )
		,	" : " , widen(cat(k->ref_cnt),10  ,true)
		,	" : " ,           k.str()
		,'\n')) ;
	//
	Fd::Stdout.write("# id          : n_statics n_runs : name\n") ;
	for( Cjob  j : lst<Cjob >() )
		Fd::Stdout.write(cat( //!          width right
			/**/    widen(cat(j           ),13       )
		,	" : " , widen(cat(j->n_statics), 9  ,true)
		,	' '   , widen(cat(j->n_runs   ), 6  ,true)
		,	" : " ,           j->name()
		,'\n')) ;
	//
	Fd::Stdout.write("# id          : job           : last_access          size   rate    n_deps(crc) : key\n") ;
	for( Crun  r : lst<Crun >() )
		Fd::Stdout.write(cat( //!    width
			/**/    widen(cat(r     ),13 )
		,	" : " , widen(cat(r->job),13 )
		,	" : " , r->last_access.str(0) //!                                                             width right
		,	' '   , widen(to_short_string_with_unit     (r->sz                                           ),5   ,true),"B"
		,	' '   , widen(to_short_string_with_unit<'m'>(uint64_t(from_rate(g_cache_config,r->rate)*1024)),5   ,true),"B/s"
		,	' '   , widen(cat                           (r->deps    .size()                              ),6   ,true)
		,	'('   , widen(cat                           (r->dep_crcs.size()                              ),3   ,true)
		,	')'
		,	" : " , cat(r->key,'-',"FL"[r->key_is_last])
		,'\n')) ;
	//
	Fd::Stdout.write("# id          :  ref_count : name\n") ;
	for( Cnode n : lst<Cnode>() )
		Fd::Stdout.write(cat( //!        width right
			/**/    widen(cat(n         ),13       )
		,	" : " , widen(cat(n->ref_cnt),10  ,true)
		,	" : " ,           n->name()
		,'\n')) ;
	//
	cache_finalize() ;
	//
	return 0 ;
}
