// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "disk.hh"
#include "lib.hh"
#include "time.hh"
#include "trace.hh"

extern ::string* g_startup_dir_s ; // pointer to avoid init/fini order hazards, relative to g_repo_root_s, includes final /,  dir from which command was launched
extern ::string* g_repo_root_s   ; // pointer to avoid init/fini order hazards, absolute                 , root of repository
extern ::string* g_lmake_root_s  ; // pointer to avoid init/fini order hazards, absolute                 , installation dir of lmake
extern ::string* g_exe_name      ; // pointer to avoid init/fini order hazards,                            executable name for user messages

/**/   bool/*read_only*/ app_init( bool read_only_ok , Bool3 chk_version_=Yes , bool cd_root=true ) ; // if chk_version_==Maybe, it is ok to initialize stored version
inline bool/*read_only*/ app_init( bool read_only_ok ,                          bool cd_root      ) { return app_init(read_only_ok,Yes,cd_root) ; }

void chk_version( bool may_init=false , ::string const& admin_dir_s=AdminDirS ) ;
inline ::string git_clean_msg() {
	::string d ; if (+*g_startup_dir_s) d = ' '+no_slash(Disk::dir_name_s(Disk::mk_rel(".",*g_startup_dir_s))) ;
	return "consider : git clean -ffdx"+d ;
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

template<StdEnum Key,StdEnum Flag,bool OptionsAnywhere=true> struct Syntax {
	// cxtors & casts
	Syntax() = default ;
	Syntax(                                 ::umap<Flag,FlagSpec> const& fs    ) : Syntax{{},fs} {}
	Syntax( ::umap<Key,KeySpec> const& ks , ::umap<Flag,FlagSpec> const& fs={} ) {
		has_dflt_key = !ks || ks.contains(Key::None) ;
		//
		SWEAR(!( has_dflt_key && +ks && ks.at(Key::None).short_name )) ;
		//
		for( auto const& [k,s] : ks ) { SWEAR(!keys [+k].short_name) ; keys [+k] = s ; } // ensure no short name conflicts
		for( auto const& [f,s] : fs ) { SWEAR(!flags[+f].short_name) ; flags[+f] = s ; } // .
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
	static constexpr char   NoKey[] = "<no_key>"      ;                                                                                        // cannot use ::strlen which is not constexpr with clang
    static constexpr size_t NoKeySz = sizeof(NoKey)-1 ;                                                                                        // account for terminating null
	size_t wk      = 0     ; for( Key  k : iota(All<Key >) ) if (keys [+k].short_name) wk       = ::max( wk , snake(k).size() ) ;
	size_t wf      = 0     ; for( Flag f : iota(All<Flag>) ) if (flags[+f].short_name) wf       = ::max( wf , snake(f).size() ) ;
	bool   has_arg = false ; for( Flag e : iota(All<Flag>) )                           has_arg |= flags[+e].has_arg             ;
	//
	::string err_msg = ensure_nl(msg) ;
	/**/                 err_msg << Disk::base_name(get_exe()) <<" [ -<short-option>[<option-value>] | --<long-option>[=<option-value>] | <arg> ]* [--] [<arg>]*\n" ;
	if (OptionsAnywhere) err_msg << "options may be interleaved with args\n"                                                                                        ;
	/**/                 err_msg << "-h or --help : print this help\n"                                                                                              ;
	//
	if (wk) {
		if (has_dflt_key) { err_msg << "keys (at most 1) :\n" ; wk = ::max(wk,NoKeySz) ; }
		else                err_msg << "keys (exactly 1) :\n" ;
		if (has_dflt_key) err_msg << "<no key>" << widen("",wk) <<" : "<< keys[0].doc <<'\n' ;
		for( Key k : iota(All<Key>) ) if (keys[+k].short_name) {
			::string option { snake(k) } ; for( char& c : option ) if (c=='_') c = '-' ;
			err_msg << '-' << keys[+k].short_name << " or --" << widen(option,wk) <<" : "<< keys[+k].doc <<'\n' ;
		}
	}
	//
	if (wf) {
		err_msg << "flags (0 or more) :\n"  ;
		for( Flag f : iota(All<Flag>) ) {
			if (!flags[+f].short_name) continue ;
			::string flag { snake(f) } ; for( char& c : flag ) if (c=='_') c = '-' ;
			/**/                                err_msg << '-'<<flags[+f].short_name<<" or --"<<widen(flag,wf) ;
			if      (flags[+f].has_arg        ) err_msg << " <arg>"                                            ;
			else if (has_arg                  ) err_msg << "      "                                            ;
			/**/                                err_msg << " : "<<flags[+f].doc<<set_nl                        ;
		}
	}
	err_msg << "consider :\n"                                                     ;
	err_msg << "  man "      <<Disk::base_name (get_exe()  )<<'\n'                ;
	err_msg << "  <browser> "<<Disk::dir_name_s(get_exe(),2)<<"docs/index.html\n" ;

	exit(Rc::Usage,err_msg) ;
}

template<StdEnum Key,StdEnum Flag> template<bool OptionsAnywhere> CmdLine<Key,Flag>::CmdLine( Syntax<Key,Flag,OptionsAnywhere> const& syntax , int argc , const char* const* argv ) {
	SWEAR(argc>0) ;
	//
	int               a        = 0 ;
	::umap<char,Key > key_map  ; for( Key  k : iota(All<Key >) ) if (syntax.keys [+k].short_name) key_map [syntax.keys [+k].short_name] = k ;
	::umap<char,Flag> flag_map ; for( Flag f : iota(All<Flag>) ) if (syntax.flags[+f].short_name) flag_map[syntax.flags[+f].short_name] = f ;
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
					if (syntax.keys[+k].short_name) {
						throw_if( has_key , "cannot specify both --",option," and --",key ) ;
						throw_if( *p      , "unexpected value for option --",option       ) ;
						key     = k    ;
						has_key = true ;
						continue ;
					}
				}
				if (can_mk_enum<Flag>(option)) {
					Flag f = mk_enum<Flag>(option) ;
					if (syntax.flags[+f].short_name) {
						if (syntax.flags[+f].has_arg) { throw_unless( *p=='=' , "no value for option --"        ,option) ; flag_args[+f] = p+1 ; } // skip = sign
						else                            throw_unless( !*p     , "unexpected value for option --",option) ;
						flags |= f ;
						continue ;
					}
				}
				if (option=="help") throw ""s                           ;
				else                throw "unexpected option --"+option ;
			} else {
				// short options
				const char* p ;
				for( p=arg+1 ; *p ; p++ ) {
					if (key_map.contains(*p)) {
						Key k = key_map.at(*p) ;
						throw_if( has_key , "cannot specify both --",k," and --",key ) ;
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
						else               throw "no value for option -"s+*p ;
						break ;
					} else if (*p=='h') {
						throw ""s ;
					} else {
						throw "unexpected option -"s+*p ;
					}
				}
			}
		}
		throw_unless( has_key || syntax.has_dflt_key , "must specify a key" ) ;
	} catch (::string const& e) { syntax.usage(e) ; }
	//
	exe = argv[0] ;
	args.reserve(argc-a) ;
	for( ; a<argc ; a++ ) args.emplace_back(argv[a]) ;
}
