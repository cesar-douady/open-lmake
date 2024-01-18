// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <elf.h>

#include <bit>

#include "app.hh"
#include "disk.hh"

using namespace Disk ;

static constexpr bool Is32Bits = sizeof(void*)==4 ;
static constexpr bool Is64Bits = sizeof(void*)==8 ;
static_assert( Is32Bits || Is64Bits ) ;

using Ehdr = ::conditional_t<Is32Bits,Elf32_Ehdr,Elf64_Ehdr> ;
using Phdr = ::conditional_t<Is32Bits,Elf32_Phdr,Elf64_Phdr> ;
using Shdr = ::conditional_t<Is32Bits,Elf32_Shdr,Elf64_Shdr> ;
using Dyn  = ::conditional_t<Is32Bits,Elf32_Dyn ,Elf64_Dyn > ;

template<class T> T const& gather( FileMap const& file_map , size_t offset=0 ) { return *reinterpret_cast<T const*>(file_map.data+offset) ; }

size_t vma_to_offset( FileMap const& file_map , Ehdr const& ehdr , size_t vma ) {
	for( size_t i=0 ; i<ehdr.e_phnum ; i++ ) {
		Phdr const& phdr = gather<Phdr>( file_map , ehdr.e_phoff+i*ehdr.e_phentsize ) ;
		if ( phdr.p_type!=PT_LOAD                                                  ) continue ;
		if ( vma<(phdr.p_vaddr&-phdr.p_align) || vma>=(phdr.p_vaddr+phdr.p_filesz) ) continue ;
		return vma - phdr.p_vaddr + phdr.p_offset ;
	}
	throw "cannot find address"s ;
}

void do_file( FileMap const& file_map ) {
	//
	size_t dyn_offset = 0/*garbage*/ ;
	size_t dyn_sz     = 0/*garbage*/ ;
	//
	Ehdr const& ehdr = gather<Ehdr>(file_map) ;
	//
	::cout<<"pg hdrs offset      : "<<::hex<<ehdr.e_phoff<<::dec<<endl ;
	::cout<<"n pg hdrs           : "<<ehdr.e_phnum<<endl ;
	for( size_t i=0 ; i<ehdr.e_phnum ; i++ ) {
		size_t      phdr_offset = ehdr.e_phoff + i*ehdr.e_phentsize      ;
		Phdr const& phdr        = gather<Phdr>( file_map , phdr_offset ) ;
		::cout << "pg hdr "<<i<<"            : "<<::hex<<phdr_offset<<::dec<<" "<<::hex<<phdr.p_type<<::dec<<" "<<::hex<<phdr.p_offset<<"->"<<phdr.p_vaddr<<::dec<<endl ;
		if (phdr.p_type==PT_DYNAMIC) {
			dyn_offset = phdr.p_offset ;
			dyn_sz     = phdr.p_filesz ;
			goto DoSection ;
		}
	}
	throw "no dynamic header"s ;
DoSection :
	size_t string_shdr_offset = ehdr.e_shoff + ehdr.e_shstrndx * ehdr.e_shentsize   ;
	size_t string_offset      = gather<Shdr>(file_map,string_shdr_offset).sh_offset ;
	//
	::cout<<"string shdr         : "<<ehdr.e_shstrndx   <<" "<<::hex<<string_shdr_offset<<::dec<<endl ;
	::cout<<"string offset       : "<<string_offset <<" "<<::hex<<string_offset     <<::dec<<endl ;
	::cout<<"section hdrs offset : "<<::hex<<ehdr.e_shoff<<::dec<<endl ;
	::cout<<"n section hdrs      : "<<ehdr.e_shnum     <<endl ;
	for( size_t i=0 ; i<ehdr.e_shnum ; i++ ) {
		size_t      shdr_offset = ehdr.e_shoff + i*ehdr.e_shentsize      ;
		Shdr const& shdr        = gather<Shdr>( file_map , shdr_offset ) ;
		//
		size_t      shdr_name    = string_offset + shdr.sh_name      ;
		const char* section_name = &gather<char>(file_map,shdr_name) ;
		::cout << "section hdr "<<i<<"       : "<<::hex<<shdr_offset<<::dec<<" "<<shdr.sh_name<<" "<<::hex<<shdr_name<<::dec<<" /"<<section_name<<"/"<<endl ;
		if (section_name==".dynamic"s) {
			dyn_offset = shdr.sh_offset ;
			dyn_sz     = shdr.sh_size   ;
		}
	}
	::cout<<"dyn offset : "<<::hex<<dyn_offset<<::dec<<endl ;
	Dyn const* dyn_sentinel   = &gather<Dyn>(file_map,dyn_offset+dyn_sz) ;
	size_t     dyn_str_offset = 0                                        ;
	size_t     dyn_str_sz     = 0                                        ;
	for( Dyn const* dyn_entry = &gather<Dyn>(file_map,dyn_offset) ; dyn_entry->d_tag!=DT_NULL ; dyn_entry++ ) {
		if (dyn_entry+1>=dyn_sentinel) throw "dyn entry past end of dyn section"s ;
		switch (dyn_entry->d_tag) {
			case DT_STRTAB : dyn_str_offset = vma_to_offset(file_map,ehdr,dyn_entry->d_un.d_val) ; break ;
			case DT_STRSZ  : dyn_str_sz     = dyn_entry->d_un.d_val                              ; break ;
		}
		if (dyn_str_offset&&dyn_str_sz) goto FoundDynStr ;
	}
	throw "no dynamic string tab"s ;
FoundDynStr :
	const char* dyn_str_tab = &gather<char>(file_map,dyn_str_offset) ;
	for( Dyn const* dyn_entry = &gather<Dyn>(file_map,dyn_offset) ; dyn_entry->d_tag!=DT_NULL ; dyn_entry++ ) {
		switch (dyn_entry->d_tag) {
			case DT_NEEDED  : ::cout<<"DT_NEEDED         : "<< ::hex<<dyn_entry->d_tag<<::dec<<" /"<< (dyn_str_tab+dyn_entry->d_un.d_val) <<"/"<<endl ; break ;
			case DT_RPATH   : ::cout<<"DT_RPATH          : "<< ::hex<<dyn_entry->d_tag<<::dec<<" /"<< (dyn_str_tab+dyn_entry->d_un.d_val) <<"/"<<endl ; break ;
			case DT_RUNPATH : ::cout<<"DT_RUNPATH        : "<< ::hex<<dyn_entry->d_tag<<::dec<<" /"<< (dyn_str_tab+dyn_entry->d_un.d_val) <<"/"<<endl ; break ;
			default         : ::cout<<"dyn entry         : "<< ::hex<<dyn_entry->d_tag<<::dec<<                                                  endl ; break ;
		}
	}
}

void do_file(::string const& file) {
	::cout<<file<<endl;
	FileMap file_map{file} ;
	uint8_t const* data = file_map.data ;
	if ( file_map.sz                   <  sizeof(Ehdr)                      ) throw "file too small"s ;
	if ( memcmp(data,ELFMAG,SELFMAG)   != 0                                 ) throw "bad header"s     ;
	if ( (data[EI_CLASS]==ELFCLASS64 ) != Is64Bits                          ) throw "bad word width"s ;
	if ( (data[EI_DATA ]==ELFDATA2MSB) != (::endian::native==::endian::big) ) throw "bad endianness"s ;
	//
	do_file(file_map) ;
}

int main( int argc , char* argv[] ) {
	//
	if (argc!=2) exit(2,"must be called with one arg") ;
	app_init(true/*search_root*/,true/*cd_root*/) ;
	//
	try                       { do_file(argv[1]) ; }
	catch (::string const& e) { exit(1,e) ;        }
	//
	return 0 ;
}
