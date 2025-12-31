// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "disk.hh"
#include "trace.hh"
#include "version.hh"

extern StaticUniqPtr<::string> g_startup_dir_s ; // relative to g_repo_root_s, includes final /,  dir from which command was launched
extern StaticUniqPtr<::string> g_repo_root_s   ; // absolute                 , root of repository
extern StaticUniqPtr<::string> g_lmake_root_s  ; // absolute                 , installation dir of lmake
extern StaticUniqPtr<::string> g_exe_name      ; //                            executable name for user messages

struct AppInitAction {
	Bool3      chk_version  = Yes   ; // Maybe means it is ok to initialize
	bool       cd_root      = true  ; // if false, ensure we are at root level
	PermExt    perm_ext     = {}    ; // right to apply if initializing
	bool       read_only_ok = true  ;
	Bool3      trace        = Maybe ; // if Maybe, trace if chk_version!=No
	::vector_s root_mrkrs   = {}    ;
	uint64_t   version      = {}    ;
} ;

struct SearchRootResult {
	::string top_s     ;
	::string sub_s     ;
	::string startup_s ;
} ;

bool/*read_only*/ app_init   ( AppInitAction const&                            ) ;
void              chk_version( AppInitAction const& , ::string const& dir_s={} ) ;
SearchRootResult search_root ( AppInitAction const&                            ) ;

inline ::string git_clean_msg() {
	::string res = "git clean -ffdx" ;
	if (+*g_startup_dir_s) res << ' '<<Disk::dir_name_s(Disk::mk_rel(".",*g_startup_dir_s))<<rm_slash ;
	return res ;
}

struct KeySpec {
	char     short_name = 0 ;
	::string doc        ;
} ;

struct FlagSpec {
	char     short_name = 0     ;
	bool     has_arg    = false ;
	::string doc        ;
} ;

template<UEnum Key,UEnum Flag> struct Syntax {
	static constexpr bool HasNone = requires() {Key::None;} ;
	static ::string s_version_str() {
		return cat("version ",Version::Major," (cache:",Version::DaemonCache,",job:",Version::Job,",repo:",Version::Repo,')') ;
	}
	// cxtors & casts
	Syntax() = default ;
	Syntax(                                 ::umap<Flag,FlagSpec> const& fs    ) : Syntax{{},fs} {}
	Syntax( ::umap<Key,KeySpec> const& ks , ::umap<Flag,FlagSpec> const& fs={} ) {
		has_keys     = +ks ;
		has_flags    = +fs ;
		has_dflt_key = !ks ;
		if constexpr (HasNone) {
			static_assert( Key::None==Key() ) ;
			has_dflt_key |= ks.contains({}) ;
			SWEAR(!( has_dflt_key && ks.contains({}) && ks.at({}).short_name )) ;
		}
		::uset<char> short_names ;
		for( auto const& [k,s] : ks ) { keys [+k] = s ; if (+s.short_name) SWEAR( short_names.insert(s.short_name).second , s.short_name ) ; } // ensure no short name conflicts
		for( auto const& [f,s] : fs ) { flags[+f] = s ; if (+s.short_name) SWEAR( short_names.insert(s.short_name).second , s.short_name ) ; } // .
	}
	//
	void usage(::string const& msg={}) const ;
	// data
	bool                                  has_keys     = false ;
	bool                                  has_flags    = false ;
	bool                                  has_dflt_key = false ;
	bool                                  args_ok      = true  ;
	::string                              sub_option   ;
	::array<::optional<KeySpec >,N<Key >> keys         ;
	::array<::optional<FlagSpec>,N<Flag>> flags        ;
} ;

template<UEnum Key,UEnum Flag> struct CmdLine {
	// cxtors & casts
	CmdLine() = default ;
	CmdLine( Syntax<Key,Flag> const& , int argc , const char* const* argv ) ;
	// services
	::vector_s files() const {
		Trace trace("files") ;
		::vector_s res ; res.reserve(args.size()) ;
		for( ::string const& a : args ) {
			throw_unless( +a , "empty arg" ) ;
			res.push_back(Disk::mk_glb(a,*g_startup_dir_s)) ; // translate arg to be relative to repo root dir
			trace(a,"->",res.back()) ;
		}
		return res ;
	}
	// data
	Key                key       = {} ;
	BitMap<Flag>       flags     ;
	::array_s<N<Flag>> flag_args ;
	::vector_s         args      ;
} ;

template<UEnum Key,UEnum Flag> void Syntax<Key,Flag>::usage(::string const& msg) const {
	static constexpr char   NoKey[] = "<no_key>"      ;                                  // cannot use ::strlen which is not constexpr with clang
	static constexpr size_t NoKeySz = sizeof(NoKey)-1 ;                                  // account for terminating null
	::string exe_path = get_exe()                 ;
	::string exe_name = Disk::base_name(exe_path) ;
	bool     has_arg  = false                     ; for( Flag e : iota(All<Flag>) ) if (+flags[+e]) has_arg |= flags[+e]->has_arg ;
	//
	::string err_msg = with_nl(msg) ;
	//
	/**/             err_msg <<      exe_name                                                                                          ;
	if (+sub_option) err_msg <<' '<< sub_option                                                                                        ;
	if (args_ok    ) err_msg <<' '<< "[ -<short-option>[<option-value>] | --<long-option>[=<option-value>] | <arg> ]* [--] [<arg>]*\n" ;
	else             err_msg <<' '<< "[ -<short-option>[<option-value>] | --<long-option>[=<option-value>] ]*\n"                       ;
	//
	if (!sub_option) err_msg << s_version_str() <<"\n"                      ;            // analyze top level cmd line
	if (args_ok    ) err_msg << "options may be interleaved with args\n"    ;
	/**/             err_msg << "-h or --help : print this help and exit\n" ;
	if (!sub_option) err_msg << "--version    : print version and exit\n"   ;
	//
	if (has_keys) {
		// XXX/ : more elegant form below hit a false positive for Warray-bounds with gcc -O[23]
		//size_t wk = ::max<size_t>( iota(All<Key>) , [&](Key k) { return +keys[+k] && (!HasNone||+k) ? snake(k).size() : 0 ; } ) ; if (has_dflt_key) wk = ::max(wk,NoKeySz) ;
		size_t wk = 0 ; for( Key k : iota(All<Key>) ) if ( +keys[+k] && (!HasNone||+k) ) wk = ::max(wk,snake(k).size()) ; if (has_dflt_key) wk = ::max(wk,NoKeySz) ;
		if (has_dflt_key) err_msg << "keys (at most 1) :\n"                          ;
		else              err_msg << "keys (exactly 1) :\n"                          ;
		if (has_dflt_key) err_msg << widen(NoKey,8+wk) <<" : "<< keys[0]->doc <<'\n' ;
		for( Key k : iota(All<Key>) ) {
			if ( has_dflt_key && !k ) continue ;                                         // default key line is output separately above
			if ( !keys[+k]          ) continue ;
			::string option { snake(k) } ; for( char& c : option ) if (c=='_') c = '-' ;
			if (keys[+k]->short_name) err_msg << '-'<<keys[+k]->short_name<<" or --"<<widen(option,wk)<<" : "<<keys[+k]->doc<<add_nl ;
			else                      err_msg << ' '<<' '                 <<"    --"<<widen(option,wk)<<" : "<<keys[+k]->doc<<add_nl ;
		}
	}
	//
	if (has_flags) {
		size_t wf = ::max<size_t>( iota(All<Flag>) , [&](Flag f) { return +flags[+f] ? snake(f).size() : 0 ; } ) ;
		err_msg << "flags (0 or more) :\n"  ;
		for( Flag f : iota(All<Flag>) ) {
			if (!flags[+f]) continue ;
			::string flag { snake(f) } ; for( char& c : flag ) if (c=='_') c = '-' ;
			if      (flags[+f]->short_name) err_msg << '-'<<flags[+f]->short_name<<" or --"<<widen(flag,wf) ;
			else                            err_msg << ' '<<' '                  <<"    --"<<widen(flag,wf) ;
			if      (flags[+f]->has_arg   ) err_msg << " <arg>"                                             ;
			else if (has_arg              ) err_msg << "      "                                             ;
			/**/                            err_msg << " : "<<flags[+f]->doc<<add_nl                        ;
		}
	}
	err_msg << "consider :"                                                   <<'\n' ;
	err_msg << "  man "      <<exe_name                                       <<'\n' ;
	err_msg << "  <browser> "<<Disk::dir_name_s(exe_path,2)<<"docs/index.html"<<'\n' ;
	//
	if (!sub_option) exit( Rc::Usage , err_msg ) ;
	else             throw err_msg ;
}

template<UEnum Key,UEnum Flag> CmdLine<Key,Flag>::CmdLine(  Syntax<Key,Flag> const& syntax , int argc , const char* const* argv ) {
	SWEAR(argc>0) ;
	int a = 0 ;
	//
	::umap<char,Key > key_map  ; key_map .reserve(N<Key >) ; for( Key  k : iota(All<Key >) ) if ( +syntax.keys [+k] && syntax.keys [+k]->short_name ) key_map [syntax.keys [+k]->short_name] = k ;
	::umap<char,Flag> flag_map ; flag_map.reserve(N<Flag>) ; for( Flag f : iota(All<Flag>) ) if ( +syntax.flags[+f] && syntax.flags[+f]->short_name ) flag_map[syntax.flags[+f]->short_name] = f ;
	try {
		bool has_key       = false ;
		bool force_args    = false ;
		bool print_help    = false ;
		bool print_version = false ;
		for( a=1 ; a<argc ; a++ ) {
			const char* arg = argv[a] ;
			if ( arg[0]!='-' || force_args ) {
				throw_unless( syntax.args_ok , "unrecognized option does not start with - : ",arg ) ;
				args.emplace_back(arg) ;
				continue ;
			}
			throw_unless( arg[1] , "unexpected lonely -" ) ;
			if (arg[1]=='-') {
				// long option
				if (arg[2]==0) { force_args = true ; continue ; }                                                     // a lonely --, options are no more recognized
				::string    option ;
				const char* p      ;
				for( p=arg+2 ; *p && *p!='=' ; p++ ) switch (*p) {
					case '_' :                         throw "unexpected option (use -, not _, to separate words)"s ;
					case '-' : option.push_back('_') ; break                                                        ; // make snake case to use mk_enum while usual convention for options is to use '-'
					default  : option.push_back(*p ) ;
				}
				if (can_mk_enum<Key>(option)) {
					Key k = mk_enum<Key>(option) ;
					if (+syntax.keys[+k]) {
						throw_if( has_key , "cannot specify both --",key," and --",option ) ;
						throw_if( *p      , "unexpected value for option --",option       ) ;
						key     = k    ;
						has_key = true ;
						continue ;
					}
				}
				if (can_mk_enum<Flag>(option)) {
					Flag f = mk_enum<Flag>(option) ;
					if (+syntax.flags[+f]) {
						if (syntax.flags[+f]->has_arg) { throw_unless( *p=='=' , "no value for option --"        ,option) ; flag_args[+f] = p+1 ; } // +1 to skip = sign
						else                             throw_unless( !*p     , "unexpected value for option --",option) ;
						flags |= f ;
						continue ;
					}
				}
				if      ( !syntax.sub_option && option=="version" ) print_version = true ;
				else if (                       option=="help"    ) print_help    = true ;
				else                                                throw "unexpected option --"+option ;
			} else {
				// short options
				const char* p ;
				for( p=arg+1 ; *p ; p++ )
					if ( auto it=key_map.find(*p) ; it!=key_map.end() ) {
						Key k = it->second ;
						throw_if( has_key , "cannot specify both --",key," and --",k ) ;
						key     = k    ;
						has_key = true ;
					} else if ( auto it=flag_map.find(*p) ; it!=flag_map.end() ) {
						Flag f  = it->second ;
						flags  |= f          ;
						//
						if (!syntax.flags[+f]->has_arg) continue ;
						//
						if      (p[1]    ) flag_args[+f] = p+1       ;
						else if (a+1<argc) flag_args[+f] = argv[++a] ;
						else               throw cat("no value for option -",*p) ;
						break ;
					} else if (*p=='h') {
						print_help = true ;
					} else {
						throw cat("unexpected option -",*p) ;
					}
			}
		}
		if (print_version) {
			if      constexpr (!requires(){Flag::Quiet;}) exit( Rc::Ok , syntax.s_version_str() ) ;
			else if           (!flags[Flag::Quiet]      ) exit( Rc::Ok , syntax.s_version_str() ) ;
			else                                          exit( Rc::Ok , Version::Repo          ) ;
		}
		throw_if    ( print_help                                            ) ;
		throw_unless( has_key || syntax.has_dflt_key , "must specify a key" ) ;
	} catch (::string const& e) { syntax.usage(e) ; }
	//
	args.reserve(argc-a) ;
	for( ; a<argc ; a++ ) args.emplace_back(argv[a]) ;
}
