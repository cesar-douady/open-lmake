// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <dlfcn.h>  // dlsym

#include "utils.hh"

#include "record.hh"

extern bool g_force_orig ;                                                     // for use with LD_AUDIT : when true, dlsym will return the real orig function, not the autodep one

void* get_orig(const char*) ;

struct Audit : RecordSock {                                                    // singleton
	// statics
	static Audit & t_audit() ;                                                 // access to the unique instance
private :
	static Record& _t_record() { return t_audit() ; }                          // idem, as a Record
	// cxtors & casts
public :
	template<class Action,bool E=false> struct AuditAction : Action,Ctx {
		// errno must be protected from our auditing actions in cxtor and operator()
		// more specifically, errno must be the original one before the actual call to libc
		// and must be the one after the actual call to libc when auditing code finally leave
		// Ctx contains save_errno in its cxtor and restore_errno in its dxtor
		// so here, errno must be restored at the end of cxtor and saved at the beginning of operator()
		template<class... A> AuditAction ( A&&... args ) : Action{ !Lock::s_busy() , _t_record() , ::forward<A>(args)... } { restore_errno() ; }
		int operator()(            int res) requires(!E) { save_errno() ; if (Lock::s_busy()) return res ; else return Action::operator()(_t_record(),       res            ) ; }
		int operator()(            int res) requires( E) { save_errno() ; if (Lock::s_busy()) return res ; else return Action::operator()(_t_record(),       res,get_errno()) ; }
		int operator()(bool has_fd,int res) requires(!E) { save_errno() ; if (Lock::s_busy()) return res ; else return Action::operator()(_t_record(),has_fd,res            ) ; }
		int operator()(bool has_fd,int res) requires( E) { save_errno() ; if (Lock::s_busy()) return res ; else return Action::operator()(_t_record(),has_fd,res,get_errno()) ; }
	} ;
	using Chdir   = AuditAction<Record::Chdir  ,false/*errno*/> ;
	using Lnk     = AuditAction<Record::Lnk    ,true /*errno*/> ;
	using Open    = AuditAction<Record::Open   ,true /*errno*/> ;
	using ReadLnk = AuditAction<Record::ReadLnk,true /*errno*/> ;
	using Rename  = AuditAction<Record::Rename ,true /*errno*/> ;
	using SymLnk  = AuditAction<Record::SymLnk ,false/*errno*/> ;
	using Unlink  = AuditAction<Record::Unlink ,false/*errno*/> ;
	//
	struct Fopen : AuditAction<Record::Open,true/*errno*/> {
		using Base = AuditAction<Record::Open,true/*errno*/> ;
		static int mk_flags(const char* mode) {
			bool a = false ;
			bool c = false ;
			bool p = false ;
			bool r = false ;
			bool w = false ;
			for( const char* m=mode ; *m && *m!=',' ; m++ )                    // after a ',', there is a css=xxx which we do not care about
				switch (*m) {
					case 'a' : a = true ; break ;
					case 'c' : c = true ; break ;
					case '+' : p = true ; break ;
					case 'r' : r = true ; break ;
					case 'w' : w = true ; break ;
					default : ;
				}
			if (a+r+w!=1) return O_PATH ;                                                         // error case   , no access
			if (c       ) return O_PATH ;                                                         // gnu extension, no access
			/**/          return ( p ? O_RDWR : r ? O_RDONLY : O_WRONLY ) | ( w ? O_TRUNC : 0 ) ; // normal posix
		}
		Fopen( const char* path , const char* mode , ::string const& comment="fopen" ) : Base{ AT_FDCWD , path , mk_flags(mode) , to_string(comment,'.',mode) } {}
		FILE* operator()(FILE* fp) {
			Base::operator()( true/*has_fd*/ , fp?::fileno(fp):-1 ) ;
			return fp ;
		}
	} ;
	// services
	// protect agains recursive calls as Record does accesses which are routed back to us
	// ctx is useless when ld_audit, hence we have to say maybe_unused
	static void solve( int at , const char* file , bool no_follow=false , ::string const& c={}     ) { Ctx ctx [[maybe_unused]] ; if (!Lock::s_busy()) _t_record().solve(at,file,no_follow,c) ; }
	static void stat ( int at , const char* file , bool no_follow=false , ::string const& c={}     ) { Ctx ctx [[maybe_unused]] ; if (!Lock::s_busy()) _t_record().stat (at,file,no_follow,c) ; }
	static void read ( int at , const char* file , bool no_follow=false , ::string const& c="read" ) { Ctx ctx [[maybe_unused]] ; if (!Lock::s_busy()) _t_record().read (at,file,no_follow,c) ; }
	static void exec ( int at , const char* file , bool no_follow=false , ::string const& c="exec" ) { Ctx ctx [[maybe_unused]] ; if (!Lock::s_busy()) _t_record().exec (at,file,no_follow,c) ; }
	//
	static void hide      ( int fd                ) ;                          // note that fd           is  closed or about to be closed
	static void hide_range( int min , int max=~0u ) ;                          // note that min<=fd<=max are closed or about to be closed
private :
	// dont put data in static, although class is a singleton, so that we can more easily control when they are constructed
	::string _service ;
} ;
