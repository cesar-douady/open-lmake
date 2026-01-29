// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "lmake_server/core.hh" // /!\ must be first to include Python.h first

#include <iostream>

#include "app.hh"
#include "disk.hh"
#include "py.hh"
#include "rpc_job_exec.hh"

using namespace Codec  ;
using namespace Disk   ;
using namespace Engine ;
using namespace Hash   ;
using namespace Time   ;

enum class Key  : uint8_t { None } ;
enum class Flag : uint8_t {
	DryRun
,	Force
,	Reconstruct
} ;

enum class FileKind : uint8_t {
	Decode
,	Encode
} ;

struct CodecEntry {
	bool encoded = false ;
	Crc  crc     ;
} ;

struct DryRunDigest {
	::vmap_ss                    to_rm           ;     // map files to reasons
	::vmap_ss                    to_lnk          ;     // map lnks to targets
	::umap_s<umap_s<CodecEntry>> decode_tab      ;     // map ctx -> code->entry
	size_t                       n_ok            = 0 ;
	size_t                       n_reconstructed = 0 ;
	size_t                       n_decode_only   = 0 ;
	size_t                       n_encode_only   = 0 ;
	size_t                       n_inconsistent  = 0 ;
	size_t                       n_spurious      = 0 ;
} ;

static DryRunDigest _dry_run(bool from_decode) {
	Trace trace("_dry_run") ;
	DryRunDigest res ;
	//
	::string          admin_dir = cat("./",AdminDirS,rm_slash)                                                                                             ;
	::vmap_s<FileTag> files     = walk( Fd::Cwd , {FileTag::Reg,FileTag::Lnk} , {}/*pfx*/ , [&](::string const& f) { return f.starts_with(admin_dir) ; } ) ; ::sort(files) ;
	//
	for( auto& [file,_] : files ) {
		if (file.ends_with(EncodeSfx) ) continue ;                               // process in 2nd pass
		SWEAR(file[0]=='/') ; file = file.substr(1/* / */) ;
		try {
			if (!file.ends_with(DecodeSfx)) throw "unrecognized encode/decode suffix"s ;
			Crc crc = { New , AcFd( file , {.flags=O_RDONLY|O_NOFOLLOW} ).read() } ;
			file.resize(file.size()-(sizeof(DecodeSfx)-1/*null*/)) ;
			::string ctx_s                                  = dir_name_s(file) ;
			::string code                                   = base_name (file) ;
			res.decode_tab[::move(ctx_s)][::move(code)].crc = crc              ;
			file.clear() ;                                                       // ensure file is not recognized in 2nd pass as its suffix has been modified
		} catch (::string const& e) {
			res.n_spurious++ ;
			res.to_rm.emplace_back(file,e) ;
		}
	}
	for( auto& [file,_] : files ) {
		if (!file.ends_with(EncodeSfx) ) continue ;                              // was processed in 1st pass
		SWEAR(file[0]=='/') ; file = file.substr(1/* / */) ;
		try {
			res.n_spurious++ ;                                                   // until file has been qualified, it is spurious
			Crc      crc  ;
			::string code = read_lnk(file) ; if (!code                     ) throw "encode file is not a link"s ;
			/**/                             if (!code.ends_with(DecodeSfx)) throw "bad encode link"s           ;
			/**/                             if ( code.find('/')!=Npos     ) throw "bad encode link"s           ;
			file.resize(file.size()-(sizeof(EncodeSfx)-1/*null*/)) ;
			code.resize(code.size()-(sizeof(DecodeSfx)-1/*null*/)) ;
			try                     { crc = Crc::s_from_hex(base_name(file)) ; }
			catch (::string const&) { throw "bad encode link"s ;               }
			res.n_spurious-- ;                                                   // file has been qualified, not spurious any more
			//
			::string    ctx_s = dir_name_s(file)           ;
			auto        it1   = res.decode_tab.find(ctx_s) ; if (it1==res.decode_tab.end()) { res.n_encode_only ++ ; throw "no decode entry"s     ; }
			auto        it2   = it1->second   .find(code ) ; if (it2==it1->second   .end()) { res.n_encode_only ++ ; throw "no decode entry"s     ; }
			CodecEntry& entry = it2->second                ; if (entry.crc!=crc           ) { res.n_inconsistent++ ; throw "inconsistent encode"s ; }
			entry.encoded = true ;
		} catch (::string const& e) {
			res.to_rm.emplace_back(file,e) ;
		}
	}
	for( auto const& [ctx_s,ctx_tab] : res.decode_tab ) {
		::umap<Crc,::pair_s<bool/*encoded*/>> encode_tab ;                       // val crc -> (code,encoded)
		for( auto const& [code,entry] : ctx_tab ) {
			/**/         if (!entry.encoded) continue ;
			res.n_ok++ ; if (!from_decode  ) continue ;
			auto inserted = encode_tab.try_emplace(entry.crc,code,true/*encoded*/).second ;
			SWEAR( inserted , ctx_s,code,entry.crc ) ;
		}
		for( auto const& [code,entry] : ctx_tab ) {
			if (entry.encoded) continue ;
			if (!from_decode) {
				res.n_decode_only++ ;
				res.to_rm.emplace_back( cat(ctx_s,code,DecodeSfx) , "no encode entry" ) ;
				continue ;
			}
			auto it_inserted = encode_tab.try_emplace(entry.crc,code,false/*encoded*/) ;
			if (it_inserted.second) continue ;
			// manage conflict : keep best code
			::pair_s<bool/*encoded*/>& prev_code   = it_inserted.first->second ;
			::string                   crc_str     = entry.crc.hex()           ;
			bool                       better_code =
				//!     user_provided                        !encoded          size                  any stable order
				::tuple(crc_str.starts_with(code           ),true             ,code           .size(),code           )
			<	::tuple(crc_str.starts_with(prev_code.first),!prev_code.second,prev_code.first.size(),prev_code.first)
			;
			res.n_decode_only++ ;                                                // finally no new code
			if (better_code) {
				if (prev_code.second) res.to_rm.emplace_back( cat(ctx_s,entry.crc.hex(),EncodeSfx) , "conflict with "+code ) ;
				/**/                  res.to_rm.emplace_back( cat(ctx_s,prev_code.first,DecodeSfx) , "conflict with "+code ) ;
				prev_code = {code,false/*encoded*/} ;
			} else {
				res.to_rm.emplace_back( cat(ctx_s,code,DecodeSfx) , "conflict with "+prev_code.first ) ;
			}
		}
		for( auto const& [crc,code] : encode_tab ) {
			if (code.second) continue ;
			res.n_reconstructed++ ;
			res.to_lnk.emplace_back( cat(ctx_s,crc.hex(),EncodeSfx) , code.first+DecodeSfx ) ;
		}
	}
	return res ;
}

static ::string _codec_clean_msg() {
	::string cwd_s_ = Disk::cwd_s() ;
	return cat(
		"cfg=$(cat    "  ,cwd_s_,"LMAKE/config.py)",'\n'
	,	"rm -rf       "  ,cwd_s_,rm_slash          ,'\n'
	,	"mkdir -p     "  ,cwd_s_,"LMAKE"           ,'\n'
	,	"echo \"$cfg\" >",cwd_s_,"LMAKE/config.py"
	) ;
}

int main( int argc , char* argv[] ) {
	Syntax<Key,Flag> syntax {{
		{ Flag::DryRun      , { .short_name='n' , .doc="report actions but dont execute them" } }
	,	{ Flag::Force       , { .short_name='f' , .doc="execute actions without confirmation" } }
	,	{ Flag::Reconstruct , { .short_name='r' , .doc="reconstruct from decode files"        } }
	}} ;
	CmdLine<Key,Flag> cmd_line { syntax,argc,argv } ;
	if (cmd_line.args.size()<1) syntax.usage("must provide a cache dir to repair") ;
	if (cmd_line.args.size()>1) syntax.usage("cannot repair several cache dirs"  ) ;
	//
	if ( FileInfo(File(ServerMrkr)).exists() ) exit(Rc::BadState,"after having ensured no lcache_server is running, consider : rm ",ServerMrkr) ;
	//
	::string const& top_dir_s = with_slash(cmd_line.args[0]) ;
	if (::chdir(top_dir_s.c_str())!=0) exit( Rc::System  , "cannot chdir (",StrErr(),") to ",top_dir_s,rm_slash ) ;
	//
	app_init({
		.cd_root      = false // we have already chdir'ed to top
	,	.chk_version  = No
	,	.clean_msg    = _codec_clean_msg()
	,	.read_only_ok = cmd_line.flags[Flag::DryRun]
	,	.root_mrkrs   = { cat(AdminDirS,"config.py") }
	,	.version      = Version::Cache
	}) ;
	Py::init(*g_lmake_root_s) ;
	//
	//                 vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	DryRunDigest drd = _dry_run(cmd_line.flags[Flag::Reconstruct]) ;
	//                 ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	//
	size_t wf = ::max<size_t>( drd.to_rm  , [](::pair_ss const& f_r) { return mk_shell_str(f_r.first ).size() ; } ) ;
	size_t wt = ::max<size_t>( drd.to_lnk , [](::pair_ss const& l_t) { return mk_shell_str(l_t.second).size() ; } ) ;
	for( auto const& [file,reason] : drd.to_rm  ) Fd::Stdout.write(cat("rm "   ,widen(mk_shell_str(file  ),wf)," # ",reason           ,'\n')) ;
	if ( +drd.to_rm && +drd.to_lnk )              Fd::Stdout.write(                                                                    "\n" ) ;
	for( auto const& [lnk ,target] : drd.to_lnk ) Fd::Stdout.write(cat("ln -s ",widen(mk_shell_str(target),wt),' '  ,mk_shell_str(lnk),'\n')) ;
	::vmap_s<size_t> summary ;
	if (drd.n_spurious     ) summary.emplace_back( "unrecognized"              , drd.n_spurious      ) ;
	if (drd.n_encode_only  ) summary.emplace_back( "encode only"               , drd.n_encode_only   ) ;
	if (drd.n_inconsistent ) summary.emplace_back( "inconsistent"              , drd.n_inconsistent  ) ;
	if (drd.n_reconstructed) summary.emplace_back( "reconstructed from decode" , drd.n_reconstructed ) ;
	if (drd.n_decode_only  ) summary.emplace_back( "decode only"               , drd.n_decode_only   ) ;
	if (drd.n_ok           ) summary.emplace_back( "correct code<->val"        , drd.n_ok            ) ;
	size_t wk = ::max<size_t>( summary , [](::pair_s<size_t> const& k_v) { return     k_v.first  .size() ; } ) ;
	size_t wv = ::max<size_t>( summary , [](::pair_s<size_t> const& k_v) { return cat(k_v.second).size() ; } ) ;
	if ( +summary )                    Fd::Stdout.write(                                                     "\n" ) ;
	for( auto const& [k,v] : summary ) Fd::Stdout.write(cat(widen(k,wk)," : ",widen(cat(v),wv,true/*right*/),'\n')) ;
	//
	if ( cmd_line.flags[Flag::DryRun]) exit(Rc::Ok) ;
	if (!cmd_line.flags[Flag::Force ])
		for(;;) {
			::string user_reply ;
			std::cout << "continue [y/n] ? "      ;
			std::getline( std::cin , user_reply ) ;
			if (user_reply=="n") exit(Rc::Ok) ;
			if (user_reply=="y") break        ;
		}
	CodecServerSide config { ""/*root_dir_s*/ } ;
	//
	for( auto const& [file,reason] : drd.to_rm  ) unlnk  ( file ,          {.dir_ok=is_dir_name(file),.abs_ok=true} ) ;
	for( auto const& [lnk ,target] : drd.to_lnk ) sym_lnk( lnk  , target , {.perm_ext=config.perm_ext}              ) ;
	//
	exit(Rc::Ok) ;
}
