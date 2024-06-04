// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

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
	SymEntry( void* f , bool is=false ) : func{f} , is_stat{is} {}
	void*         func    = nullptr ;
	bool          is_stat = false   ;
	mutable void* orig    = nullptr ;
} ;
extern ::umap_s<SymEntry> const* const g_syscall_tab ;

void* get_orig(const char* syscall) {
	if (!g_libc_name) exit(Rc::Usage,"cannot use autodep method ld_audit or ld_preload with statically linked libc") ;
	SymEntry const& entry = g_syscall_tab->at(syscall) ;
	if (!entry.orig) entry.orig = ::dlsym( RTLD_NEXT , syscall ) ; // may be not initialized if syscall was routed to another syscall
	return entry.orig ;
}

void load_exec(::string const& /*file*/) {} // the auditing mechanism tells us about indirectly loaded libraries

// elf dependencies are capture by auditing code, no need to interpret elf content
void         elf_deps  ( Record& /*r*/ , Record::Solve const& , const char* /*ld_library_path*/ , ::string&& /*comment*/="elf_dep"  ) {             }
Record::Read search_elf( Record& /*r*/ , const char* /*file*/ ,                                   ::string&& /*comment*/="elf_srch" ) { return {} ; }

static bool started() { return true ; }

#define LD_AUDIT 1
#include "ld_common.x.cc"

::umap_s<SymEntry> const* const g_syscall_tab = new ::umap_s<SymEntry>{
	{ "chdir"               , { reinterpret_cast<void*>(Audited::chdir               ) } }
,	{ "chmod"               , { reinterpret_cast<void*>(Audited::chmod               ) } }
,	{ "close"               , { reinterpret_cast<void*>(Audited::close               ) } }
,	{ "__close"             , { reinterpret_cast<void*>(Audited::__close             ) } }
,	{ "creat"               , { reinterpret_cast<void*>(Audited::creat               ) } }
,	{ "creat64"             , { reinterpret_cast<void*>(Audited::creat64             ) } }
//,	{ "dlmopen"             , { reinterpret_cast<void*>(Audited::dlmopen             ) } } // handled by la_objopen (calling Audited::dlmopen does not work for a mysterious reason)
//,	{ "dlopen"              , { reinterpret_cast<void*>(Audited::dlopen              ) } } // .
,	{ "dup2"                , { reinterpret_cast<void*>(Audited::dup2                ) } }
,	{ "dup3"                , { reinterpret_cast<void*>(Audited::dup3                ) } }
,	{ "execl"               , { reinterpret_cast<void*>(Audited::execl               ) } }
,	{ "execle"              , { reinterpret_cast<void*>(Audited::execle              ) } }
,	{ "execlp"              , { reinterpret_cast<void*>(Audited::execlp              ) } }
,	{ "execv"               , { reinterpret_cast<void*>(Audited::execv               ) } }
,	{ "execve"              , { reinterpret_cast<void*>(Audited::execve              ) } }
,	{ "execveat"            , { reinterpret_cast<void*>(Audited::execveat            ) } }
,	{ "execvp"              , { reinterpret_cast<void*>(Audited::execvp              ) } }
,	{ "execvpe"             , { reinterpret_cast<void*>(Audited::execvpe             ) } }
,	{ "fchdir"              , { reinterpret_cast<void*>(Audited::fchdir              ) } }
,	{ "fchmodat"            , { reinterpret_cast<void*>(Audited::fchmodat            ) } }
,	{ "fopen"               , { reinterpret_cast<void*>(Audited::fopen               ) } }
,	{ "fopen64"             , { reinterpret_cast<void*>(Audited::fopen64             ) } }
,	{ "fork"                , { reinterpret_cast<void*>(Audited::fork                ) } }
,	{ "__fork"              , { reinterpret_cast<void*>(Audited::__fork              ) } }
,	{ "freopen"             , { reinterpret_cast<void*>(Audited::freopen             ) } }
,	{ "freopen64"           , { reinterpret_cast<void*>(Audited::freopen64           ) } }
,	{ "futimesat"           , { reinterpret_cast<void*>(Audited::futimesat           ) } }
,	{ "getcwd"              , { reinterpret_cast<void*>(Audited::getcwd              ) } } // necessary when tmp_view is not empty to map back to job view
,	{ "getwd"               , { reinterpret_cast<void*>(Audited::getwd               ) } } // .
,	{ "get_current_dir_name", { reinterpret_cast<void*>(Audited::get_current_dir_name) } } // .
,	{ "__libc_fork"         , { reinterpret_cast<void*>(Audited::__libc_fork         ) } }
,	{ "link"                , { reinterpret_cast<void*>(Audited::link                ) } }
,	{ "linkat"              , { reinterpret_cast<void*>(Audited::linkat              ) } }
,	{ "lutimes"             , { reinterpret_cast<void*>(Audited::lutimes             ) } }
,	{ "mkdir"               , { reinterpret_cast<void*>(Audited::mkdir               ) } } // necessary against NFS strange notion of coherence as this touches containing dir
,	{ "mkostemp"            , { reinterpret_cast<void*>(Audited::mkostemp            ) } }
,	{ "mkostemp64"          , { reinterpret_cast<void*>(Audited::mkostemp64          ) } }
,	{ "mkostemps"           , { reinterpret_cast<void*>(Audited::mkostemps           ) } }
,	{ "mkostemps64"         , { reinterpret_cast<void*>(Audited::mkostemps64         ) } }
,	{ "mkstemp"             , { reinterpret_cast<void*>(Audited::mkstemp             ) } }
,	{ "mkstemp64"           , { reinterpret_cast<void*>(Audited::mkstemp64           ) } }
,	{ "mkstemps"            , { reinterpret_cast<void*>(Audited::mkstemps            ) } }
,	{ "mkstemps64"          , { reinterpret_cast<void*>(Audited::mkstemps64          ) } }
,	{ "open"                , { reinterpret_cast<void*>(Audited::open                ) } }
,	{ "__open"              , { reinterpret_cast<void*>(Audited::__open              ) } }
,	{ "__open_nocancel"     , { reinterpret_cast<void*>(Audited::__open_nocancel     ) } }
,	{ "__open_2"            , { reinterpret_cast<void*>(Audited::__open_2            ) } }
,	{ "open64"              , { reinterpret_cast<void*>(Audited::open64              ) } }
,	{ "__open64"            , { reinterpret_cast<void*>(Audited::__open64            ) } }
,	{ "__open64_nocancel"   , { reinterpret_cast<void*>(Audited::__open64_nocancel   ) } }
,	{ "__open64_2"          , { reinterpret_cast<void*>(Audited::__open64_2          ) } }
,	{ "openat"              , { reinterpret_cast<void*>(Audited::openat              ) } }
,	{ "__openat_2"          , { reinterpret_cast<void*>(Audited::__openat_2          ) } }
,	{ "openat64"            , { reinterpret_cast<void*>(Audited::openat64            ) } }
,	{ "__openat64_2"        , { reinterpret_cast<void*>(Audited::__openat64_2        ) } }
,	{ "readlink"            , { reinterpret_cast<void*>(Audited::readlink            ) } }
,	{ "readlinkat"          , { reinterpret_cast<void*>(Audited::readlinkat          ) } }
,	{ "__readlinkat_chk"    , { reinterpret_cast<void*>(Audited::__readlinkat_chk    ) } }
,	{ "__readlink_chk"      , { reinterpret_cast<void*>(Audited::__readlink_chk      ) } }
,	{ "rename"              , { reinterpret_cast<void*>(Audited::rename              ) } }
,	{ "renameat"            , { reinterpret_cast<void*>(Audited::renameat            ) } }
,	{ "renameat2"           , { reinterpret_cast<void*>(Audited::renameat2           ) } }
,	{ "rmdir"               , { reinterpret_cast<void*>(Audited::rmdir               ) } } // necessary against NFS strange notion of coherence as this touches containing dir
,	{ "symlink"             , { reinterpret_cast<void*>(Audited::symlink             ) } }
,	{ "symlinkat"           , { reinterpret_cast<void*>(Audited::symlinkat           ) } }
,	{ "syscall"             , { reinterpret_cast<void*>(Audited::syscall             ) } }
,	{ "system"              , { reinterpret_cast<void*>(Audited::system              ) } }
,	{ "truncate"            , { reinterpret_cast<void*>(Audited::truncate            ) } }
,	{ "truncate64"          , { reinterpret_cast<void*>(Audited::truncate64          ) } }
,	{ "unlink"              , { reinterpret_cast<void*>(Audited::unlink              ) } }
,	{ "unlinkat"            , { reinterpret_cast<void*>(Audited::unlinkat            ) } }
,	{ "utime"               , { reinterpret_cast<void*>(Audited::utime               ) } }
,	{ "utimensat"           , { reinterpret_cast<void*>(Audited::utimensat           ) } }
,	{ "utimes"              , { reinterpret_cast<void*>(Audited::utimes              ) } }
,	{ "vfork"               , { reinterpret_cast<void*>(Audited::vfork               ) } } // because vfork semantic does not allow instrumentation of following exec
,	{ "__vfork"             , { reinterpret_cast<void*>(Audited::__vfork             ) } } // .
//
// mere path accesses, no actual accesses to file data
//
,	{ "access"    , { reinterpret_cast<void*>(Audited::access   ) , true } }
,	{ "faccessat" , { reinterpret_cast<void*>(Audited::faccessat) , true } }
,	{ "opendir"   , { reinterpret_cast<void*>(Audited::opendir  ) , true } }
,	{ "mkdirat"   , { reinterpret_cast<void*>(Audited::mkdirat  ) , true } }
,	{ "statx"     , { reinterpret_cast<void*>(Audited::statx    ) , true } }
//
,	{ "__xstat"      , { reinterpret_cast<void*>(Audited::__xstat     ) , true } }
,	{ "__xstat64"    , { reinterpret_cast<void*>(Audited::__xstat64   ) , true } }
,	{ "__lxstat"     , { reinterpret_cast<void*>(Audited::__lxstat    ) , true } }
,	{ "__lxstat64"   , { reinterpret_cast<void*>(Audited::__lxstat64  ) , true } }
,	{ "__fxstatat"   , { reinterpret_cast<void*>(Audited::__fxstatat  ) , true } }
,	{ "__fxstatat64" , { reinterpret_cast<void*>(Audited::__fxstatat64) , true } }
#if !NEED_STAT_WRAPPERS
	,	{ "stat"      , { reinterpret_cast<void*>(Audited::stat     ) , true } }
	,	{ "stat64"    , { reinterpret_cast<void*>(Audited::stat64   ) , true } }
	,	{ "lstat"     , { reinterpret_cast<void*>(Audited::lstat    ) , true } }
	,	{ "lstat64"   , { reinterpret_cast<void*>(Audited::lstat64  ) , true } }
	,	{ "fstatat"   , { reinterpret_cast<void*>(Audited::fstatat  ) , true } }
	,	{ "fstatat64" , { reinterpret_cast<void*>(Audited::fstatat64) , true } }
#endif
,	{ "realpath"               , { reinterpret_cast<void*>(Audited::realpath              ) , true } }
,	{ "__realpath_chk"         , { reinterpret_cast<void*>(Audited::__realpath_chk        ) , true } }
,	{ "canonicalize_file_name" , { reinterpret_cast<void*>(Audited::canonicalize_file_name) , true } }
,	{ "scandir"                , { reinterpret_cast<void*>(Audited::scandir               ) , true } }
,	{ "scandir64"              , { reinterpret_cast<void*>(Audited::scandir64             ) , true } }
,	{ "scandirat"              , { reinterpret_cast<void*>(Audited::scandirat             ) , true } }
,	{ "scandirat64"            , { reinterpret_cast<void*>(Audited::scandirat64           ) , true } }
} ;

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
	auditor() ;                     // force Audit static init
	if (g_force_orig) goto Ignore ; // avoid recursion loop
	if (*def_cook   ) goto Ignore ; // cookie is used to identify libc (when cookie==0)
	//
	{	auto            it    = g_syscall_tab->find(sym_name) ; if ( it==g_syscall_tab->end()                             ) goto Ignore ;
		SymEntry const& entry = it->second                    ; if ( Record::s_autodep_env().ignore_stat && entry.is_stat ) goto Ignore ;
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
		auditor() ;                                                                                                                 // force Audit static init
		if ( !map->l_name || !*map->l_name ) {
			*cookie = true/*not_std*/ ;
			return LA_FLG_BINDFROM ;
		}
		if (!::string_view(map->l_name).starts_with("linux-vdso.so"))                                                               // linux-vdso.so is listed, but is not a real file
			Read(static_cast<const char*>(map->l_name),false/*no_follow*/,false/*keep_real*/,false/*allow_tmp_map*/,"la_objopen") ;
		::pair<bool/*is_std*/,bool/*is_libc*/> known = _catch_std_lib(map->l_name) ;
		*cookie = !known.first ;
		if (known.second) {
			if (lmid!=LM_ID_BASE) exit(Rc::Usage,"new namespaces not supported for libc") ; // need to find a way to gather the actual map, because here we just get LM_ID_NEWLM
			g_libc_name = map->l_name ;
		}
		return LA_FLG_BINDFROM | (known.first?LA_FLG_BINDTO:0) ;
	}

	char* la_objsearch( const char* name , uintptr_t* /*cookie*/ , unsigned int flag ) {
		switch (flag) {
			case LA_SER_ORIG    : if (strrchr(name,'/')) Read(name,false/*no_follow*/,false/*keep_real*/,false/*allow_tmp_map*/,"la_objsearch") ; break ;
			case LA_SER_LIBPATH :
			case LA_SER_RUNPATH :                        Read(name,false/*no_follow*/,false/*keep_real*/,false/*allow_tmp_map*/,"la_objsearch") ; break ;
			default : ;
		}
		return const_cast<char*>(name) ;
	}

	uintptr_t la_symbind64(Elf64_Sym* s,unsigned int n,uintptr_t* rc,uintptr_t* dc,unsigned int* f,const char* sn) { return _la_symbind(s,n,rc,dc,f,sn) ; }
	uintptr_t la_symbind32(Elf32_Sym* s,unsigned int n,uintptr_t* rc,uintptr_t* dc,unsigned int* f,const char* sn) { return _la_symbind(s,n,rc,dc,f,sn) ; }

}
#pragma GCC visibility pop
