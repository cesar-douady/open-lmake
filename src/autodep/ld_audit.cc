// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#define LD_AUDIT 1

#include <link.h> // all dynamic link related

#include "record.hh"

bool        g_force_orig = false   ;
const char* g_libc_name  = nullptr ;

struct SymbolEntry {
	SymbolEntry( void* f , bool is=false ) : func{f} , is_stat{is} {}
	void*         func    = nullptr ;
	bool          is_stat = false   ;
	mutable void* orig    = nullptr ;
} ;
static ::umap_s<SymbolEntry> const* _g_libcall_tab = nullptr ;

void* get_orig(const char* libcall) {
	if (!g_libc_name) exit(Rc::Usage,"cannot use autodep method ld_audit or ld_preload with statically linked libc") ;
	SymbolEntry const& entry = _g_libcall_tab->at(libcall) ;
	if (!entry.orig) entry.orig = ::dlsym( RTLD_NEXT , libcall ) ; // may be not initialized if a libcall is routed to another libcall
	return entry.orig ;
}

static bool started() { return true ; }

#include "ld_common.x.cc"

#include "syscall_tab.hh"

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

template<class Sym> uintptr_t _la_symbind( Sym* sym , uint /*ndx*/ , uintptr_t* /*ref_cook*/ , uintptr_t* def_cook , uint* /*flags*/ , const char* sym_name ) {
	//
	auditor() ;                     // force Audit static init
	if (g_force_orig) goto Ignore ; // avoid recursion loop
	if (*def_cook   ) goto Ignore ; // cookie is used to identify libc (when cookie==0)
	//
	{	auto               it    = _g_libcall_tab->find(sym_name) ; if ( it==_g_libcall_tab->end()                            ) goto Ignore ;
		SymbolEntry const& entry = it->second                     ; if ( Record::s_autodep_env().ignore_stat && entry.is_stat ) goto Ignore ;
		entry.orig = reinterpret_cast<void*>(sym->st_value) ;
		return reinterpret_cast<uintptr_t>(entry.func) ;
	}
Ignore :
	return sym->st_value ;
}

#pragma GCC visibility push(default)
extern "C" {

	uint la_version(uint /*version*/) {
		#define LIBCALL_ENTRY(libcall,is_stat) { #libcall , { reinterpret_cast<void*>(Audited::libcall) } }
		_g_libcall_tab = new ::umap_s<SymbolEntry>{ ENUMERATE_LIBCALLS } ;
		#undef LIBCALL_ENTRY
		return LAV_CURRENT ;
	}

	uint la_objopen( struct link_map* map , Lmid_t lmid , uintptr_t *cookie ) {
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

	char* la_objsearch( const char* name , uintptr_t* /*cookie*/ , uint flag ) {
		switch (flag) {
			case LA_SER_ORIG    : if (strrchr(name,'/')) ReadCS(name,false/*no_follow*/,false/*keep_real*/,"la_objsearch") ; break ;
			case LA_SER_LIBPATH :
			case LA_SER_RUNPATH :                        ReadCS(name,false/*no_follow*/,false/*keep_real*/,"la_objsearch") ; break ;
		DN}
		return const_cast<char*>(name) ;
	}

	uintptr_t la_symbind64(Elf64_Sym* s,uint n,uintptr_t* rc,uintptr_t* dc,uint* f,const char* sn) { return _la_symbind(s,n,rc,dc,f,sn) ; }
	uintptr_t la_symbind32(Elf32_Sym* s,uint n,uintptr_t* rc,uintptr_t* dc,uint* f,const char* sn) { return _la_symbind(s,n,rc,dc,f,sn) ; }

}
#pragma GCC visibility pop
