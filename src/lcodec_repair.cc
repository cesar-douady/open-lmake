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
	bool     encoded = false ;
	bool     is_reg  = false ; // true if decode entry was seen as a regular file
	CodecCrc crc     ;
} ;

struct StoreEntry {
	bool used   = false ; // true if referenced by a node
	bool exists = false ; // true if it physically exists in store
} ;

struct DryRunDigest {
	::vmap_ss to_rm           ; // map files to reasons
	::vmap_ss to_lnk          ; // map lnks  to targets
	::vmap_ss to_rename       ; // map dsts  to srcs
	::set_s   to_rmdir_s      ;
	size_t    n_ok            = 0 ;
	size_t    n_reconstructed = 0 ;
	size_t    n_decode_only   = 0 ;
	size_t    n_encode_only   = 0 ;
	size_t    n_inconsistent  = 0 ;
	size_t    n_spurious      = 0 ;
} ;

static DryRunDigest _dry_run(bool from_decode) {
	Trace trace("_dry_run") ;
	DryRunDigest res ;
	//
	::string                     admin_dir  = cat("./",AdminDirS,rm_slash) ;
	::umap<CodecCrc,StoreEntry>  store      ;
	::umap<CodecCrc,::string>    new_store  ;                                                                                                   // a node that can provide data for the store
	::umap_s<umap_s<CodecEntry>> decode_tab ;                                                                                                   // map ctx -> code->entry
	//
	static constexpr ::string_view AdminDir = { AdminDirS , sizeof(AdminDirS)-2/* /null*/ } ;
	for( ::string& file : lst_dir_s() ) {
		if (file==AdminDir) continue ;
		if (file=="store" ) continue ;
		if (file=="tab"   ) continue ;
		res.to_rm.emplace_back( ::move(file) , "unexpected top-level" ) ;
	}
	for( ::string& file : lst_dir_s( ::string(AdminDirS) ) ) {
		switch (file[0]) {
			case 'f' : if (file=="file_sync") continue ; break ;
			case 'v' : if (file=="version"  ) continue ; break ;
		DN}
		res.to_rm.emplace_back( cat(AdminDirS,file) , "in admin dir" ) ;
	}
	for( ::string const& crc_str : lst_dir_s( "store/"s ) ) {
		::string file = "store/"+crc_str ;
		CodecCrc crc  ;
		try                                     { crc = CodecCrc::s_from_hex(crc_str) ;                                      }
		catch (::string const& e)               { res.to_rm.emplace_back( ::move(file) , cat("bad name : ",e) ) ; continue ; }
		if (FileInfo(file).tag()!=FileTag::Reg) { res.to_rm.emplace_back( ::move(file) , "not a regular file" ) ; continue ; }
		//
		bool inserted = store.try_emplace( crc , StoreEntry({.exists=true}) ).second ; SWEAR( inserted , crc ) ;
	}
	::string           here_s    = cwd_s()                         ;
	::vmap_s<FileTag>  files     = walk("tab/"s,~FileTags(),"tab") ; ::sort(files) ;
	//
	// dirs
	//
	for( auto const& [file,tag] : files )
		if (tag==FileTag::Dir)
			for( ::string d=with_slash(file) ; +d ; d=dir_name_s(d) )
				if (!res.to_rmdir_s.insert(d).second) break ;
	//
	// decode side
	//
	for( auto const& [file,tag] : files ) {
		if (tag==FileTag::Dir        ) continue ;                                                                                               // already processed
		if (file.ends_with(EncodeSfx)) continue ;                                                                                               // process in encode pass
		try {
			throw_unless( file.ends_with(DecodeSfx) , "unrecognized suffix" ) ;
			CodecFile codec_file { New , mk_glb(file,here_s) , here_s } ;
			CodecCrc  crc        ;
			switch (tag) {
				case FileTag::Lnk : {
					::string  rel_target = read_lnk(file)                          ; throw_unless( +rel_target                  , "cannot read sym link"      ) ;
					::string  target     = mk_glb( rel_target , dir_name_s(file) ) ; throw_unless( target.starts_with("store/") , "bad sym link not to store" ) ;
					//
					try                       { crc = CodecCrc::s_from_hex(target.substr(6/*store/ */)) ; }
					catch (::string const& e) { throw cat("bad sym link is not a checksum : ",e) ;        }
					throw_unless( store.contains(crc) , "decode checksum not in store" ) ;
				} break ;
				case FileTag::Reg : {
					crc = { New , AcFd(file).read() } ;
					if (!new_store.try_emplace(crc,file).second) res.to_rm.emplace_back( file , "duplicate regular file" ) ;
					decode_tab[codec_file.ctx][codec_file.code()].is_reg = true ;
				} break ;
				default :
					res.to_rm.emplace_back( file , "not a sym link nor a regular file" ) ;
					continue ;
			}
			//
			decode_tab[::move(codec_file.ctx)][::move(codec_file.code())].crc = crc ;
		} catch (::string const& e) {
			res.n_spurious++ ;
			res.to_rm.emplace_back(file,e) ;
		}
	}
	//
	// encode side
	//
	for( auto const& [file,tag] : files ) {
		if (tag==FileTag::Dir         )                                                       continue ;                                        // already processed in decode pass
		if (!file.ends_with(EncodeSfx))                                                       continue ;                                        // .
		if (tag!=FileTag::Lnk         ) { res.to_rm.emplace_back( file , "not a sym link" ) ; continue ; }
		try {
			res.n_spurious++ ;                                                                                                                  // until file has been qualified, it is spurious
			CodecFile codec_file { New , mk_glb(file,here_s) , here_s } ;
			::string  code       = read_lnk(file)                       ; throw_unless( +code                      , "cannot read sym link" ) ;
			/**/                                                          throw_unless(  code.ends_with(DecodeSfx) , "bad encode link"      ) ;
			code.resize(code.size()-DecodeSfxSz) ;                        throw_unless(  code.find('/')==Npos      , "bad encode link"      ) ;
			res.n_spurious-- ;                                                                                                                  // file has been qualified, not spurious any more
			//
			auto        it1   = decode_tab .find(codec_file.ctx) ; if (it1==decode_tab .end()         ) { res.n_encode_only ++ ; throw "no decode entry"s     ; }
			auto        it2   = it1->second.find(code          ) ; if (it2==it1->second.end()         ) { res.n_encode_only ++ ; throw "no decode entry"s     ; }
			CodecEntry& entry = it2->second                      ; if (entry.crc!=codec_file.val_crc()) { res.n_inconsistent++ ; throw "inconsistent encode"s ; }
			entry.encoded = true ;
		} catch (::string const& e) {
			res.to_rm.emplace_back(file,e) ;
		}
	}
	//
	// synthesis
	//
	auto use_node = [&]( ::string const& ctx , ::string const& code , CodecEntry const& entry ) {
		::string ctx_s = cat("tab/",ctx,'/') ;
		if (entry.is_reg) res.to_lnk.emplace_back( cat(ctx_s,code,DecodeSfx) , mk_rel("store/"+entry.crc.hex(),ctx_s) ) ; // node is moved to store or unlinked, so we must recreate it ...
		store[entry.crc].used = true ;                                                                                    // ... as a link to store
		for( ::string d = ctx_s ; +d ; d=dir_name_s(d) )
			if (!res.to_rmdir_s.erase(d)) break ;
	} ;
	for( auto const& [ctx,ctx_tab] : decode_tab ) {
		::umap<CodecCrc,::pair_s<bool/*encoded*/>> encode_tab ;                                                           // val crc -> (code,encoded)
		for( auto const& [code,entry] : ctx_tab ) {
			if (!entry.encoded) continue ;
			use_node( ctx , code , entry ) ;
			res.n_ok++ ;
			if (!from_decode) continue ;
			auto inserted = encode_tab.try_emplace(entry.crc,code,true/*encoded*/).second ;
			SWEAR( inserted , ctx,code,entry.crc ) ;
		}
		for( auto const& [code,entry] : ctx_tab ) {
			if (entry.encoded) continue ;
			if (!from_decode) {
				res.n_decode_only++ ;
				res.to_rm.emplace_back( cat(ctx,'/',code,DecodeSfx) , "no encode entry" ) ;
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
			res.n_decode_only++ ;                                                                                         // finally no new code
			if (better_code) {
				if (prev_code.second) res.to_rm.emplace_back( cat(ctx,'/',entry.crc.hex(),EncodeSfx) , "conflict with "+code ) ;
				/**/                  res.to_rm.emplace_back( cat(ctx,'/',prev_code.first,DecodeSfx) , "conflict with "+code ) ;
				prev_code = {code,false/*encoded*/} ;
			} else {
				res.to_rm.emplace_back( cat(ctx,code,DecodeSfx) , "conflict with "+prev_code.first ) ;
			}
		}
		for( auto const& [crc,code_encoded] : encode_tab ) {
			if (code_encoded.second) continue ;
			::string const& code = code_encoded.first ;
			use_node( ctx , code , ctx_tab.at(code) ) ;
			res.n_reconstructed++ ;
			res.to_lnk.emplace_back( cat("tab/",ctx,'/',crc.hex(),EncodeSfx) , code+DecodeSfx ) ;
		}
		for( auto [crc,entry] : store ) {
			bool is_reg = new_store.contains(crc) ;
			if ( !is_reg && !entry.used ) res.to_rm    .emplace_back(                     "store/"+crc.hex() , "unused"         ) ;
			if (  is_reg &&  entry.used ) res.to_rename.emplace_back( new_store.at(crc) , "store/"+crc.hex()                    ) ;
			if (  is_reg && !entry.used ) res.to_rm    .emplace_back( new_store.at(crc) ,                      "unused regular" ) ;
		}
	}
	return res ;
}

static ::string _codec_clean_msg() {
	::string cwd_s_ = Disk::cwd_s() ;
	return cat(
		"file_sync=$(cat ",cwd_s_,AdminDirS,"file_sync)",'\n'
	,	"rm -rf          ",cwd_s_,rm_slash              ,'\n'
	,	"mkdir -p        ",cwd_s_,AdminDirS,rm_slash    ,'\n'
	,	"echo \"$cfg\"  >",cwd_s_,AdminDirS,"file_sync"
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
	::string const& top_dir_s = with_slash(cmd_line.args[0]) ;
	if (::chdir(top_dir_s.c_str())!=0) exit( Rc::System  , "cannot chdir (",StrErr(),") to ",top_dir_s,rm_slash ) ;
	//
	app_init({
		.cd_root      = false                                                                                          // we have already chdir'ed to top
	,	.chk_version  = Yes
	,	.clean_msg    = _codec_clean_msg()
	,	.read_only_ok = cmd_line.flags[Flag::DryRun]
	,	.root_mrkrs   = { cat(AdminDirS,"file_sync") }
	,	.version      = Version::Codec
	}) ;
	Py::init(*g_lmake_root_s) ;
	//
	//                 vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	DryRunDigest drd = _dry_run(cmd_line.flags[Flag::Reconstruct]) ;
	//                 ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	::vmap_s<size_t> summary ;
	if (drd.n_spurious     ) summary.emplace_back( "unrecognized"              , drd.n_spurious      ) ;
	if (drd.n_encode_only  ) summary.emplace_back( "encode only"               , drd.n_encode_only   ) ;
	if (drd.n_inconsistent ) summary.emplace_back( "inconsistent"              , drd.n_inconsistent  ) ;
	if (drd.n_reconstructed) summary.emplace_back( "reconstructed from decode" , drd.n_reconstructed ) ;
	if (drd.n_decode_only  ) summary.emplace_back( "decode only"               , drd.n_decode_only   ) ;
	if (drd.n_ok           ) summary.emplace_back( "correct code<->val"        , drd.n_ok            ) ;
	//
	#define W(s) Fd::Stdout.write(s)
	#define S(s) mk_shell_str    (s)
	size_t w1 = ::max<size_t>( drd.to_rm     , [](::pair_ss        const& f_r) { return S(f_r.first )  .size() ; } ) ;
	size_t w2 = ::max<size_t>( drd.to_rename , [](::pair_ss        const& s_d) { return S(s_d.first )  .size() ; } ) ;
	size_t w3 = ::max<size_t>( drd.to_lnk    , [](::pair_ss        const& l_t) { return S(l_t.second)  .size() ; } ) ;
	size_t w4 = ::max<size_t>( summary       , [](::pair_s<size_t> const& k_v) { return     k_v.first  .size() ; } ) ;
	size_t w5 = ::max<size_t>( summary       , [](::pair_s<size_t> const& k_v) { return cat(k_v.second).size() ; } ) ;
	bool   nl = false                                                                                                ; // generate new line between categories
	for( auto     const& [file,reason] : drd.to_rm      ) { W(cat("rm "   ,widen(S(file),w1)," # ",reason         ,'\n')) ; nl=true ; } if ( nl && +drd.to_rename  ) { W("\n") ; nl=false ; }
	for( auto     const& [src ,dst   ] : drd.to_rename  ) { W(cat("mv "   ,widen(S(src ),w2),' '  ,S(dst)         ,'\n')) ; nl=true ; } if ( nl && +drd.to_lnk     ) { W("\n") ; nl=false ; }
	for( auto     const& [lnk ,tgt   ] : drd.to_lnk     ) { W(cat("ln -s ",widen(S(tgt ),w3),' '  ,S(lnk)         ,'\n')) ; nl=true ; } if ( nl && +drd.to_rmdir_s ) { W("\n") ; nl=false ; }
	for( ::string const&  dir_s        : drd.to_rmdir_s ) { W(cat("rmdir ",S(no_slash(dir_s))                     ,'\n')) ; nl=true ; } if ( nl && +summary        ) { W("\n") ; nl=false ; }
	for( auto     const& [k   ,v     ] : summary        ) { W(cat(widen(k,w4)," : ",widen(cat(v),w5,true/*right*/),'\n')) ; nl=true ; }
	#undef S
	#undef W
	//
	if ( cmd_line.flags[Flag::DryRun]) exit(Rc::Ok) ;
	if (!cmd_line.flags[Flag::Force ]) {
		if (nl) Fd::Stdout.write("\n") ;
		for(;;) {
			::string user_reply ;
			std::cout << "continue [y/n] ? "      ;
			std::getline( std::cin , user_reply ) ;
			if (user_reply=="n") exit(Rc::Ok) ;
			if (user_reply=="y") break        ;
		}
	}
	CodecRemoteSide config ;
	try                       { config = { New , ""/*root_dir_s*/ } ; }
	catch (::string const& e) { exit(Rc::BadMakefile,e) ;             }
	//
	for( auto const& [file,reason] : drd.to_rm                              ) unlnk  ( file ,          {.abs_ok=true,.dir_ok=is_dir_name(file)} ) ;
	for( auto const& [src ,dst   ] : drd.to_rename                          ) rename ( src  , dst                                               ) ;
	for( auto const& [lnk ,target] : drd.to_lnk                             ) sym_lnk( lnk  , target , {.umask=config.umask}                    ) ;
	for( auto it=drd.to_rmdir_s.rbegin() ; it!=drd.to_rmdir_s.rend() ; it++ ) ::rmdir( it->c_str()                                              ) ;
	//
	exit(Rc::Ok) ;
}
