// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "disk.hh"
#include "lib.hh"
#include "time.hh"
#include "trace.hh"

extern ::string* g_startup_dir_s ; // pointer to avoid init/fini order hazards, relative to g_root_dir, includes final /,  dir from which command was launched
extern ::string* g_lmake_dir     ; // pointer to avoid init/fini order hazards, absolute              , installation dir of lmake
extern ::string* g_root_dir      ; // pointer to avoid init/fini order hazards, absolute              , root of repository
extern ::string* g_exe_name      ; // pointer to avoid init/fini order hazards, absolute              , executable name for user messages

/**/          void app_init( Bool3 chk_version_=Yes , bool cd_root=true ) ;                           // if chk_version_==Maybe, it is ok to initialize stored version
static inline void app_init(                          bool cd_root      ) { app_init(Yes,cd_root) ; }

void chk_version( bool may_init=false , ::string const& dir_s={} , bool with_repo=true ) ;

struct KeySpec {
	char     short_name = 0 ;
	::string doc        ;
} ;

struct FlagSpec {
	char     short_name = 0     ;
	bool     has_arg    = false ;
	::string doc        ;
} ;

template<StdEnum Key,StdEnum Flag,bool OptionsAnywhere=true> struct Syntax {
	// cxtors & casts
	Syntax() = default ;
	Syntax(                                 ::umap<Flag,FlagSpec> const& fs    ) : Syntax{{},fs} {}
	Syntax( ::umap<Key,KeySpec> const& ks , ::umap<Flag,FlagSpec> const& fs={} ) {
		has_dflt_key = !ks || ks.contains(Key::None) ;
		//
		SWEAR(!( has_dflt_key && +ks && ks.at(Key::None).short_name )) ;
		//
		for( auto const& [k,s] : ks ) keys [+k] = s ;
		for( auto const& [f,s] : fs ) flags[+f] = s ;
	}
	//
	[[noreturn]] void usage(::string const& msg={}) const ;
	// data
	bool                      has_dflt_key = false ;
	::array<KeySpec ,N<Key >> keys         ;
	::array<FlagSpec,N<Flag>> flags        ;
} ;

template<StdEnum Key,StdEnum Flag> struct CmdLine {
	// cxtors & casts
	CmdLine() = default ;
	template<bool OAW> CmdLine( Syntax<Key,Flag,OAW> const& , int argc , const char* const* argv ) ;
	// services
	::vector_s files() const {
		Trace trace("files") ;
		::vector_s res ; res.reserve(args.size()) ;
		for( ::string const& a : args ) {
			res.push_back(Disk::mk_glb(a,*g_startup_dir_s)) ; // translate arg to be relative to repo root dir
			trace(a,"->",res.back()) ;
		}
		return res ;
	}
	// data
	::string           exe       ;
	Key                key       = Key::None ;
	BitMap<Flag>       flags     ;
	::array_s<N<Flag>> flag_args ;
	::vector_s         args      ;
} ;

template<StdEnum Key,StdEnum Flag,bool OptionsAnywhere> [[noreturn]] void Syntax<Key,Flag,OptionsAnywhere>::usage(::string const& msg) const {
	size_t key_sz  = 0     ; for( Key  k : All<Key > ) if (keys [+k].short_name) key_sz   = ::max( key_sz  , snake(k).size() ) ;
	size_t flag_sz = 0     ; for( Flag f : All<Flag> ) if (flags[+f].short_name) flag_sz  = ::max( flag_sz , snake(f).size() ) ;
	bool   has_arg = false ; for( Flag e : All<Flag> )                           has_arg |= flags[+e].has_arg                  ;
	//
	::cerr << ::left ;
	//
	if (+msg           ) ::cerr << msg <<'\n' ;
	/**/                 ::cerr << Disk::base_name(Disk::read_lnk("/proc/self/exe")) <<" [ -<short-option>[<option-value>] | --<long-option>[=<option-value>] | <arg> ]* [--] [<arg>]*\n" ;
	if (OptionsAnywhere) ::cerr << "options may be interleaved with args\n"                                                                                                               ;
	/**/                 ::cerr << "-h or --help : print this help\n"                                                                                                                     ;
	//
	if (key_sz) {
		if (has_dflt_key) ::cerr << "keys (at most 1) :\n" ;
		else              ::cerr << "keys (exactly 1) :\n" ;
		if (has_dflt_key)                                ::cerr << "<no key>"                            << setw(key_sz)<<""       <<" : "<< keys[0 ].doc <<'\n' ;
		for( Key k : All<Key> ) if (keys[+k].short_name) ::cerr <<'-' << keys[+k].short_name << " or --" << setw(key_sz)<<snake(k) <<" : "<< keys[+k].doc <<'\n' ;
	}
	//
	if (flag_sz) {
		::cerr << "flags (0 or more) :\n"  ;
		for( Flag f : All<Flag> ) {
			if (!flags[+f].short_name) continue ;
			/**/                        ::cerr << '-'<<flags[+f].short_name<<" or --"<<setw(flag_sz)<<snake(f) ;
			if      (flags[+f].has_arg) ::cerr << " <arg>"                                                     ;
			else if (has_arg          ) ::cerr << "      "                                                     ;
			/**/                        ::cerr << " : "<<flags[+f].doc<<'\n'                                   ;
		}
	}
	exit(Rc::Usage) ;
}

template<StdEnum Key,StdEnum Flag> template<bool OptionsAnywhere> CmdLine<Key,Flag>::CmdLine( Syntax<Key,Flag,OptionsAnywhere> const& syntax , int argc , const char* const* argv ) {
	SWEAR(argc>0) ;
	//
	int               a        = 0 ;
	::umap<char,Key > key_map  ; for( Key  k : All<Key > ) if (syntax.keys [+k].short_name) key_map [syntax.keys [+k].short_name] = k ;
	::umap<char,Flag> flag_map ; for( Flag f : All<Flag> ) if (syntax.flags[+f].short_name) flag_map[syntax.flags[+f].short_name] = f ;
	try {
		bool has_key    = false ;
		bool force_args = false ;
		for( a=1 ; a<argc ; a++ ) {
			const char* arg = argv[a] ;
			if ( arg[0]!='-' || force_args ) {
				args.emplace_back(arg) ;
				if (!OptionsAnywhere) force_args = true ;
				continue ;
			}
			if (!arg[1]) throw "unexpected lonely -"s ;
			if (arg[1]=='-') {
				// long option
				if (arg[2]==0) { force_args = true ; continue ; }                             // a lonely --, options are no more recognized
				::string    option ;
				const char* p      ;
				for( p=arg+2 ; *p && *p!='=' ; p++ ) option.push_back( *p=='-' ? '_' : *p ) ; // make snake case to use mk_enum while usual convention for options is to use '-'
				if (can_mk_enum<Key>(option)) {
					Key k = mk_enum<Key>(option) ;
					if (syntax.keys[+k].short_name) {
						if (has_key) throw to_string("cannot specify both --",option," and --",snake(key)) ;
						if (*p     ) throw to_string("unexpected value for option --",option             ) ;
						key     = k    ;
						has_key = true ;
						continue ;
					}
				}
				if (can_mk_enum<Flag>(option)) {
					Flag f = mk_enum<Flag>(option) ;
					if (syntax.flags[+f].short_name) {
						if (syntax.flags[+f].has_arg) { if (*p!='=') throw to_string("no value for option --"        ,option) ; flag_args[+f] = p+1 ; } // skip = sign
						else                          { if (*p     ) throw to_string("unexpected value for option --",option) ;                       }
						flags |= f ;
						continue ;
					}
				}
				if (option=="help") throw ""s                                      ;
				else                throw to_string("unexpected option --",option) ;
			} else {
				// short options
				const char* p ;
				for( p=arg+1 ; *p ; p++ ) {
					if (key_map.contains(*p)) {
						Key k = key_map.at(*p) ;
						if (has_key) throw to_string("cannot specify both --",snake(k)," and --",snake(key)) ;
						key     = k    ;
						has_key = true ;
					} else if (flag_map.contains(*p)) {
						Flag f = flag_map.at(*p) ;
						flags |= f ;
						//
						if (!syntax.flags[+f].has_arg) continue ;
						//
						p++ ;
						if      (*p      ) flag_args[+f] = p         ;
						else if (a+1<argc) flag_args[+f] = argv[++a] ;
						else               throw to_string("no value for option -",*p) ;
						break ;
					} else if (*p=='h') {
						throw ""s ;
					} else {
						throw to_string("unexpected option -",*p) ;
					}
				}
			}
		}
		if ( !has_key && !syntax.has_dflt_key ) throw "must specify a key"s ;
	} catch (::string const& e) { syntax.usage(e) ; }
	//
	exe = argv[0] ;
	args.reserve(argc-a) ;
	for( ; a<argc ; a++ ) args.emplace_back(argv[a]) ;
}

//
// env encoding
//

// replace occurrences of repo and lmake absolute references by markers and vice versa
// this is important not only for user comfort, but also to make the cache entries independent of these 2 directories
::string env_encode(::string&& txt) ;
::string env_decode(::string&& txt) ;
