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

enum class Key : uint8_t { None } ;

enum class Flag : uint8_t {
	Verbose
} ;

int main( int argc , char* argv[]) {
	Syntax<Key,Flag> syntax {{
		{ Flag::Verbose , { .short_name='v' , .doc="more verbose output" } }
	}} ;
	CmdLine<Key,Flag> cmd_line { syntax , argc , argv } ;
	//
	if (  cmd_line.args.size()>1                                ) syntax.usage("cannot chdir to more than a single dir") ;
	if ( +cmd_line.args && ::chdir(cmd_line.args[0].c_str())!=0 ) syntax.usage(cat("cannot chdir to ",cmd_line.args[0])) ;
	//
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
	Fd::Stdout.write("\n# id          :  ref_count : name\n") ;
	for( Ckey k : lst<Ckey>() )
		Fd::Stdout.write(cat( //!        width right
			/**/    widen(cat(k         ),13       )
		,	" : " , widen(cat(k->ref_cnt),10  ,true)
		,	" : " ,           k.str()
		,'\n')) ;
	//
	Fd::Stdout.write("\n# id          : n_statics n_runs : name\n") ;
	for( Cjob  j : lst<Cjob>() )
		Fd::Stdout.write(cat( //!          width right
			/**/    widen(cat(j           ),13       )
		,	" : " , widen(cat(j->n_statics), 9  ,true)
		,	' '   , widen(cat(j->n_runs   ), 6  ,true)
		,	" : " ,           j->name()
		,'\n')) ;
	//
	Fd::Stdout.write("\n# id          : job           : last_access          size   rate    n_deps(crc) : key\n") ;
	for( Crun  r : lst<Crun>() ) {
		CrunData const& rd = *r ;
		Fd::Stdout.write(cat( //!    width
			/**/    widen(cat(r     ),13 )
		,	" : " , widen(cat(rd.job),13 )
		,	" : " , r->last_access.str(0) //!                                                             width right
		,	' '   , widen(to_short_string_with_unit     (rd.sz                                           ),5   ,true),"B"
		,	' '   , widen(to_short_string_with_unit<'m'>(uint64_t(from_rate(g_cache_config,rd.rate)*1024)),5   ,true),"B/s"
		,	' '   , widen(cat                           (rd.deps    .size()                              ),6   ,true)
		,	'('   , widen(cat                           (rd.dep_crcs.size()                              ),3   ,true)
		,	')'
		,	" : " , cat(rd.key,'-',"FL"[rd.key_is_last])
		,'\n')) ;
		if (cmd_line.flags[Flag::Verbose]) {
			::span deps = rd.deps    .view() ;
			::span crcs = rd.dep_crcs.view() ;
			for( size_t i : iota(            crcs.size()) ) Fd::Stdout.write(cat('\t',widen(cat(deps[i]),13)," :  ",crcs[i],'\n')) ;
			for( size_t i : iota(crcs.size(),deps.size()) ) Fd::Stdout.write(cat('\t',          deps[i]                    ,'\n')) ;
		}
	}
	//
	Fd::Stdout.write("\n# id          :  ref_count : name\n") ;
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
