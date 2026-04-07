// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
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
	bool       cd_root      = true                                         ; // if false, ensure we are at root level
	Bool3      chk_version  = Yes                                          ; // Maybe means it is ok to initialize
	::string   key          = "repo"                                       ;
	::string   init_msg     = "lmake"                                      ;
	::string   clean_msg    = {}                                           ;
	mode_t     umask        = -1                                           ; // right to apply if initializing
	bool       read_only_ok = true                                         ;
	::vector_s root_mrkrs   = { "Lmakefile.py" , "Lmakefile/__init__.py" } ;
	Bool3      trace        = Maybe                                        ; // if Maybe, trace if chk_version!=No
	uint64_t   version      = Version::Repo                                ;
} ;

bool/*read_only*/ app_init(AppInitAction const& ={}) ;

inline ::string git_clean_msg() {
	::string res = "git clean -ffdx" ;
	if (+*g_startup_dir_s) res << ' '<<Disk::mk_canon(Disk::mk_lcl(".",*g_startup_dir_s))<<rm_slash ;
	return res ;
}

inline ::string version_help_str() {
	return cat("version ",Version::Major,'.',Version::Tag," (cache:",Version::Cache,",job:",Version::Job,",repo:",Version::Repo,')') ;
}


enum class NoKey : uint8_t {} ;

struct KeySpec {
	char     short_name = 0 ;
	::string doc        ;
} ;

struct FlagSpec {
	char     short_name = 0     ;
	bool     has_arg    = false ;
	::string doc        ;
} ;

template<UEnum Flag,UEnum Key1=NoKey,UEnum Key2=NoKey> struct Syntax {
	static constexpr bool HasNone1 = requires() {Key1::None;} ;
	static constexpr bool HasNone2 = requires() {Key2::None;} ;
	// cxtors & casts
	Syntax() = default ;
	Syntax(                                                                     ::umap<Flag,FlagSpec> const& fs ) requires (::is_same_v<Key1,NoKey>&&::is_same_v<Key2,NoKey>) : Syntax{{} ,{},fs} {}
	Syntax( ::umap<Key1,KeySpec> const& ks1 ,                                   ::umap<Flag,FlagSpec> const& fs ) requires (                         ::is_same_v<Key2,NoKey>) : Syntax{ks1,{},fs} {}
	Syntax( ::umap<Key1,KeySpec> const& ks1 , ::umap<Key2,KeySpec> const& ks2 , ::umap<Flag,FlagSpec> const& fs ) {
		has_keys1 = +ks1 ;
		has_keys2 = +ks2 ;
		has_flags = +fs  ;
		if constexpr (HasNone1) { static_assert( Key1::None==Key1() ) ; has_dflt_key1 = ks1.contains({}) ; SWEAR_PROD(!( has_dflt_key1 && ks1.at({}).short_name )) ; }
		if constexpr (HasNone2) { static_assert( Key2::None==Key2() ) ; has_dflt_key2 = ks2.contains({}) ; SWEAR_PROD(!( has_dflt_key2 && ks1.at({}).short_name )) ; }
		::uset<char> short_names ;
		for( auto const& [k,s] : ks1 ) { keys1[+k] = s ; if (+s.short_name) SWEAR_PROD( short_names.insert(s.short_name).second , s.short_name ) ; } // ensure no short name conflicts
		for( auto const& [k,s] : ks2 ) { keys2[+k] = s ; if (+s.short_name) SWEAR_PROD( short_names.insert(s.short_name).second , s.short_name ) ; } // ensure no short name conflicts
		for( auto const& [f,s] : fs  ) { flags[+f] = s ; if (+s.short_name) SWEAR_PROD( short_names.insert(s.short_name).second , s.short_name ) ; } // .
	}
	//
	void usage(::string const& msg={}) const ;
	// data
	bool                                  has_keys1     = false ;
	bool                                  has_keys2     = false ;
	bool                                  has_dflt_key1 = false ;
	bool                                  has_dflt_key2 = false ;
	bool                                  has_flags     = false ;
	bool                                  args_ok       = true  ;
	::string                              sub_option    ;
	::array<::optional<KeySpec >,N<Key1>> keys1         ;
	::array<::optional<KeySpec >,N<Key2>> keys2         ;
	::array<::optional<FlagSpec>,N<Flag>> flags         ;
} ;

template<UEnum Flag,UEnum Key1=NoKey,UEnum Key2=NoKey> struct CmdLine {
	// cxtors & casts
	CmdLine() = default ;
	CmdLine( Syntax<Flag,Key1,Key2> const& , int argc , const char* const* argv ) ;
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
	Key1               key1      = {} ;
	Key2               key2      = {} ;
	BitMap<Flag>       flags     ;
	::array_s<N<Flag>> flag_args ;
	::vector_s         args      ;
} ;

template<UEnum Flag,UEnum Key1,UEnum Key2> void Syntax<Flag,Key1,Key2>::usage(::string const& msg) const {
	static constexpr char   NoKeyKey[] = "<no_key>"              ;                                         // cannot use ::strlen which is not constexpr with clang
	static constexpr size_t NoKeySz = sizeof(NoKeyKey)-1/*null*/ ;
	::string exe_path = get_exe()                                ;
	::string exe_name = Disk::base_name(exe_path)                ;
	bool     has_arg  = false                                    ; for( Flag e : iota(All<Flag>) ) if (+flags[+e]) has_arg |= flags[+e]->has_arg ;
	//
	::string err_msg = with_nl(msg) ;
	//
	/**/             err_msg <<      exe_name                                                                                          ;
	if (+sub_option) err_msg <<' '<< sub_option                                                                                        ;
	if (args_ok    ) err_msg <<' '<< "[ -<short-option>[<option-value>] | --<long-option>[=<option-value>] | <arg> ]* [--] [<arg>]*\n" ;
	else             err_msg <<' '<< "[ -<short-option>[<option-value>] | --<long-option>[=<option-value>] ]*\n"                       ;
	//
	if (!sub_option) err_msg << version_help_str() <<"\n"                   ;                              // analyze top level cmd line
	if (args_ok    ) err_msg << "options may be interleaved with args\n"    ;
	/**/             err_msg << "-h or --help : print this help and exit\n" ;
	if (!sub_option) err_msg << "--version    : print version and exit\n"   ;
	//
	if constexpr (!::is_same_v<Key1,NoKey>) if (has_keys1) {
			// XXX/ : more elegant form below hit a false positive for Warray-bounds with gcc -O[23]
			//size_t wk = ::max<size_t>( iota(All<Key>) , [&](Key k) { return +keys[+k] && (!HasNone||+k) ? snake(k).size() : 0 ; } ) ; if (has_dflt_key) wk = ::max(wk,NoKeySz) ;
			size_t wk = 0 ; for( Key1 k : iota(All<Key1>) ) if ( +keys1[+k] && (!HasNone1||+k) ) wk = ::max(wk,snake(k).size()) ; if (has_dflt_key1) wk = ::max(wk,NoKeySz) ;
			if (has_dflt_key1) err_msg << "keys (at most 1) :\n"                              ;
			else               err_msg << "keys (exactly 1) :\n"                              ;
			if (has_dflt_key1) err_msg << widen(NoKeyKey,8+wk) <<" : "<< keys1[0]->doc <<'\n' ;
			for( Key1 k : iota(All<Key1>) ) {
				if ( has_dflt_key1 && !k ) continue ;                                                      // default key line is output separately above
				if ( !keys1[+k]          ) continue ;
				::string option { snake(k) } ; for( char& c : option ) if (c=='_') c = '-' ;
				if (keys1[+k]->short_name) err_msg << '-'<<keys1[+k]->short_name<<" or --"<<widen(option,wk)<<" : "<<keys1[+k]->doc<<add_nl ;
				else                       err_msg << ' '<<' '                  <<"    --"<<widen(option,wk)<<" : "<<keys1[+k]->doc<<add_nl ;
			}
		}
	if constexpr (!::is_same_v<Key2,NoKey>) if (has_keys2) {
		// XXX/ : more elegant form below hit a false positive for Warray-bounds with gcc -O[23]
		//size_t wk = ::max<size_t>( iota(All<Key>) , [&](Key k) { return +keys[+k] && (!HasNone||+k) ? snake(k).size() : 0 ; } ) ; if (has_dflt_key) wk = ::max(wk,NoKeySz) ;
		size_t wk = 0 ; for( Key2 k : iota(All<Key2>) ) if ( +keys2[+k] && (!HasNone2||+k) ) wk = ::max(wk,snake(k).size()) ; if (has_dflt_key2) wk = ::max(wk,NoKeySz) ;
		if (has_dflt_key2) err_msg << "marks (at most 1) :\n"                             ;
		else               err_msg << "marks (exactly 1) :\n"                             ;
		if (has_dflt_key2) err_msg << widen(NoKeyKey,8+wk) <<" : "<< keys2[0]->doc <<'\n' ;
		for( Key2 k : iota(All<Key2>) ) {
			if ( has_dflt_key2 && !k ) continue ;                                                          // default key line is output separately above
			if ( !keys2[+k]          ) continue ;
			::string option { snake(k) } ; for( char& c : option ) if (c=='_') c = '-' ;
			if (keys2[+k]->short_name) err_msg << '-'<<keys2[+k]->short_name<<" or --"<<widen(option,wk)<<" : "<<keys2[+k]->doc<<add_nl ;
			else                       err_msg << ' '<<' '                  <<"    --"<<widen(option,wk)<<" : "<<keys2[+k]->doc<<add_nl ;
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

template<UEnum Flag,UEnum Key1,UEnum Key2> CmdLine<Flag,Key1,Key2>::CmdLine( Syntax<Flag,Key1,Key2> const& syntax , int argc , const char* const* argv ) {
	SWEAR_PROD(argc>0) ;
	int a = 0 ;
	//
	::umap<char,Key1> key_map1 ; key_map1.reserve(N<Key1>) ; for( Key1 k : iota(All<Key1>) ) if ( +syntax.keys1[+k] && syntax.keys1[+k]->short_name ) key_map1[syntax.keys1[+k]->short_name] = k ;
	::umap<char,Key2> key_map2 ; key_map2.reserve(N<Key2>) ; for( Key2 k : iota(All<Key2>) ) if ( +syntax.keys2[+k] && syntax.keys2[+k]->short_name ) key_map2[syntax.keys2[+k]->short_name] = k ;
	::umap<char,Flag> flag_map ; flag_map.reserve(N<Flag>) ; for( Flag f : iota(All<Flag>) ) if ( +syntax.flags[+f] && syntax.flags[+f]->short_name ) flag_map[syntax.flags[+f]->short_name] = f ;
	try {
		::string_view seen_key1     ;
		::string_view seen_key2     ;
		bool          force_args    = false ;
		bool          print_help    = false ;
		bool          print_version = false ;
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
				if (arg[2]==0) { force_args = true ; continue ; }                                           // a lonely --, options are no more recognized
				::string    option ;
				const char* p      ;
				for( p=arg+2 ; *p && *p!='=' ; p++ ) switch (*p) {
					case '_' :                         throw "use -, not _, to separate words"s ;
					case '-' : option.push_back('_') ; break                                    ;           // make snake case to use mk_enum while usual convention for options is to use '-'
					default  : option.push_back(*p ) ;
				}
				::string_view key_str { arg+2 , size_t(p-(arg+2)) } ; SWEAR( key_str.size()>1 , key_str ) ; // ensure no confusion with short options
				if constexpr (!::is_same_v<Key1,NoKey>) if (can_mk_enum<Key1>(option)) {
					Key1 k = mk_enum<Key1>(option) ;
					if (+syntax.keys1[+k]) {
						throw_if( +seen_key1 , "cannot specify both -",(seen_key1.size()>1?"-":""),seen_key1," and --",key_str ) ;
						throw_if( *p         , "unexpected value for --"                                              ,key_str ) ;
						key1      = k       ;
						seen_key1 = key_str ;
						continue ;
					}
				}
				if constexpr (!::is_same_v<Key2,NoKey>) if (can_mk_enum<Key2>(option)) {
					Key2 k = mk_enum<Key2>(option) ;
					if (+syntax.keys2[+k]) {
						throw_if( +seen_key2 , "cannot specify both -",(seen_key2.size()>1?"-":""),seen_key2," and --",key_str ) ;
						throw_if( *p         , "unexpected value for --"                                              ,key_str ) ;
						key2      = k       ;
						seen_key2 = key_str ;
						continue ;
					}
				}
				if (can_mk_enum<Flag>(option)) {
					Flag f = mk_enum<Flag>(option) ;
					if (+syntax.flags[+f]) {
						if (syntax.flags[+f]->has_arg) { throw_unless( *p=='=' , "no value for option --"        ,key_str) ; flag_args[+f] = p+1 ; } // +1 to skip = sign
						else                             throw_unless( !*p     , "unexpected value for option --",key_str) ;
						flags |= f ;
						continue ;
					}
				}
				if      ( !syntax.sub_option && key_str=="version" ) print_version = true ;
				else if (                       key_str=="help"    ) print_help    = true ;
				else                                                 throw "unexpected option --"+key_str ;
			} else {
				// short options
				const char* p ;
				for( p=arg+1 ; *p ; p++ )
					if ( auto it=key_map1.find(*p) ; it!=key_map1.end() ) {
						Key1 k = it->second ;
						throw_if( +seen_key1 , "cannot specify both -",(seen_key1.size()>1?"-":""),seen_key1," and -",*p ) ;
						key1     = k    ;
						seen_key1 = { p , 1 } ;
					} else if ( auto it=key_map2.find(*p) ; it!=key_map2.end() ) {
						Key2 k = it->second ;
						throw_if( +seen_key2 , "cannot specify both -",(seen_key2.size()>1?"-":""),seen_key2," and -",*p ) ;
						key2     = k    ;
						seen_key2 = { p , 1 } ;
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
			if      constexpr (!requires(){Flag::Quiet;}) exit( Rc::Ok , version_help_str() ) ;
			else if           (!flags[Flag::Quiet]      ) exit( Rc::Ok , version_help_str() ) ;
			else                                          exit( Rc::Ok , Version::Repo      ) ;
		}
		throw_if    ( print_help                                                                      ) ;
		throw_unless( +seen_key1 || !syntax.has_keys1 || syntax.has_dflt_key1 , "must specify a key"  ) ;
		throw_unless( +seen_key2 || !syntax.has_keys2 || syntax.has_dflt_key2 , "must specify a mark" ) ;
	} catch (::string const& e) { syntax.usage(e) ; }
	//
	args.reserve(argc-a) ;
	for( ; a<argc ; a++ ) args.emplace_back(argv[a]) ;
}
