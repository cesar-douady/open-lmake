// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <sys/auxv.h> // getauxval

#include "disk.hh"
#include "rpc_job_exec.hh"

#include "record.hh"

using namespace Disk ;

//
// Elf
//

// elf is interpreted before exec and ldopen to find indirect dependencies
// this cannot be done by examining objects after they are loaded as we need to know files that have been tried before the ones that finally were loaded

struct Elf {
	using Ehdr = ElfW(Ehdr) ;
	using Phdr = ElfW(Phdr) ;
	using Shdr = ElfW(Shdr) ;
	using Dyn  = ElfW(Dyn ) ;

	static constexpr bool Is32Bits = sizeof(void*)==4 ;
	static constexpr bool Is64Bits = sizeof(void*)==8 ;
	static_assert(Is32Bits+Is64Bits==1) ;

	struct DynDigest {
		// statics
	private :
		template<class T> static T const&                   _s_vma_to_ref    ( size_t vma         , FileMap const& file_map={} ) ;
		/**/              static Dyn const*                 _s_search_dyn_tab(                      FileMap const& file_map    ) ;
		/**/              static ::pair<const char*,size_t> _s_str_tab       ( Dyn const* dyn_tab , FileMap const& file_map    ) ;
		// cxtors & casts
	public :
		DynDigest( Dyn const* dyn_tab , FileMap const& file_map={} ) ;
		DynDigest(                      FileMap const& file_map    ) : DynDigest{ _s_search_dyn_tab(file_map) , file_map } {}
		DynDigest( NewType ) {
			void*       main = ::dlopen(nullptr/*filename*/,RTLD_NOW|RTLD_NOLOAD) ; if (!main                                ) return ;
			::link_map* lm   = nullptr/*garbage*/                                 ; if (::dlinfo(main,RTLD_DI_LINKMAP,&lm)!=0) return ;
			self = DynDigest(lm->l_ld) ;
		}
		// data
		::vector<const char*> neededs ;
		const char*           rpath   = nullptr ;
		const char*           runpath = nullptr ;
	} ;

	// statics
	static ::string s_expand( const char* txt , ::string const& exe={} ) ;
	// cxtors & casts
	Elf( Record& r_ , ::string const& exe , const char* llp , const char* rp=nullptr ) : r{&r_} , ld_library_path{s_expand(llp,exe)} , rpath{s_expand(rp,exe)} {
		if (!llp) return ;
		::string const& root_s = Record::s_autodep_env().repo_root_s ; SWEAR(+root_s) ;     // root_s contains at least /
		size_t          sz     = root_s.size()-1 ;
		bool start = true ;
		for( const char* p=llp ; *p ; p++ ) {
			if (start) {
				if ( *p!='/'                                                     ) return ; // found a relative entry, most probably inside the repo
				if ( ::strncmp(p,root_s.c_str(),sz)==0 && (p[sz]==':'||p[sz]==0) ) return ; // found an absolute entry pointing inside the repo
				start = false ;
			} else {
				if (*p==':') start = true ;
			}
		}
		simple_llp = true ;
	}
	// services
	Record::Read<true/*Send*/> search_elf( ::string                     const& file , ::string const& runpath , Comment ) ;
	void                       elf_deps  ( Record::Solve<false/*Send*/> const&      , bool top                , Comment ) ;
	// data
	Record*                   r               = nullptr/*garbage*/ ;
	::string                  ld_library_path ;
	::string                  rpath           ;                                             // DT_RPATH or DT_RUNPATH entry
	::umap_s<Bool3/*exists*/> seen            = {}                 ;
	bool                      simple_llp      = false              ;                        // if true => ld_library_path contains no dir to the repo
} ;

inline Elf::Dyn const* Elf::DynDigest::_s_search_dyn_tab( FileMap const& file_map ) {
	if (file_map.sz<sizeof(Ehdr)) throw 2 ;                                                              // file too small : stop analysis and ignore
	//
	Ehdr const& ehdr       = file_map.get<Ehdr>() ;
	size_t      dyn_offset = 0/*garbage*/         ;
	//
	if ( ::memcmp(ehdr.e_ident,ELFMAG,SELFMAG)!=0                                            ) throw 3 ; // bad header     : .
	if ( ehdr.e_ident[EI_CLASS] != (Is64Bits                       ?ELFCLASS64 :ELFCLASS32 ) ) throw 4 ; // bad word width : .
	if ( ehdr.e_ident[EI_DATA ] != (::endian::native==::endian::big?ELFDATA2MSB:ELFDATA2LSB) ) throw 5 ; // bad endianness : .
	//
	for( size_t i : iota(ehdr.e_phnum) ) {
		size_t      phdr_offset = ehdr.e_phoff + i*ehdr.e_phentsize ;
		Phdr const& phdr        = file_map.get<Phdr>(phdr_offset)   ;
		if (phdr.p_type==PT_DYNAMIC) {
			dyn_offset = phdr.p_offset ;
			goto DoSection ;
		}
	}
	return {} ;                                                                                          // no dynamic header
DoSection :
	size_t string_shdr_offset = ehdr.e_shoff + ehdr.e_shstrndx*ehdr.e_shentsize  ;
	size_t string_offset      = file_map.get<Shdr>(string_shdr_offset).sh_offset ;
	//
	for( size_t i : iota(ehdr.e_shnum) ) {
		size_t      shdr_offset  = ehdr.e_shoff + i*ehdr.e_shentsize ;
		Shdr const& shdr         = file_map.get<Shdr>(shdr_offset)   ;
		size_t      shdr_name    = string_offset + shdr.sh_name      ;
		const char* section_name = &file_map.get<char>(shdr_name)    ;
		if (::strcmp(section_name,".dynamic")==0) {
			dyn_offset = shdr.sh_offset ;
			break ;
		}
	}
	return &file_map.get<Dyn>(dyn_offset) ;
}

template<class T> T const& Elf::DynDigest::_s_vma_to_ref( size_t vma , FileMap const& file_map ) {
	if (!file_map) return *reinterpret_cast<const T*>(vma) ;
	Ehdr const& ehdr = file_map.get<Ehdr>() ;
	for( size_t i : iota(ehdr.e_phnum) ) {
		Phdr const& phdr = file_map.get<Phdr>( ehdr.e_phoff + i*ehdr.e_phentsize ) ;
		if ( phdr.p_type!=PT_LOAD                                                  ) continue ;
		if ( vma<(phdr.p_vaddr&-phdr.p_align) || vma>=(phdr.p_vaddr+phdr.p_filesz) ) continue ;
		return file_map.get<T>( vma - phdr.p_vaddr + phdr.p_offset ) ;
	}
	throw 1 ; // bad address : stop analysis and ignore
}

inline ::pair<const char*,size_t> Elf::DynDigest::_s_str_tab( Dyn const* dyn_tab , FileMap const& file_map ) {
	const char* str_tab = nullptr ;
	size_t      sz      = 0       ;
	for( Dyn const* dyn=dyn_tab ; dyn->d_tag!=DT_NULL ; dyn++ ) {
		if ( +file_map && dyn>&file_map.get<Dyn>(file_map.sz-sizeof(Dyn)) ) throw 6 ;           // "bad dynamic table entry : stop analysis and ignore
		switch (dyn->d_tag) {
			case DT_STRTAB : str_tab = &_s_vma_to_ref<char>(dyn->d_un.d_ptr,file_map) ; break ;
			case DT_STRSZ  : sz      =                      dyn->d_un.d_val           ; break ; // for swear only
			default : continue ;
		}
		if ( str_tab && sz ) {
			if ( +file_map && str_tab+sz-1>&file_map.get<char>(file_map.sz-1) ) throw 7 ;       // str tab too long
			return {str_tab,sz} ;
		}
	}
	throw 8 ;                                                                                   // cannot find dyn string table
}

inline Elf::DynDigest::DynDigest( Dyn const* dyn_tab , FileMap const& file_map ) {
	auto        [str_tab,str_sz] = _s_str_tab(dyn_tab,file_map) ;
	const char* str_tab_end      = str_tab + str_sz             ;
	for( Dyn const* dyn=dyn_tab ; dyn->d_tag!=DT_NULL ; dyn++ ) {
		const char* s = str_tab + dyn->d_un.d_val ;
		switch (dyn->d_tag) {
			case DT_RPATH   : SWEAR(!rpath  ) ; rpath   = s ; break ;
			case DT_RUNPATH : SWEAR(!runpath) ; runpath = s ; break ;
			case DT_NEEDED  : if (*s) neededs.push_back(s) ;  break ;
			default : continue ;
		}
		if (s>=str_tab_end) throw 9 ;               // dyn entry name outside string tab
	}
	if ( runpath              ) rpath   = nullptr ; // DT_RPATH is not used if DT_RUNPATH is present
	if ( rpath   && !*rpath   ) rpath   = nullptr ;
	if ( runpath && !*runpath ) runpath = nullptr ;
}

inline ::string _mk_abs_exe(::string const& exe) {
	if (+exe) {        ::string abs_exe = mk_glb(exe,Record::s_autodep_env().repo_root_s) ; return abs_exe ; }
	else      { static ::string abs_exe = read_lnk(File("/proc/self/exe"))                ; return abs_exe ; }
} ;
inline ::string Elf::s_expand( const char* txt , ::string const& exe ) {
	static constexpr const char* LdSoLib   =                 LD_SO_LIB              ;
	static constexpr const char* LdSoLib32 = LD_SO_LIB_32[0]?LD_SO_LIB_32:LD_SO_LIB ; // on 32 bits systems, there is only 32 bits apps and info is in LD_SO_LIB
	if (!txt) return {} ;
	const char* ptr = ::strchrnul(txt,'$') ;
	::string    res { txt , ptr }          ;
	while (*ptr) {                                                                    // INVARIANT : *ptr=='$', result must be res+ptr if no further substitution
		bool        brace = ptr[1]=='{' ;
		const char* p1    = ptr+1+brace ;
		if      ( ::memcmp(p1,"ORIGIN"  ,6)==0 && (!brace||p1[6]=='}') ) { res += no_slash(dir_name_s(_mk_abs_exe(exe)))                  ; ptr = p1+6+brace ; }
		else if ( ::memcmp(p1,"LIB"     ,3)==0 && (!brace||p1[3]=='}') ) { res += Is64Bits?LdSoLib:LdSoLib32                              ; ptr = p1+3+brace ; }
		else if ( ::memcmp(p1,"PLATFORM",8)==0 && (!brace||p1[8]=='}') ) { res += reinterpret_cast<const char*>(::getauxval(AT_PLATFORM)) ; ptr = p1+8+brace ; }
		else                                                             { res += *ptr                                                    ; ptr++            ; }
		const char* new_ptr = ::strchrnul(ptr,'$') ;
		res.append(ptr,new_ptr) ;
		ptr = new_ptr ;
	}
	return res ;
}

inline Record::Read<true/*Send*/> Elf::search_elf( ::string const& file , ::string const& runpath , Comment c ) {
	if (!file               ) return {} ;
	if (file.find('/')!=Npos) {
		if (!seen.try_emplace(file,Maybe).second) return {} ;
		Record::Read<true/*Send*/> res = { *r , file , false/*no_follow*/ , true/*keep_real*/ , c } ;
		elf_deps( res , false/*top*/ , c ) ;
		return res ;
	}
	//
	static constexpr const char* StdLibraryPath = Is64Bits ? STD_LIBRARY_PATH : STD_LIBRARY_PATH_32 ;
	First    first ;
	::string path  ;
	if (+rpath           ) path <<first("",":")<< rpath           ;
	if (+ld_library_path ) path <<first("",":")<< ld_library_path ;
	if (+runpath         ) path <<first("",":")<< runpath         ;
	if (StdLibraryPath[0]) path <<first("",":")<< StdLibraryPath  ;
	//
	if (+path)
		for( size_t pos=0 ;;) {
			size_t end = path.find(':',pos) ;
			::string        full_file  = path.substr(pos,end-pos) ;
			if (+full_file) full_file += '/'                      ;
			/**/            full_file += file                     ;
			Record::Read<true/*Send*/> rr            { *r , full_file , false/*no_follow*/ , true/*keep_real*/ , c } ;
			auto                       [it,inserted] = seen.try_emplace(rr.real,Maybe)                               ;
			if ( it->second==Maybe           )   it->second = No | FileInfo({Record::s_repo_root_fd(),rr.real},{.no_follow=false}).exists() ;   // real may be a sym link in the system directories
			if ( it->second==Yes && inserted ) { elf_deps( rr , false/*top*/ , c ) ; return rr ;                                              }
			if ( it->second==Yes             )                                       return {} ;
			if (end==Npos) break ;
			pos = end+1 ;
		}
	return {} ;
}

inline void Elf::elf_deps( Record::Solve<false/*Send*/> const& file , bool top , Comment c ) {
	if ( simple_llp && file.file_loc==FileLoc::Ext ) return ;
	try {
		FileMap   file_map {{ Record::s_repo_root_fd() , file.real }} ; if (!file_map) return ;                                        // real may be a sym link in system dirs
		DynDigest digest   { file_map }                               ;
		if ( top && digest.rpath ) rpath = digest.rpath ;                                                                              // rpath applies to the whole search
		for( const char* needed : digest.neededs ) search_elf( s_expand(needed,file.real) , s_expand(digest.runpath,file.real) , c ) ;
	} catch (int) { return ; }                                                                                                         // bad file format, ignore
}

// capture LD_LIBRARY_PATH when first called : man dlopen says it must be captured at program start, but we capture it before any environment modif, should be ok
inline const char* get_ld_library_path() {
	static ::string llp = get_env("LD_LIBRARY_PATH") ;
	return llp.c_str() ;
}

inline Record::Read<true/*Send*/> search_elf( Record& r , const char* file , Comment c ) {
	if (!file) return {} ;
	static Elf::DynDigest s_digest { New } ;
	try                       { return Elf(r,{},get_ld_library_path(),s_digest.rpath).search_elf( file , Elf::s_expand(s_digest.runpath) , c ) ; }
	catch (::string const& e) { r.report_panic(cat("while searching elf executable ",file," : ",e)) ; return {} ;                                } // if we cannot report the dep, panic
}

inline void elf_deps( Record& r , Record::Solve<false/*Send*/> const& file , const char* ld_library_path , Comment c ) {
	try                       { Elf(r,file.real,ld_library_path).elf_deps( file , true/*top*/ , c ) ;               }
	catch (::string const& e) { r.report_panic(cat("while analyzing elf executable ",mk_file(file.real)," : ",e)) ; } // if we cannot report the dep, panic
}
