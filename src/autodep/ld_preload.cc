// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <dlfcn.h>
#include <link.h>       // dl related stuff
#include <sys/auxv.h>   // getauxval

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
			void*       main = ::dlopen(nullptr,RTLD_NOW|RTLD_NOLOAD) ; if (!main                                ) return ;
			::link_map* lm   = nullptr/*garbage*/                     ; if (::dlinfo(main,RTLD_DI_LINKMAP,&lm)!=0) return ;
			*this = DynDigest(lm->l_ld) ;
		}
		// data
		::vector<const char*> neededs ;
		const char*           rpath   = nullptr ;
		const char*           runpath = nullptr ;
	} ;

	// statics
	static ::string s_expand( const char* txt , ::string const& exe={} ) ;
	// cxtors & casts
	Elf( Record& r_ , ::string const& exe , const char* llp , const char* rp=nullptr ) : r{&r_} , ld_library_path{s_expand(llp,exe)} , rpath{s_expand(rp,exe)} {}
	// services
	Record::Read search_elf( ::string const& file , ::string const& runpath , ::string&& comment="elf_srch" ) ;
	void         elf_deps  ( ::string const& real , bool top                , ::string&& comment="elf_deps" ) ;
	// data
	Record*                   r               = nullptr/*garbage*/ ;
	::string                  ld_library_path ;
	::string                  rpath           ;
	::umap_s<Bool3/*exists*/> seen            = {}                 ;
} ;

Elf::Dyn const* Elf::DynDigest::_s_search_dyn_tab( FileMap const& file_map ) {
	//
	Ehdr const& ehdr       = file_map.get<Ehdr>() ;
	size_t      dyn_offset = 0/*garbage*/         ;
	//
	if ( file_map.sz                         <  sizeof(Ehdr)                                              ) throw 2 ; // file too small : stop analysis and ignore
	if ( memcmp(ehdr.e_ident,ELFMAG,SELFMAG) != 0                                                         ) throw 3 ; // bad header     : .
	if ( ehdr.e_ident[EI_CLASS]              != (Is64Bits                       ?ELFCLASS64 :ELFCLASS32 ) ) throw 4 ; // bad word width : .
	if ( ehdr.e_ident[EI_DATA ]              != (::endian::native==::endian::big?ELFDATA2MSB:ELFDATA2MSB) ) throw 5 ; // bad endianness : .
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

template<class T> T const& Elf::DynDigest::_s_vma_to_ref( size_t vma , FileMap const& file_map ) {
	if (!file_map) return *reinterpret_cast<const T*>(vma) ;
	Ehdr const& ehdr = file_map.get<Ehdr>() ;
	for( size_t i=0 ; i<ehdr.e_phnum ; i++ ) {
		Phdr const& phdr = file_map.get<Phdr>( ehdr.e_phoff + i*ehdr.e_phentsize ) ;
		if ( phdr.p_type!=PT_LOAD                                                  ) continue ;
		if ( vma<(phdr.p_vaddr&-phdr.p_align) || vma>=(phdr.p_vaddr+phdr.p_filesz) ) continue ;
		return file_map.get<T>( vma - phdr.p_vaddr + phdr.p_offset ) ;
	}
	throw 1 ; // bad address : stop analysis and ignore
}

::pair<const char*,size_t> Elf::DynDigest::_s_str_tab( Dyn const* dyn_tab , FileMap const& file_map ) {
	const char* str_tab = nullptr ;
	size_t      sz      = 0       ;
	for( Dyn const* dyn=dyn_tab ; dyn->d_tag!=DT_NULL ; dyn++ ) {
		if ( +file_map && dyn>&file_map.get<Dyn>(file_map.sz-sizeof(Dyn)) ) throw 6 ; // "bad dynamic table entry : stop analysis and ignore
		switch (dyn->d_tag) {
			case DT_STRTAB : str_tab = &_s_vma_to_ref<char>(dyn->d_un.d_ptr,file_map) ; break ;
			case DT_STRSZ  : sz      =                      dyn->d_un.d_val           ; break ; // for swear only
			default : continue ;
		}
		if ( str_tab && sz ) {
			if ( +file_map && str_tab+sz-1>&file_map.get<char>(file_map.sz-1) ) throw 7 ; // str tab too long
			return {str_tab,sz} ;
		}
	}
	throw 8 ; // cannot find dyn string table
}

Elf::DynDigest::DynDigest( Dyn const* dyn_tab , FileMap const& file_map ) {
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
		if (s>=str_tab_end) throw 9 ; // dyn entry name outside string tab
	}
	if ( runpath              ) rpath   = nullptr ; // DT_RPATH is not used if DT_RUNPATH is present
	if ( rpath   && !*rpath   ) rpath   = nullptr ;
	if ( runpath && !*runpath ) runpath = nullptr ;
}

static ::string _mk_origin(::string const& exe) {
	if (+exe) {
		return dir_name(mk_abs(exe,Record::s_autodep_env().root_dir+'/')) ;
	} else {
		static ::string s_res = dir_name(read_lnk("/proc/self/exe")) ;
		return s_res ;
	}
} ;
::string Elf::s_expand( const char* txt , ::string const& exe ) {
	if (!txt) return {} ;
	const char* ptr = ::strchrnul(txt,'$') ;
	::string    res { txt , ptr }          ;
	while (*ptr) {                                                           // INVARIANT : *ptr=='$', result must be res+ptr if no further substitution
		bool        brace = ptr[1]=='{' ;
		const char* p1    = ptr+1+brace ;
		if      ( ::memcmp(p1,"ORIGIN"  ,6)==0 && (!brace||p1[6]=='}') ) { res += _mk_origin(exe)                                         ; ptr = p1+6+brace ; }
		else if ( ::memcmp(p1,"LIB"     ,3)==0 && (!brace||p1[3]=='}') ) { res += Is64Bits?"lib64":"lib"                                  ; ptr = p1+3+brace ; } // XXX : find reliable libs
		else if ( ::memcmp(p1,"PLATFORM",8)==0 && (!brace||p1[8]=='}') ) { res += reinterpret_cast<const char*>(::getauxval(AT_PLATFORM)) ; ptr = p1+8+brace ; }
		else                                                             { res += *ptr                                                    ; ptr++            ; }
		const char* new_ptr = ::strchrnul(ptr,'$') ;
		res.append(ptr,new_ptr-ptr) ;
		ptr = new_ptr ;
	}
	return res ;
}

// XXX : optimize search : do not search libs outside repo if ld_library_path does not have an entry inside the repo as outside world is not supposed to have any pointer inside repo
Record::Read Elf::search_elf( ::string const& file , ::string const& runpath , ::string&& comment ) {
	if (!file) return {} ;
	if (file.find('/')!=Npos) {
		if (!seen.try_emplace(file,Maybe).second) return {} ;
		::string     dc  = comment+".dep"                                                                                    ;
		Record::Read res = { *r , file , false/*no_follow*/ , true/*keep_real*/ , false/*allow_tmp_map*/ , ::move(comment) } ;
		elf_deps( res.real , false/*top*/ , ::move(dc) ) ;
		return res ;
	}
	//
	::string path ;
	if (+rpath          ) append_to_string( path , rpath           , ':' ) ;
	if (+ld_library_path) append_to_string( path , ld_library_path , ':' ) ;
	if (+runpath        ) append_to_string( path , runpath         , ':' ) ;
	path += "/lib:/usr/lib:/lib64:/usr/lib64" ;                             // XXX : find a reliable way to get default directories
	//
	for( size_t pos=0 ;;) {
		size_t end = path.find(':',pos) ;
		::string        full_file  = path.substr(pos,end-pos) ;
		if (+full_file) full_file += '/'                      ;
		/**/            full_file += file                     ;
		Record::Read rr { *r , full_file , false/*no_follow*/ , true/*keep_real*/ , false/*allow_tmp_map*/ , ::copy(comment) } ;
		auto [it,inserted] = seen.try_emplace(rr.real,Maybe) ;
		if ( it->second==Maybe           )   it->second = No | is_target(Record::s_root_fd(),rr.real,false/*no_follow*/) ;      // real may be a sym link in the system directories
		if ( it->second==Yes && inserted ) { elf_deps( rr.real , false/*top*/ , comment+".dep" ) ; return rr ; }
		if ( it->second==Yes             )                                                         return {} ;
		if (end==Npos) break ;
		pos = end+1 ;
	}
	return {} ;
}

void Elf::elf_deps( ::string const& real , bool top , ::string&& comment ) {
	try {
		FileMap   file_map { Record::s_root_fd() , real } ; if (!file_map) return ;                           // real may be a sym link in system dirs
		DynDigest digest   { file_map }                   ;
		if (top) rpath = digest.rpath ;                                                                                                       // rpath applies to the whole search
		for( const char* needed : digest.neededs ) search_elf( s_expand(needed,real) , s_expand(digest.runpath,real) , comment+".needed" ) ;
	} catch (int) { return ; }                                                                                                                // bad file format, ignore
}

// capture LD_LIBRARY_PATH when first called : man dlopen says it must be captured at program start, but we capture it before any environment modif, should be ok
const char* get_ld_library_path() {
	static ::string llp = get_env("LD_LIBRARY_PATH") ;
	return llp.c_str() ;
}

Record::Read search_elf( Record& r , const char* file , ::string&& comment="elf_srch" ) {
	if (!file) return {} ;
	static Elf::DynDigest s_digest { New } ;
	try                       { return Elf(r,{},get_ld_library_path(),s_digest.rpath).search_elf( file , Elf::s_expand(s_digest.runpath) , ::move(comment) ) ; }
	catch (::string const& e) { r.report_panic("while searching elf executable ",file," : ",e) ; return {} ;                                                   } // if we cannot report the dep, panic
}

void elf_deps( Record& r , ::string const& real , const char* ld_library_path , ::string&& comment="elf_deps" ) {
	try                       { Elf(r,real,ld_library_path).elf_deps( real , true/*top*/ , ::move(comment) ) ; }
	catch (::string const& e) { r.report_panic("while analyzing elf executable ",mk_file(real)," : ",e) ; } // if we cannot report the dep, panic
}

#define LD_PRELOAD 1
#include "ld.cc"
