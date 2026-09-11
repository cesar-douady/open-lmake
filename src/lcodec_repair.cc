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

enum class Flag : uint8_t {
	DryRun
,	Force
,	Reconstruct
} ;

struct CodecEntry {
	bool     encoded = false ;
	bool     is_reg  = false ; // true if decode entry was seen as a regular file
	CodecCrc crc     ;
} ;

struct DryRunDigest {
	::vmap_ss to_rm           ; // map files to reasons
	::vmap_ss to_store        ; // map regular decode files to store files : content is copied to store, then file is replaced by a link (cf. to_lnk)
	::vmap_ss to_lnk          ; // map lnks  to targets, links are created atomically (replacing any existing file)
	::set_s   to_rmdir_s      ;
	size_t    n_ok            = 0 ;
	size_t    n_reconstructed = 0 ;
	size_t    n_decode_only   = 0 ;
	size_t    n_encode_only   = 0 ;
	size_t    n_inconsistent  = 0 ;
	size_t    n_spurious      = 0 ;
} ;

static ::string _store_dir_s(CodecCrc crc) { ::string crc_base64 = crc.base64() ; return cat("store/",substr_view(crc_base64,0,2),'/'                          ) ; }
static ::string _store_file (CodecCrc crc) { ::string crc_base64 = crc.base64() ; return cat("store/",substr_view(crc_base64,0,2),'/',substr_view(crc_base64,2)) ; }
static DryRunDigest _dry_run(bool from_decode) {
	Trace trace("_dry_run") ;
	DryRunDigest res ;
	//
	::string                      admin_dir    = cat("./",AdminDirS,rm_slash) ;
	::umap<CodecCrc,bool/*used*/> store        ;                                                                                              // existing store files
	::vector_s                    store_dirs_s ;                                                                                              // existing (well formed) store dirs
	::umap_s<umap_s<CodecEntry>>  decode_tab   ;                                                                                              // map ctx -> code_name->entry
	//
	static constexpr ::string_view AdminDir = { AdminDirS , sizeof(AdminDirS)-2/* /null*/ } ;
	//
	for( ::string const& d : {"store"s,"tab"s} ) {
		FileTag tag = FileInfo(d).tag() ;
		throw_unless( tag==FileTag::Dir||tag==FileTag::None , d," must be a dir" ) ;                                                          // do not rm a possible sym link to actual data
	}
	//
	for( ::string& file : lst_dir_s() ) {
		if (file==AdminDir) continue ;
		if (file=="store" ) continue ;
		if (file=="tab"   ) continue ;
		if (file=="stamp" ) continue ;
		//
		if (FileInfo(file).tag()==FileTag::Dir) add_slash(file) ;
		res.to_rm.emplace_back( ::move(file) , "unexpected top-level" ) ;
	}
	for( ::string& file : lst_dir_s( ::string(AdminDirS) ) ) {
		switch (file[0]) {
			case 'f' : if (file=="file_sync") continue ; break ;
			case 'v' : if (file=="version"  ) continue ; break ;
		DN}
		::string f = cat(AdminDirS,file) ;
		if (FileInfo(f).tag()==FileTag::Dir) add_slash(f) ;
		res.to_rm.emplace_back( ::move(f) , "in admin dir" ) ;
	}
	//
	if (FileInfo("store"s).tag()==FileTag::Dir)
		for( ::string const& pfx : lst_dir_s( "store/"s ) ) {
			::string dir_s = "store/"+pfx+'/'      ;
			FileTag  tag   = FileInfo(dir_s).tag() ;
			if ( pfx.size()!=2 || tag!=FileTag::Dir ) {
				if (tag!=FileTag::Dir) rm_slash(dir_s) ;
				res.to_rm.emplace_back( ::move(dir_s) , "bad name for first level" ) ;
				continue ;
			}
			for( ::string const& crc_sfx : lst_dir_s(dir_s) ) {
				::string file       = dir_s+crc_sfx        ;
				::string crc_base64 = pfx  +crc_sfx        ;
				FileTag  tag        = FileInfo(file).tag() ;
				CodecCrc crc        ;
				switch (tag) {
					case FileTag::None  :                   res.to_rm.emplace_back( ::move(file) , "cannot be stat'ed"  ) ; continue ;
					case FileTag::Dir   : add_slash(file) ; res.to_rm.emplace_back( ::move(file) , "is a dir"           ) ; continue ;
					case FileTag::Lnk   :                   res.to_rm.emplace_back( ::move(file) , "is a symbolic link" ) ; continue ;
					case FileTag::Reg   :
					case FileTag::Empty :                                                                                   break    ;
					case FileTag::Exe   :                   res.to_rm.emplace_back( ::move(file) , "is executable"      ) ; continue ;
				}
				try                       {                                          crc = CodecCrc::s_from_base64(crc_base64) ;                                }
				catch (::string const& e) {                                          res.to_rm.emplace_back( ::move(file) , cat("bad name : ",e) ) ; continue ; }
				//
				bool inserted = store.try_emplace( crc , false/*used*/ ).second ; SWEAR( inserted , crc ) ;
			}
			store_dirs_s.push_back(::move(dir_s)) ;
		}
	//
	::vmap_s<FileTag> files ;
	if (FileInfo("tab"s).tag()==FileTag::Dir) {
		files = walk( "tab/"s , ~FileTags() , "tab" , [](::string const& d) { return d.ends_with(DecodeSfx) || d.ends_with(EncodeSfx) ; } ) ; // do not walk in spurious dirs
		::sort(files) ;
	}
	//
	// dirs
	//
	for( auto const& [file,tag] : files ) {
		if (tag!=FileTag::Dir) continue ;
		if ( file.ends_with(DecodeSfx) || file.ends_with(EncodeSfx) ) {                                       // cannot be a context (cf. CodecFile::chk), and content has not been walked
			res.n_spurious++ ;
			res.to_rm.emplace_back( with_slash(file) , "dir with a codec suffix" ) ;
			continue ;
		}
		for( ::string d_s=with_slash(file) ; d_s!="tab/" ; d_s=dir_name_s(d_s) )                              // tab/ itself is never removed
			if (!res.to_rmdir_s.insert(d_s).second) break ;
	}
	//
	// decode side
	//
	for( auto const& [file,tag] : files ) {
		if (tag==FileTag::Dir        ) continue ;                                                             // already processed
		if (file.ends_with(EncodeSfx)) continue ;                                                             // process in encode pass
		size_t   slash = file.rfind('/')                                  ; SWEAR( slash!=Npos , file ) ;
		::string ctx   = slash>4/*tab/ */ ? file.substr(4,slash-4) : ""s  ;                                   // empty for files directly in tab/, such files are spurious
		::string code  = file.substr( slash+1 )                           ;
		try {
			throw_unless( +ctx                      , "no context"          ) ;
			throw_unless( code.ends_with(DecodeSfx) , "unrecognized suffix" ) ;
			code.resize(code.size()-DecodeSfxSz) ;
			// codes are handled through their on-disk name, i.e. mk_printable<'/'>(code), as this is the way jobs address them (cf. CodecFile::name)
			// files whose name do not round trip through parse_printable/mk_printable (e.g. with non-printable chars) can never be reached by any code and are spurious
			throw_unless( mk_printable<'/'>(parse_printable<'/'>(code))==code, "bad code name" ) ;
			CodecCrc crc    ;
			bool     is_reg = false ;
			switch (tag) {
				case FileTag::Lnk : {
					::string  rel_target = read_lnk(file)                                    ; throw_unless( +rel_target                                         , "cannot read sym link"      ) ;
					::string  target     = mk_glb( rel_target , dir_name_s(file) )           ; throw_unless( target.starts_with("store/")                        , "bad sym link not to store" ) ;
					                                                                           throw_unless( target.size()==9/*store/XX/ */+CodecCrc::Base64Sz-2 , "bad sym link format"       ) ;
					                                                                           throw_unless( target[8/*store/XX*/=='/']                          , "bad sym link format"       ) ;
					::string  crc_base64 = target.substr(6/*store*/,2)+substr_view(target,9) ;
					//
					try                       { crc = CodecCrc::s_from_base64(crc_base64) ;        }
					catch (::string const& e) { throw cat("bad sym link is not a checksum : ",e) ; }
					throw_unless( store.contains(crc) , "decode checksum not in store" ) ;
				} break ;
				case FileTag::Reg   :
				case FileTag::Empty :
				case FileTag::Exe   : {
					::string val ;
					try                       { val = AcFd(file).read() ;                                   }
					catch (::string const& e) { throw ::pair(Rc::System,cat("cannot read ",file," : ",e)) ; } // do not rm user data, ask user to fix
					crc    = {New,val} ;
					is_reg = true      ;
				} break ;
				default :
					throw "not a sym link nor a regular file"s ;
			}
			//
			decode_tab[::move(ctx)][::move(code)] = { .is_reg=is_reg , .crc=crc } ;
		} catch (::string const& e) {
			res.n_spurious++ ;
			res.to_rm.emplace_back( file , e ) ;
		}
	}
	//
	// encode side
	//
	for( auto const& [file,tag] : files ) {
		if (tag==FileTag::Dir         )                                                       continue ;                                      // already processed in decode pass
		if (!file.ends_with(EncodeSfx))                                                       continue ;                                      // .
		if (tag!=FileTag::Lnk         ) { res.to_rm.emplace_back( file , "not a sym link" ) ; continue ; }
		size_t   slash = file.rfind('/')                                  ; SWEAR( slash!=Npos , file ) ;
		::string ctx   = slash>4/*tab/ */ ? file.substr(4,slash-4) : ""s  ;
		::string crc_s = file.substr( slash+1 )                           ;
		try {
			res.n_spurious++ ;                                                                                                                // until file has been qualified, it is spurious
			crc_s.resize(crc_s.size()-EncodeSfxSz) ;
			/**/                                     throw_unless( +ctx                             , "no context"           ) ;
			/**/                                     throw_unless( crc_s.size()==CodecCrc::Base64Sz , "bad encode name"      ) ;              // CodecFile would assert
			::string  code = read_lnk(file) ;        throw_unless( +code                            , "cannot read sym link" ) ;
			/**/                                     throw_unless(  code.ends_with(DecodeSfx)       , "bad encode link"      ) ;
			code.resize(code.size()-DecodeSfxSz) ;   throw_unless(  code.find('/')==Npos            , "bad encode link"      ) ;
			CodecCrc  crc  ; try { crc = CodecCrc::s_from_base64(crc_s) ; } catch (::string const& e) { throw cat("bad encode name : ",e) ; }
			res.n_spurious-- ;                                                                                                                // file has been qualified, not spurious any more
			//
			auto        it1   = decode_tab .find(ctx ) ; if (it1==decode_tab .end()) { res.n_encode_only ++ ; throw "no decode entry"s     ; }
			auto        it2   = it1->second.find(code) ; if (it2==it1->second.end()) { res.n_encode_only ++ ; throw "no decode entry"s     ; }
			CodecEntry& entry = it2->second            ; if (entry.crc!=crc        ) { res.n_inconsistent++ ; throw "inconsistent encode"s ; }
			entry.encoded = true ;
		} catch (::string const& e) {
			res.to_rm.emplace_back( file , e ) ;
		}
	}
	//
	// synthesis
	//
	auto use_node = [&]( ::string const& ctx , ::string const& code , CodecEntry const& entry ) {
		::string ctx_s = cat("tab/",ctx,'/') ;
		if (entry.is_reg) {
			::string node = cat(ctx_s,code,DecodeSfx) ;
			res.to_store.emplace_back( node , _store_file(entry.crc)               ) ;      // content is copied to store (no-op if already there)
			res.to_lnk  .emplace_back( node , mk_rel(_store_file(entry.crc),ctx_s) ) ;      // then node is replaced by a link to store
		}
		store[entry.crc] = true/*used*/ ;                                                   // entry is created if node is a regular file (and file does not exist yet)
		for( ::string d = ctx_s ; +d ; d=dir_name_s(d) )
			if (!res.to_rmdir_s.erase(d)) break ;
	} ;
	for( auto const& [ctx,ctx_tab] : decode_tab ) {
		::umap<CodecCrc,::pair_s<bool/*encoded*/>> encode_tab ;                             // val crc -> (code,encoded)
		for( auto const& [code,entry] : ctx_tab ) {
			if (!entry.encoded) continue ;
			use_node( ctx , code , entry ) ;
			res.n_ok++ ;
			if (!from_decode) continue ;
			auto inserted = encode_tab.try_emplace(entry.crc,code,true/*encoded*/).second ;
			SWEAR( inserted , ctx,code,entry.crc ) ;                                        // there is a single <crc>.encode file per ctx
		}
		for( auto const& [code,entry] : ctx_tab ) {
			if (entry.encoded) continue ;
			if (!from_decode) {
				res.n_decode_only++ ;
				res.to_rm.emplace_back( cat("tab/",ctx,'/',code,DecodeSfx) , "no encode entry" ) ;
				continue ;
			}
			auto it_inserted = encode_tab.try_emplace(entry.crc,code,false/*encoded*/) ;
			if (it_inserted.second) continue ;
			// manage conflict : keep best code
			::pair_s<bool/*encoded*/>& prev_code   = it_inserted.first->second ;
			::string                   crc_str     = entry.crc.hex()           ;
			bool                       better_code =
				//!     !user_provided                       !encoded          size                  any stable order
				::tuple(crc_str.starts_with(code           ),true             ,code           .size(),code           )
			<	::tuple(crc_str.starts_with(prev_code.first),!prev_code.second,prev_code.first.size(),prev_code.first)
			;
			res.n_decode_only++ ;                                                           // finally no new code
			if (better_code) {
				if (prev_code.second) res.to_rm.emplace_back( cat("tab/",ctx,'/',entry.crc.base64(),EncodeSfx) , "conflict with "+code ) ;
				/**/                  res.to_rm.emplace_back( cat("tab/",ctx,'/',prev_code.first   ,DecodeSfx) , "conflict with "+code ) ;
				prev_code = {code,false/*encoded*/} ;
			} else {
				res.to_rm.emplace_back( cat("tab/",ctx,'/',code,DecodeSfx) , "conflict with "+prev_code.first ) ;
			}
		}
		for( auto const& [crc,code_encoded] : encode_tab ) {
			if (code_encoded.second) continue ;
			::string const& code = code_encoded.first ;
			use_node( ctx , code , ctx_tab.at(code) ) ;
			res.n_reconstructed++ ;
			res.to_lnk.emplace_back( cat("tab/",ctx,'/',crc.base64(),EncodeSfx) , code+DecodeSfx ) ;
		}
	}
	//
	// store
	//
	::set_s used_store_dirs_s ;
	for( auto const& [crc,used] : store ) {
		if (used) used_store_dirs_s.insert(_store_dir_s(crc)) ;
		else      res.to_rm.emplace_back( _store_file(crc) , "unused" ) ;                   // used entries are those either existing or created by to_store
	}
	for( ::string& dir_s : store_dirs_s )
		if (!used_store_dirs_s.contains(dir_s)) res.to_rmdir_s.insert(::move(dir_s)) ;      // nothing kept
	return res ;
}

static ::string _codec_clean_msg() {
	::string cwd_s_ = Disk::cwd_s() ;
	return cat(
		"file_sync=$(cat    "  ,cwd_s_,AdminDirS,"file_sync)",'\n'
	,	"rm -rf             "  ,cwd_s_,rm_slash              ,'\n'
	,	"mkdir -p           "  ,cwd_s_,AdminDirS,rm_slash    ,'\n'
	,	"echo \"$file_sync\" >",cwd_s_,AdminDirS,"file_sync"
	) ;
}

int main( int argc , char* argv[] ) {
	Syntax<Flag> syntax {{
		{ Flag::DryRun      , { .short_name='n' , .doc="report actions but dont execute them" } }
	,	{ Flag::Force       , { .short_name='f' , .doc="execute actions without confirmation" } }
	,	{ Flag::Reconstruct , { .short_name='r' , .doc="reconstruct from decode files"        } }
	}} ;
	CmdLine<Flag> cmd_line { syntax,argc,argv } ;
	if (cmd_line.args.size()<1) syntax.usage("must provide a codec dir to repair") ;
	if (cmd_line.args.size()>1) syntax.usage("cannot repair several codec dirs"  ) ;
	//
	::string const& top_dir_s = with_slash(cmd_line.args[0]) ;
	if (::chdir(top_dir_s.c_str())!=0) exit( Rc::System  , "cannot chdir (",StrErr(),") to ",top_dir_s,rm_slash ) ;
	//
	app_init({
		.cd_root      = false                                                                                                       // we have already chdir'ed to top
	,	.chk_version  = Yes
	,	.key          = "codec dir"
	,	.init_msg     = cat("mkdir -p ",top_dir_s,AdminDirS,rm_slash," ; echo ",Version::Codec," >",top_dir_s,AdminDirS,"version")
	,	.clean_msg    = _codec_clean_msg()
	,	.read_only_ok = cmd_line.flags[Flag::DryRun]
	,	.root_mrkrs   = {}
	,	.version      = Version::Codec
	}) ;
	Py::init(*g_lmake_root_s) ;
	//
	DryRunDigest drd ;
	//                                        vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	try                                     { drd = _dry_run(cmd_line.flags[Flag::Reconstruct]) ; }
	//                                        ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	catch (::string            const& e   ) { exit( Rc::System , e           ) ;                  }
	catch (::pair<Rc,::string> const& rc_e) { exit( rc_e.first , rc_e.second ) ;                  }
	::vmap_s<size_t> summary ;
	if (drd.n_spurious     ) summary.emplace_back( "unrecognized"              , drd.n_spurious      ) ;
	if (drd.n_encode_only  ) summary.emplace_back( "encode only"               , drd.n_encode_only   ) ;
	if (drd.n_inconsistent ) summary.emplace_back( "inconsistent"              , drd.n_inconsistent  ) ;
	if (drd.n_reconstructed) summary.emplace_back( "reconstructed from decode" , drd.n_reconstructed ) ;
	if (drd.n_decode_only  ) summary.emplace_back( "decode only"               , drd.n_decode_only   ) ;
	if (drd.n_ok           ) summary.emplace_back( "correct entry"             , drd.n_ok            ) ;
	//
	#define W(s) Fd::Stdout.write(s)
	#define S(s) mk_shell_str    (s)
	bool        rm_has_dir = ::any_of     ( drd.to_rm     , [](::pair_ss        const& f_r) { return is_dir_name(f_r.first) ; } ) ;
	size_t      w1         = ::max<size_t>( drd.to_rm     , [](::pair_ss        const& f_r) { return S(f_r.first )  .size() ; } ) ;
	size_t      w2         = ::max<size_t>( drd.to_store  , [](::pair_ss        const& s_d) { return S(s_d.first )  .size() ; } ) ;
	size_t      w3         = ::max<size_t>( drd.to_lnk    , [](::pair_ss        const& l_t) { return S(l_t.second)  .size() ; } ) ;
	size_t      w4         = ::max<size_t>( summary       , [](::pair_s<size_t> const& k_v) { return     k_v.first  .size() ; } ) ;
	size_t      w5         = ::max<size_t>( summary       , [](::pair_s<size_t> const& k_v) { return cat(k_v.second).size() ; } ) ;
	bool        nl         = false                                                                                                ; // generate new line between categories
	const char* rm[2]      = { rm_has_dir?"rm -f  ":"rm -f " , "rm -rf " }                                                        ;
	for( auto     const& [f  ,rsn] : drd.to_rm      ) { W(cat(rm[is_dir_name(f)],widen(S(no_slash(f  )),w1)," # ",rsn   ,'\n')) ; nl=true ; } if ( nl && +drd.to_store   ) { W("\n") ; nl=false ; }
	for( auto     const& [src,dst] : drd.to_store   ) { W(cat("cp "             ,widen(S(src          ),w2),' '  ,S(dst),'\n')) ; nl=true ; } if ( nl && +drd.to_lnk     ) { W("\n") ; nl=false ; }
	for( auto     const& [lnk,tgt] : drd.to_lnk     ) { W(cat("ln -sf "         ,widen(S(tgt          ),w3),' '  ,S(lnk),'\n')) ; nl=true ; } if ( nl && +drd.to_rmdir_s ) { W("\n") ; nl=false ; }
	for( ::string const&  d_s      : drd.to_rmdir_s ) { W(cat("rmdir "          ,      S(no_slash(d_s))                 ,'\n')) ; nl=true ; } if ( nl && +summary        ) { W("\n") ; nl=false ; }
	//
	for( auto const& [k,v] : summary ) { W(cat(widen(k,w4)," : ",widen(cat(v),w5,true/*right*/),'\n')) ; nl=true ; }
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
	::umask(config.umask) ;                                                                                      // ensure all created files & dirs have the same permissions as those created by jobs
	//
	// order of actions is important for crash safety : at no time may a value be reachable only through an unused store file (which would be removed by a subsequent run)
	try {
		for( auto const& [file,reason] : drd.to_rm )
			unlnk( file , {.abs_ok=true,.dir_ok=is_dir_name(file)} ) ;
		for( auto const& [src ,dst   ] : drd.to_store ) {
			::string crc_base64 = dst.substr(6/*store/ */,2)+substr_view(dst,9/*store/XX/ */) ;
			creat_store( ""s/*dir_s*/ , crc_base64 , AcFd(src).read() , config.umask , nullptr/*sync_guard*/ ) ; // atomic, no-op if already in store, src is untouched
		}
		for( auto const& [lnk,target] : drd.to_lnk ) {                                                           // atomically replace lnk (which may be a regular file) by a sym link
			::string tmp = lnk+".tmp" ;                                                                          // a left over is seen as spurious by a subsequent run
			unlnk  ( tmp                                                ) ;
			sym_lnk( tmp , target , {.mk_dir=false,.umask=config.umask} ) ;
			rename ( tmp , lnk    , {.mk_dir=false                    } ) ;
		}
		for( auto it=drd.to_rmdir_s.rbegin() ; it!=drd.to_rmdir_s.rend() ; it++ ) {
			int rc = ::rmdir(it->c_str()) ;
			throw_unless( rc==0 , "cannot rmdir (",StrErr(),") ",*it,rm_slash ) ;
		}
	} catch (::string const& e) {
		exit( Rc::System , e ) ;
	}
	//
	mk_dir_s( "store/"s ) ;                                                                                      // ensure canonical layout (harmless if already there)
	mk_dir_s( "tab/"s   ) ;                                                                                      // .
	touch   ( "stamp"s  ) ;
	//
	exit(Rc::Ok) ;
}
