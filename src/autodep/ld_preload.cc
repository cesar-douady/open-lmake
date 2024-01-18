// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <dlfcn.h>
#include <link.h>   // dl related stuff

#include "disk.hh"

#include "record.hh"

using namespace Disk ;

struct Ctx {
	// cxtors & casts
	Ctx               () { save_errno   () ; }
	~Ctx              () { restore_errno() ; }
	// services
	void save_errno   () { errno_ = errno  ; }
	void restore_errno() { errno  = errno_ ; }
	// data
	int errno_ ;
} ;

struct Lock {
	// s_mutex prevents several threads from recording deps simultaneously
	// t_loop prevents recursion within a thread :
	//   if a thread does access while processing an original access, this second access must not be recorded, it is for us, not for the user
	//   in that case, Lock let it go, but then, t_busy will return true, which in turn will prevent recording
	// t_loop must be thread local so as to distinguish which thread owns the mutex. Values can be :
	// - 0 : thread is outside and must acquire the mutex to enter
	// - 1 : thread is processing a user access and must record deps
	// - 2 : thread has entered a recursive call and must not record deps
	// statics
	static bool t_busy() { return t_loop ; }                                  // same, before taking a 2nd lock
	// static data
	static              ::mutex s_mutex ;
	static thread_local bool    t_loop  ;
	// cxtors & casts
	Lock () { SWEAR(!t_loop) ; t_loop = true  ; s_mutex.lock  () ; }
	~Lock() { SWEAR( t_loop) ; t_loop = false ; s_mutex.unlock() ; }
} ;
::mutex           Lock::s_mutex ;
bool thread_local Lock::t_loop  = false ;

void* get_orig(const char* syscall) {
	void* res = ::dlsym(RTLD_NEXT,syscall) ;                                   // with CentOS-7, dlopen is in libdl, not in libc, but we want to track it
	swear_prod(res,"cannot find symbol ",syscall," in libc") ;
	return res ;
}

void load_exec(::string const& file) {
	(void)file ;
}

//
// Elf
//

// elf is interpreted before exec and ldopen to find indirect dependencies
// this cannot be done by examining objects after they are loaded as we need to know files that have been tried before the ones that finally were loaded

using Ehdr = ElfW(Ehdr) ;
using Phdr = ElfW(Phdr) ;
using Shdr = ElfW(Shdr) ;
using Dyn  = ElfW(Dyn ) ;

// capture LD_LIBRARY_PATH when first called (must be called at program start, or at least before any environment modif) as man dlopen says so
const char* get_ld_library_path() {
	static ::string llp = get_env("LD_LIBRARY_PATH") ;
	return llp.c_str() ;
}

struct DynDigest {
	// statics
private :
	template<class T> static T const& _s_vma_to_ref( size_t vma , FileMap const& file_map=FileMap() ) {
		if (!file_map) return *reinterpret_cast<const T*>(vma) ;
		Ehdr const& ehdr = file_map.get<Ehdr>() ;
		for( size_t i=0 ; i<ehdr.e_phnum ; i++ ) {
			Phdr const& phdr = file_map.get<Phdr>( ehdr.e_phoff + i*ehdr.e_phentsize ) ;
			if ( phdr.p_type!=PT_LOAD                                                  ) continue ;
			if ( vma<(phdr.p_vaddr&-phdr.p_align) || vma>=(phdr.p_vaddr+phdr.p_filesz) ) continue ;
			return file_map.get<T>( vma - phdr.p_vaddr + phdr.p_offset ) ;
		}
		throw "cannot find address"s ;
	}
	Dyn const* _s_search_dyn_tab(FileMap const& file_map) {
		static constexpr bool Is64Bits = sizeof(void*)==8 ;
		//
		Ehdr const& ehdr       = file_map.get<Ehdr>() ;
		size_t      dyn_offset = 0/*garbage*/         ;
		//
		if ( file_map.sz                           <  sizeof(Ehdr)                      ) throw "file too small"s ;
		if ( memcmp(ehdr.e_ident,ELFMAG,SELFMAG)   != 0                                 ) throw "bad header"s     ;
		if ( (ehdr.e_ident[EI_CLASS]==ELFCLASS64 ) != Is64Bits                          ) throw "bad word width"s ;
		if ( (ehdr.e_ident[EI_DATA ]==ELFDATA2MSB) != (::endian::native==::endian::big) ) throw "bad endianness"s ;
		//
		for( size_t i=0 ; i<ehdr.e_phnum ; i++ ) {
			size_t      phdr_offset = ehdr.e_phoff + i*ehdr.e_phentsize ;
			Phdr const& phdr        = file_map.get<Phdr>(phdr_offset)   ;
			if (phdr.p_type==PT_DYNAMIC) {
				dyn_offset = phdr.p_offset ;
				goto DoSection ;
			}
		}
		return {} ; // no dynamic header
	DoSection :
		size_t string_shdr_offset = ehdr.e_shoff + ehdr.e_shstrndx*ehdr.e_shentsize  ;
		size_t string_offset      = file_map.get<Shdr>(string_shdr_offset).sh_offset ;
		//
		for( size_t i=0 ; i<ehdr.e_shnum ; i++ ) {
			size_t      shdr_offset  = ehdr.e_shoff + i*ehdr.e_shentsize ;
			Shdr const& shdr         = file_map.get<Shdr>(shdr_offset)   ;
			size_t      shdr_name    = string_offset + shdr.sh_name      ;
			const char* section_name = &file_map.get<char>(shdr_name)    ;
			if (section_name==".dynamic"s) {
				dyn_offset = shdr.sh_offset ;
			}
		}
		return &file_map.get<Dyn>(dyn_offset) ;
	}
	::pair<const char*,size_t> _s_str_tab( Dyn const* dyn_tab , FileMap const& file_map=FileMap() ) {
		const char* str_tab = nullptr ;
		size_t      sz      = 0       ;
		for( Dyn const* dyn=dyn_tab ; dyn->d_tag!=DT_NULL ; dyn++ ) {
			if ( +file_map && dyn>&file_map.get<Dyn>(file_map.sz-sizeof(Dyn)) ) throw to_string("dyn tab outside file ",dyn," ",&file_map.get<Dyn>(file_map.sz)) ;
			switch (dyn->d_tag) {
				case DT_STRTAB : str_tab = &_s_vma_to_ref<char>(dyn->d_un.d_ptr,file_map) ; break ;
				case DT_STRSZ  : sz      =                      dyn->d_un.d_val           ; break ; // for swear only
				default : continue ;
			}
			if ( str_tab && sz ) {
				if ( +file_map && str_tab+sz-1>&file_map.get<char>(file_map.sz-1) ) throw "str tab too long"s ;
				return {str_tab,sz} ;
			}
		}
		throw "cannot find dyn string table"s ;
	}
	// cxtors & casts
public :
	DynDigest( Dyn const* dyn_tab , FileMap const& file_map = FileMap() ) {
		auto        [str_tab,str_sz] = _s_str_tab(dyn_tab,file_map) ;
		const char* str_tab_end      = str_tab + str_sz             ;
		for( Dyn const* dyn=dyn_tab ; dyn->d_tag!=DT_NULL ; dyn++ ) {
			const char* s = str_tab + dyn->d_un.d_val ;
			switch (dyn->d_tag) {
				case DT_RPATH   : SWEAR(after!=No ) ; if (after!=Yes) { rpath =           s  ; after = No  ; } break ; // DT_RUNPATH has priority over RPATH
				case DT_RUNPATH : SWEAR(after!=Yes) ;                   rpath =           s  ; after = Yes ;   break ; // .
				case DT_NEEDED  :                     if (*s        )   neededs.push_back(s) ;                 break ;
				default : continue ;
			}
			if (s>=str_tab_end) throw "dyn entry name outside string tab"s ;
		}
	}
	DynDigest(FileMap const& file_map) : DynDigest{ _s_search_dyn_tab(file_map) , file_map } {}
	// data
	::vector<const char*> neededs ;
	const char*           rpath   = 0     ;
	Bool3                 after   = Maybe ;
} ;

static void _elf_deps( Record& r , ::string const& real , const char* ld_library_path , ::umap_s<Bool3/*exists*/>& seen , ::string&& comment="elf_dep" ) ;

[[noreturn]] static inline void _panic( Record& r , ::string const& err ) {
	r.report_panic("while processing elf executable :",err) ;
}
static Record::Read _search_elf( Record& r , const char* file , const char* ld_library_path , ::umap_s<Bool3/*exists*/>& seen , ::string&& comment="elf_srch" ) {
	if ( !file || !*file ) return {} ;
	if (strchr(file,'/')) {
		if (!seen.try_emplace(file,Maybe).second) return {} ;
		::string     dc  = comment+".dep" ;
		Record::Read res ;
		try {
			res = { r , file , false/*no_follow*/ , true/*keep_real*/ , false/*allow_tmp_map*/ , ::move(comment) } ;
		} catch (::string const& e) { _panic(r,e) ; }                                                                // if tmp mapping is used, dlopen/exec will not work ...
		_elf_deps( r , res.real , ld_library_path , seen , ::move(dc) ) ;                                            // ... there is nothing we can do about it
		return res ;
	}
	//
	::string path ; if (ld_library_path) path = ld_library_path ;
	//
	if ( void* main = ::dlopen(nullptr,RTLD_NOW|RTLD_NOLOAD) ) {
		::link_map* lm = nullptr/*garbage*/ ;
		if ( int rc=::dlinfo(main,RTLD_DI_LINKMAP,&lm) ; rc==0 ) {
			if ( DynDigest digest{lm->l_ld} ; digest.after!=Maybe && digest.rpath[0] ) {
				if      (!path            ) path =                    digest.rpath           ;
				else if (digest.after==Yes) path = to_string(path,':',digest.rpath         ) ;
				else if (digest.after==No ) path = to_string(         digest.rpath,':',path) ;
				else                        FAIL(digest.after) ;
			}
		}
	}
	if (+path) path += ':' ;
	path += "/lib:/usr/lib" ;
	for( size_t pos=0 ;;) {
		size_t end = path.find(':',pos) ;
		::string        full_file  = path.substr(pos,end-pos) ;
		if (+full_file) full_file += '/'                      ;
		/**/            full_file += file                     ;
		try {
			Record::Read rr { r , full_file , false/*no_follow*/ , true/*keep_real*/ , false/*allow_tmp_map*/ , ::copy(comment) } ;
			auto [it,inserted] = seen.try_emplace(rr.real,Maybe) ;
			if ( it->second==Maybe           )   it->second = No | is_target(Record::s_root_fd(),rr.real,false/*no_follow*/) ;      // real may be a sym link in the system directories
			if ( it->second==Yes && inserted ) { _elf_deps( r , rr.real , ld_library_path , seen , comment+".dep" ) ; return rr ; }
			if ( it->second==Yes             )                                                                        return {} ;
		} catch (::string const& e) { _panic(r,e) ; }              // if tmp mapping is used, dlopen/exec will not work, there is nothing we can do about it
		if (end==Npos) break ;
		pos = end+1 ;
	}
	return {} ;
}
Record::Read search_elf( Record& r , const char* file , ::string&& comment="elf_srch" ) {
	try {
		::umap_s<Bool3/*exists*/> seen ;
		Record::Read res = _search_elf( r , file , get_ld_library_path() , seen , ::move(comment) ) ; // man dlopen says LD_LIBRARY_PATH must be captured at program start time
		return res ;
	} catch (::string const& e) { return {} ; }                                                       // in case of elf analysis error, stop analysis, dlopen will report an error to user
}

static void _elf_deps( Record& r , ::string const& real , const char* ld_library_path , ::umap_s<Bool3/*exists*/>& seen , ::string&& comment ) {
	FileMap    file_map { Record::s_root_fd() , real } ; if (!file_map) return ;                                                                     // real does not exist, no deps
	DynDigest  digest   { file_map                   } ;
	for( const char* needed : digest.neededs ) _search_elf( r , needed , ld_library_path , seen , ::copy(comment) ) ;
}
void elf_deps( Record& r , ::string const& real , const char* ld_library_path , ::string&& comment="elf_dep" ) {
	try {
		::umap_s<Bool3/*exists*/> seen ;
		_elf_deps( r , real , ld_library_path , seen , ::move(comment) ) ;
	} catch (::string const& e) {}                                         // in case of elf analysis error, stop analysis, dlopen will report an error to user
}

#define LD_PRELOAD 1
#include "ld.cc"
