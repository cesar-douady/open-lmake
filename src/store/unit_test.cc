// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "file.hh"
#include "struct.hh"
#include "side_car.hh"
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
// File
//

struct TestFile {
	TestFile() {
		Fd::Stdout.write("check file ...") ;
		::string file_name = g_dir_s+"file" ;
		{	File<false,10000> f(file_name,true/*writable*/) ;
			f.expand(1000) ;
			f.base[100] = 'a' ;
			f.expand(5000) ;
			f.base[101] = 'b' ;
			f.clear(1000) ;
		}
		{	File<false,10000> f(file_name,false/*writable*/) ;
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

template<bool HasHdr,bool HasData,bool Multi> struct TestStruct {
	static_assert( !Multi || HasData ) ;

	using Idx  = uint32_t                                            ;
	using Hdr  = ::conditional_t<HasHdr ,TestHdr              ,void> ;
	using Data = ::conditional_t<HasData,TestData<Multi,false>,void> ;

	void test_hdr() requires(!HasHdr) {}
	void test_hdr() requires( HasHdr) {
		file.hdr() = 3 ;
		SWEAR(file.hdr()==3) ;
	}
	void test_data() requires(!HasData) {}
	void test_data() requires( HasData && !Multi ) {
		Idx idx = file.emplace_back(4) ;
		SWEAR( file.at(idx).val[0]==4 ) ;
	}
	void test_data() requires( HasData && Multi ) {
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
	TestStruct() : file(g_dir_s+"struct"+(HasHdr?"_hdr":"")+(HasData?"_data":"")+(Multi?"_multi":""),true/*writable*/) {
		::string out ;
		out<<"check struct" ;
		if (HasHdr   ) out<<" with header"   ;
		if (HasData  ) out<<" with data"     ;
		if (Multi    ) out<<" with multi"    ;
		out<<" ..." ;
		Fd::Stdout.write(out) ;
		test_hdr() ;
		test_data() ;
		Fd::Stdout.write(" ok\n") ;
	}

	// data
	StructFile<false,Hdr,Idx,HasData?20:0,Data,Multi> file ;
} ;
void test_struct() {
	TestStruct<false/*HasHdr*/,false/*HasData*/,false/*Multi*/>() ;
	TestStruct<true /*HasHdr*/,false/*HasData*/,false/*Multi*/>() ;
	TestStruct<false/*HasHdr*/,true /*HasData*/,false/*Multi*/>() ;
	TestStruct<true /*HasHdr*/,true /*HasData*/,false/*Multi*/>() ;
	TestStruct<false/*HasHdr*/,true /*HasData*/,true /*Multi*/>() ;
	TestStruct<true /*HasHdr*/,true /*HasData*/,true /*Multi*/>() ;
}

//
// SideCar (also check Alloc on the fly as SideCar inherits from Alloc and is a completely transparent when associated with void)
//

template<bool HasHdr,bool HasData,bool HasSideCar,bool Multi,bool HasDataSz> struct TestSideCar {
	static constexpr bool HasEither      =  HasData || HasSideCar ;
	static constexpr bool HasSideCarOnly = !HasData && HasSideCar ;

	using Idx       = uint32_t                                                         ;
	using Hdr       = ::conditional_t< HasHdr     , TestHdr                   , void > ;
	using Data      = ::conditional_t< HasData    , TestData<Multi,HasDataSz> , void > ;
	using SideCar   = ::conditional_t< HasSideCar , TestData<Multi,false    > , void > ;
	using DataNv    = NoVoid<Data   >                                                  ;
	using SideCarNv = NoVoid<SideCar>                                                  ;

	static_assert( !Multi     ||  HasEither ) ;
	static_assert( !HasDataSz || Multi      ) ;

	DataNv   & at(Idx idx) requires(  HasData               ) { return file.at      (idx) ; }
	SideCarNv& at(Idx idx) requires( !HasData && HasSideCar ) { return file.side_car(idx) ; }

	template<bool AutoSz> requires(!AutoSz) void pop    (Idx idx,Idx   sz                  ) { file.pop    (idx,sz           ) ; }
	template<bool AutoSz> requires( AutoSz) void pop    (Idx idx,Idx /*sz*/                ) { file.pop    (idx              ) ; }
	template<bool AutoSz> requires(!AutoSz) void shorten(Idx idx,Idx old_sz,Idx   new_sz   ) { file.shorten(idx,old_sz,new_sz) ; }
	template<bool AutoSz> requires( AutoSz) void shorten(Idx idx,Idx old_sz,Idx /*new_sz*/ ) { file.shorten(idx,old_sz       ) ; }

	Idx emplace( int v           ) requires( HasData        &&  Multi ) { return file.emplace(1,v    ) ; }
	Idx emplace( int v1 , int v2 ) requires( HasData        &&  Multi ) { return file.emplace(2,v1,v2) ; }
	Idx emplace( int v           ) requires( HasData        && !Multi ) { return file.emplace(  v    ) ; }
	Idx emplace( int v           ) requires( HasSideCarOnly &&  Multi ) { Idx idx = file.emplace(1) ; file.side_car(idx).val[0]=v  ;                                return idx ; }
	Idx emplace( int v1 , int v2 ) requires( HasSideCarOnly &&  Multi ) { Idx idx = file.emplace(2) ; file.side_car(idx).val[0]=v1 ; file.side_car(idx).val[1]=v2 ; return idx ; }
	Idx emplace( int v           ) requires( HasSideCarOnly && !Multi ) { Idx idx = file.emplace( ) ; file.side_car(idx).val[0]=v  ;                                return idx ; }

	void test_hdr() requires(!HasHdr) {}
	void test_hdr() requires( HasHdr) {
		file.hdr() = 3 ;
		SWEAR(file.hdr()==3) ;
	}
	Idx test_emplace0() {
		Idx idx = emplace(4) ;
		SWEAR( at(idx).val[0]==4 ) ;
		return idx ;
	}
	Idx test_emplace1() requires( !HasData || !HasSideCar ) {
		return test_emplace0() ;
	}
	Idx test_emplace1() requires( HasData && HasSideCar ) {
		Idx idx = test_emplace0() ;
		file.side_car(idx) = 9 ;
		SWEAR( file.side_car(idx).val[0]==9 ) ;
		return idx ;
	}
	::pair<Idx,Idx> test_emplace2() requires(Multi) {
		Idx idx2 = emplace(5,6) ;
		Idx idx1 = test_emplace1() ;
		SWEAR( at(idx2).val[0]==5 ) ;
		SWEAR( at(idx2).val[1]==6 ) ;
		return {idx1,idx2} ;
	}
	template<bool AutoSz> void test_pop(Idx idx) {
		pop<AutoSz>(idx,1) ;
		Idx idx2 = emplace(7) ;
		SWEAR(idx2==idx) ;
		SWEAR( at(idx2).val[0]==7 ) ;
	}
	template<bool AutoSz> void test_shorten(Idx idx) {
		at(idx).sz = 1 ;
		shorten<AutoSz>(idx,2,1) ;
		SWEAR( at(idx).val[0]==5 ) ;
		Idx idx2 = emplace(8) ;
		SWEAR(idx2==idx+1) ;
		SWEAR( at(idx2).val[0]==8 ) ;
	}
	void test1_data() requires( !HasEither           ) {}
	void test1_data() requires(  HasEither && !Multi ) {
		Idx idx = test_emplace1() ;
		test_pop<true>(idx) ;
	}
	void test1_data() requires( Multi ) {
		::pair<Idx,Idx> idx = test_emplace2() ;
		test_pop    <false>(idx.first) ;
		test_shorten<false>(idx.second) ;
	}
	void test2_data() requires(!HasDataSz) {}
	void test2_data() requires( HasDataSz) {
		::pair<Idx,Idx> idx = test_emplace2() ;
		test_pop    <true>(idx.first) ;
		test_shorten<true>(idx.second) ;
	}
	TestSideCar() : file(g_dir_s+"side_car"+(HasHdr?"_hdr":"")+(HasData?"_data":"")+(Multi?"_multi":"")+(HasDataSz?"_datasz":"")+(HasSideCar?"_sidecar":""),true/*writable*/) {
		::string out = "check sidecar" ;
		if (HasHdr    ) out<<" with header"   ;
		if (HasData   ) out<<" with data"     ;
		if (Multi     ) out<<" with multi"    ;
		if (HasDataSz ) out<<" with datasize" ;
		if (HasSideCar) out<<" with sidecar"  ;
		out<<" ..." ;
		Fd::Stdout.write(out) ;
		test_hdr() ;
		test1_data() ;
		test2_data() ;
		Fd::Stdout.write(" ok\n") ;
	}

	// data
	SideCarFile< false , Hdr , Idx , 20 , Data , SideCar , Multi > file ;
} ;
void test_side_car() {
	TestSideCar<false/*HasHdr*/,false/*HasData*/,false/*HasSideCar*/,false/*Multi*/,false/*HasDataSz*/>() ;
	TestSideCar<true /*HasHdr*/,false/*HasData*/,false/*HasSideCar*/,false/*Multi*/,false/*HasDataSz*/>() ;
	TestSideCar<false/*HasHdr*/,true /*HasData*/,false/*HasSideCar*/,false/*Multi*/,false/*HasDataSz*/>() ;
	TestSideCar<true /*HasHdr*/,true /*HasData*/,false/*HasSideCar*/,false/*Multi*/,false/*HasDataSz*/>() ;
	TestSideCar<false/*HasHdr*/,true /*HasData*/,false/*HasSideCar*/,true /*Multi*/,false/*HasDataSz*/>() ;
	TestSideCar<true /*HasHdr*/,true /*HasData*/,false/*HasSideCar*/,true /*Multi*/,false/*HasDataSz*/>() ;
	TestSideCar<false/*HasHdr*/,true /*HasData*/,false/*HasSideCar*/,true /*Multi*/,true /*HasDataSz*/>() ;
	TestSideCar<true /*HasHdr*/,true /*HasData*/,false/*HasSideCar*/,true /*Multi*/,true /*HasDataSz*/>() ;
	TestSideCar<false/*HasHdr*/,false/*HasData*/,true /*HasSideCar*/,false/*Multi*/,false/*HasDataSz*/>() ;
	TestSideCar<true /*HasHdr*/,false/*HasData*/,true /*HasSideCar*/,false/*Multi*/,false/*HasDataSz*/>() ;
	TestSideCar<false/*HasHdr*/,false/*HasData*/,true /*HasSideCar*/,true /*Multi*/,false/*HasDataSz*/>() ;
	TestSideCar<true /*HasHdr*/,false/*HasData*/,true /*HasSideCar*/,true /*Multi*/,false/*HasDataSz*/>() ;
	TestSideCar<false/*HasHdr*/,true /*HasData*/,true /*HasSideCar*/,false/*Multi*/,false/*HasDataSz*/>() ;
	TestSideCar<true /*HasHdr*/,true /*HasData*/,true /*HasSideCar*/,false/*Multi*/,false/*HasDataSz*/>() ;
	TestSideCar<false/*HasHdr*/,true /*HasData*/,true /*HasSideCar*/,true /*Multi*/,false/*HasDataSz*/>() ;
	TestSideCar<true /*HasHdr*/,true /*HasData*/,true /*HasSideCar*/,true /*Multi*/,false/*HasDataSz*/>() ;
	TestSideCar<false/*HasHdr*/,true /*HasData*/,true /*HasSideCar*/,true /*Multi*/,true /*HasDataSz*/>() ;
	TestSideCar<true /*HasHdr*/,true /*HasData*/,true /*HasSideCar*/,true /*Multi*/,true /*HasDataSz*/>() ;
}

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
	SinglePrefixFile<false,Hdr,Idx,20,Char,Data,Reverse> file ;
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
	SinglePrefixFile<false,void,uint32_t,20> file(g_dir_s+"lmake",true/*writable*/) ;
	char key ;
	key = (char)0x28 ; file.insert(::string(&key,1)) ;
	key = (char)0xb1 ; file.insert(::string(&key,1)) ;
	key = (char)0xef ; file.insert(::string(&key,1)) ;
	Fd::Stdout.write(" ok\n") ;
}

int main( int argc , char const* argv[] ) {
	SWEAR(argc==2) ;
	g_dir_s = with_slash(argv[1]) ;
	Fd::Stdout.write("chk dir : "s+no_slash(g_dir_s)+'\n') ;
	test_file    () ;
	test_struct  () ;
	test_side_car() ;
	test_prefix  () ;
	test_lmake   () ;
	return 0 ;
}
