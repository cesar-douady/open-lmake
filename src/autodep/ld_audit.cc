// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#define LD_AUDIT 1

#include <link.h> // all dynamic link related

#include "record.hh"

bool        g_force_orig = false   ;
const char* g_libc_name  = nullptr ;

void* get_libc_handle() {
	static void* s_libc_handle = ::dlmopen( LM_ID_BASE , g_libc_name , RTLD_NOW|RTLD_NOLOAD ) ;
	swear_prod(s_libc_handle,"cannot find libc") ;
	return s_libc_handle ;
}

struct Ctx {
	void save_errno   () {} // our errno is not the same as user errno, so nothing to do
	void restore_errno() {} // .
} ;

struct SymEntry {
	SymEntry( void* f , LnkSupport ls=LnkSupport::None ) : func{f} , lnk_support{ls} {}
	void*         func        = nullptr          ;
	LnkSupport    lnk_support = LnkSupport::None ; // above this level of link support, we need to catch this syscall
	mutable void* orig        = nullptr          ;
} ;
extern ::umap_s<SymEntry> const* const g_syscall_tab ;

void* get_orig(const char* syscall) {
	if (!g_libc_name) exit(Rc::Usage,"cannot use autodep method ld_audit or ld_preload with statically linked libc") ;
	SymEntry const& entry = g_syscall_tab->at(syscall) ;
	if (!entry.orig) entry.orig = ::dlsym( RTLD_NEXT , syscall ) ; // may be not initialized if syscall was routed to another syscall
	return entry.orig ;
}

void load_exec(::string const& /*file*/) {} // the auditing mechanism tells us about indirectly loaded libraries

// elf dependencies are captured by auditing code, no need to interpret elf content
void         elf_deps  ( Record& , Record::SolveCS const& , const char* /*ld_library_path*/ , ::string&& /*comment*/="elf_dep"  ) {             }
Record::Read search_elf( Record& , const char* /*file*/   ,                                   ::string&& /*comment*/="elf_srch" ) { return {} ; }

static bool started() { return true ; }

#include "ld_common.x.cc"

#include "syscall_tab.hh"

#define SYSCALL_ENTRY(syscall) { #syscall , { reinterpret_cast<void*>(Audited::syscall) } }
::umap_s<SymEntry> const* const g_syscall_tab = new ::umap_s<SymEntry>{ ENUMERATE_SYSCALLS } ;

static ::pair<bool/*is_std*/,bool/*is_libc*/> _catch_std_lib(const char* c_name) {
	// search for string (.*/)?libc.so(.<number>)*
	static const char LibC      [] = "libc.so"       ;
	static const char LibPthread[] = "libpthread.so" ; // some systems redefine entries such as open in libpthread
	//
	bool          is_libc = false/*garbage*/       ;
	::string_view name    { c_name }               ;
	size_t        end     = 0/*garbage*/           ;
	size_t        pos     = name.rfind(LibC      ) ; if (pos!=Npos) { end = pos+sizeof(LibC      )-1 ; is_libc = true  ; goto Qualify ; }
	/**/          pos     = name.rfind(LibPthread) ; if (pos!=Npos) { end = pos+sizeof(LibPthread)-1 ; is_libc = false ; goto Qualify ; }
	return {false,false} ;
Qualify :
	/**/                             if ( pos!=0 && name[pos-1]!='/' ) return {false,false  } ;
	for( char c : name.substr(end) ) if ( (c<'0'||c>'9') && c!='.'   ) return {false,false  } ;
	/**/                                                               return {true ,is_libc} ;
}

template<class Sym> uintptr_t _la_symbind( Sym* sym , unsigned int /*ndx*/ , uintptr_t* /*ref_cook*/ , uintptr_t* def_cook , unsigned int* /*flags*/ , const char* sym_name ) {
	//
	if (g_force_orig) goto Ignore ;                                               // avoid recursion loop
	if (*def_cook   ) goto Ignore ;                                               // cookie is used to identify libc (when cookie==0)
	//
	{	auto it = g_syscall_tab->find(sym_name) ;
		if (it==g_syscall_tab->end()) goto Ignore ;
		//
		auditer() ;                                                               // force Audit static init
		SymEntry const& entry = it->second ;
		if ( Record::s_autodep_env().lnk_support>=entry.lnk_support) goto Catch ;
		if (!Record::s_autodep_env().ignore_stat                   ) goto Catch ; // we need to generate deps for stat-like accesses
		goto Ignore ;
	Catch :
		entry.orig = reinterpret_cast<void*>(sym->st_value) ;
		return reinterpret_cast<uintptr_t>(entry.func) ;
	}
Ignore :
	return sym->st_value ;
}

#pragma GCC visibility push(default)
extern "C" {

	unsigned int la_version(unsigned int /*version*/) {
		return LAV_CURRENT ;
	}

	unsigned int la_objopen( struct link_map* map , Lmid_t lmid , uintptr_t *cookie ) {
		const char* nm = map->l_name ;
		if ( !nm || !*nm ) {
			*cookie = true/*not_std*/ ;
			return LA_FLG_BINDFROM ;
		}
		if (!::string_view(nm).starts_with("linux-vdso.so"))                                // linux-vdso.so is listed, but is not a real file
			ReadCS(nm,false/*no_follow*/,false/*keep_real*/,"la_objopen") ;
		::pair<bool/*is_std*/,bool/*is_libc*/> known = _catch_std_lib(nm) ;
		*cookie = !known.first ;
		if (known.second) {
			if (lmid!=LM_ID_BASE) exit(Rc::Usage,"new namespaces not supported for libc") ; // need to find a way to gather the actual map, because here we just get LM_ID_NEWLM
			g_libc_name = nm ;
		}
		return LA_FLG_BINDFROM | (known.first?LA_FLG_BINDTO:0) ;
	}

	char* la_objsearch( const char* name , uintptr_t* /*cookie*/ , unsigned int flag ) {
		switch (flag) {
			case LA_SER_ORIG    : if (strrchr(name,'/')) ReadCS(name,false/*no_follow*/,false/*keep_real*/,"la_objsearch") ; break ;
			case LA_SER_LIBPATH :
			case LA_SER_RUNPATH :                        ReadCS(name,false/*no_follow*/,false/*keep_real*/,"la_objsearch") ; break ;
			default : ;
		}
		return const_cast<char*>(name) ;
	}

	uintptr_t la_symbind64(Elf64_Sym* s,unsigned int n,uintptr_t* rc,uintptr_t* dc,unsigned int* f,const char* sn) { return _la_symbind(s,n,rc,dc,f,sn) ; }
	uintptr_t la_symbind32(Elf32_Sym* s,unsigned int n,uintptr_t* rc,uintptr_t* dc,unsigned int* f,const char* sn) { return _la_symbind(s,n,rc,dc,f,sn) ; }

}
#pragma GCC visibility pop
