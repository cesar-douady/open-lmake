// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "raw_file.hh"
#include "struct.hh"
#include "prefix.hh"

using namespace Store ;

::string g_dir_s ;

using TestHdr = int ;

template<bool Multi,bool HasDataSz> struct TestData {
	TestData(             )                 : sz(0) {                             }
	TestData(int v        )                 : sz(1) { val[0] = v  ;               }
	TestData(int v1,int v2) requires(Multi) : sz(2) { val[0] = v1 ; val[1] = v2 ; }
	TestData& operator=(int v) { sz = 1 ; val[0] = v ; return self ; }
	size_t n_items() const requires(HasDataSz) { return sz ; }
	size_t sz ;
	int val[1] ;
} ;

//
// RawFile
//

struct TestFile {
	TestFile() {
		Fd::Stdout.write("check file ...") ;
		::string filename = g_dir_s+"file" ;
		{	RawFile<0/*ThreadKey*/,10000> f(filename,true/*writable*/) ;
			f.expand(1000) ;
			f.base[100] = 'a' ;
			f.expand(5000) ;
			f.base[101] = 'b' ;
			f.clear(1000) ;
		}
		{	RawFile<0/*ThreadKey*/,10000> f(filename,false/*writable*/) ;
			SWEAR(f.base[100]=='a') ;
			SWEAR(f.base[101]=='b') ;
		}
		Fd::Stdout.write(" ok\n") ;
	}
} ;
void test_file() {
	TestFile() ;
}

//
// Struct
//

template<bool HasHdr,bool Multi> struct TestStruct {
	using Idx  = uint32_t                             ;
	using Hdr  = ::conditional_t<HasHdr,TestHdr,void> ;
	using Data = TestData<Multi,false>                ;

	void test_hdr() requires(!HasHdr) {}
	void test_hdr() requires( HasHdr) {
		file.hdr() = 3 ;
		SWEAR(file.hdr()==3) ;
	}
	void test_data() requires(!Multi) {
		Idx idx = file.emplace_back(4) ;
		SWEAR( file.at(idx).val[0]==4 ) ;
	}
	void test_data() requires(Multi) {
		Idx idx1 = file.emplace_back(1,4) ;
		SWEAR(idx1==1) ;
		Idx idx2 = file.emplace_back(2,5,6) ;
		SWEAR(idx2==2) ;
		SWEAR( file.at(idx1).val[0]==4 ) ;
		SWEAR( file.at(idx2).val[0]==5 ) ;
		SWEAR( file.at(idx2).val[1]==6 ) ;
		file.clear() ;
		Idx idx3 = file.emplace_back(1,7) ;
		SWEAR(idx3==1) ;
		SWEAR( file.at(idx3).val[0]==7 ) ;
	}
	TestStruct() : file(g_dir_s+"struct"+(HasHdr?"_hdr":"")+(Multi?"_multi":""),true/*writable*/) {
		::string out ;
		out<<"check struct" ;
		if (HasHdr   ) out<<" with header"   ;
		if (Multi    ) out<<" with multi"    ;
		out<<" ..." ;
		Fd::Stdout.write(out) ;
		test_hdr() ;
		test_data() ;
		Fd::Stdout.write(" ok\n") ;
	}
	// data
	StructFile<0/*ThreadKey*/,Hdr,Idx,20,Data,Multi> file ;
} ;
void test_struct() {
	TestStruct<false/*HasHdr*/,false/*Multi*/>() ;
	TestStruct<true /*HasHdr*/,false/*Multi*/>() ;
	TestStruct<false/*HasHdr*/,true /*Multi*/>() ;
	TestStruct<true /*HasHdr*/,true /*Multi*/>() ;
}

//
// Prefix
//

template<bool HasHdr,bool HasData,bool Reverse> struct TestPrefix {
	using Hdr  = ::conditional_t<HasHdr ,int,void> ;
	using Idx  = uint32_t                          ;
	using Char = char                              ;
	using Data = ::conditional_t<HasData,int,void> ;

	void test_hdr() requires(!HasHdr) {}
	void test_hdr() requires( HasHdr) {
		file.hdr() = 3 ; SWEAR(file.hdr()==3) ;
	}
	void test_tree() {
		::string f = Reverse ? "c" : "a" ;
		Idx                idx1   = file.insert (f     ) ; SWEAR( idx1                                                   ) ; file.chk() ;
		Idx                idx2   = file.insert ("abc"s) ; SWEAR( idx2                                                   ) ; file.chk() ;
		Idx                idx3   = file.search (f     ) ; SWEAR( idx3==idx1                             , idx3   , idx1 ) ;
		::string           n      = file.str_key(idx1  ) ; SWEAR( n==f                                   , n      , f    ) ;
		Idx                idx4   = file.search ("abc"s) ; SWEAR( idx4==idx2                             , idx4   , idx2 ) ;
		Idx                idx5   = file.search ("adc"s) ; SWEAR( !idx5                                  , idx5          ) ;
		::pair<Idx,size_t> idx_sz = file.longest("adc"s) ; SWEAR( idx_sz.first==idx1 && idx_sz.second==1 , idx_sz , idx1 ) ;
		Idx                idx6   = file.insert ("abe"s) ; SWEAR( idx6                                                   ) ; file.chk() ;
		Idx                idx7   = file.search ("abe"s) ; SWEAR( idx7==idx6                             , idx7   , idx6 ) ;
		/**/                        file.pop    (idx7  ) ;                                                                   file.chk() ;
		Idx                idx8   = file.search ("abe"s) ; SWEAR( !idx8                                  , idx8          ) ;
	}
	void test_data() requires(!HasData) {}
	void test_data() requires( HasData) {
		::string f = Reverse ? "c" : "a" ;
		Idx      idx1 = file.search    (f     )      ;
		Idx      idx2 = file.insert    (f     )      ; SWEAR(idx2==idx1) ; file.chk() ;
		/**/            file.at        (idx1  ) = 35 ;
		Idx      idx3 = file.insert    ("adc"s)      ;                     file.chk() ;
		/**/            file.at        (idx3  ) = 36 ;                     file.chk() ;
		Idx      idx4 = file.search    (f     )      ; SWEAR(idx4==idx1) ;
		int      v1   = file.at        (idx1  )      ; SWEAR(v1  ==35  ) ;
		::string n1   = file.str_key   (idx1  )      ; SWEAR(n1  ==f   ) ;
		int      v2   = *file.search_at("adc"s)      ; SWEAR(v2  ==36  ) ;
	}
	TestPrefix() : file(g_dir_s+"prefix"+(HasHdr?"_hdr":"")+(HasData?"_data":"")+(Reverse?"_reverse":""),true/*writable*/) {
		::string out = "check prefix" ;
		if (HasHdr    ) out<<" with header"  ;
		if (HasData   ) out<<" with data"    ;
		if (Reverse   ) out<<" with reverse" ;
		out<<" ..." ;
		Fd::Stdout.write(out) ;
		test_hdr () ;
		test_tree() ;
		test_data() ;
		Fd::Stdout.write(" ok\n") ;
	}

	// data
	SinglePrefixFile<0/*ThreadKey*/,Hdr,Idx,20,Char,Data,Reverse> file ;
} ;
void test_prefix() {
	TestPrefix<false/*HasHdr*/,false/*HasData*/,false/*Reverse*/>() ;
	TestPrefix<true /*HasHdr*/,false/*HasData*/,false/*Reverse*/>() ;
	TestPrefix<false/*HasHdr*/,true /*HasData*/,false/*Reverse*/>() ;
	TestPrefix<true /*HasHdr*/,true /*HasData*/,false/*Reverse*/>() ;
	TestPrefix<false/*HasHdr*/,false/*HasData*/,true /*Reverse*/>() ;
	TestPrefix<true /*HasHdr*/,false/*HasData*/,true /*Reverse*/>() ;
	TestPrefix<false/*HasHdr*/,true /*HasData*/,true /*Reverse*/>() ;
	TestPrefix<true /*HasHdr*/,true /*HasData*/,true /*Reverse*/>() ;
}

void test_lmake() {
	Fd::Stdout.write("check lmake ...") ;
	SinglePrefixFile<0/*ThreadKey*/,void,uint32_t,20> file(g_dir_s+"lmake",true/*writable*/) ;
	char key ; //!                              count
	key = (char)0x28 ; file.insert(::string(&key,1  )) ;
	key = (char)0xb1 ; file.insert(::string(&key,1  )) ;
	key = (char)0xef ; file.insert(::string(&key,1  )) ;
	Fd::Stdout.write(" ok\n") ;
}

int main( int argc , char const* argv[] ) {
	SWEAR(argc==2) ;
	g_dir_s = with_slash(argv[1]) ;
	Fd::Stdout.write(cat("chk dir : ",no_slash(g_dir_s),'\n')) ;
	test_file  () ;
	test_struct() ;
	test_prefix() ;
	test_lmake () ;
	return 0 ;
}
