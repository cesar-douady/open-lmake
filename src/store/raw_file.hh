// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "disk.hh"
#include "trace.hh"

#include "store_utils.hh"

// always consistent store
// memory leak is acceptable in case of crash
// inconsistent state is never acceptable

namespace Store {

	template<char ThreadKey,size_t Capacity> struct RawFile {
		// cxtors & co
		RawFile () = default ;
		RawFile ( NewType                                ) { init(New            ) ; }
		RawFile ( ::string const& name_ , bool writable_ ) { init(name_,writable_) ; }
		~RawFile(                                        ) { close()               ; }
		//
		RawFile& operator=(RawFile&& other) {
			close() ;
			name     = ::move(other.name    ) ;
			base     =        other.base      ; other.base     = nullptr ;
			size     =        other.size      ; other.size     = 0       ;
			writable =        other.writable  ; other.writable = false   ;
			return self ;
		}
		//
		void init( NewType                                ) { init({},true/*writable_*/) ; }
		void init( ::string const& name_ , bool writable_ ) {
			name     = name_     ;
			writable = writable_ ;
			//
			SWEAR(!base) ;
			base = static_cast<char*>( ::mmap( nullptr/*addr*/ , Capacity , PROT_NONE , MAP_NORESERVE|MAP_PRIVATE|MAP_ANONYMOUS , -1/*fd*/  , 0/*offfset*/ ) ) ;
			_chk_rc( base!=MAP_FAILED , "reserve address space" ) ;
			if (+name) _map( Disk::FileInfo(name).sz , false/*truncate*/ ) ;
		}
		void close() {
			if (!base) return ;
			_chk_rc( ::munmap(base,Capacity)==0 , "unmap" ) ;
			base = nullptr ;
		}
		bool operator+() const { return size ; }
		// services
		void expand( size_t sz , bool thread_chk=true ) {
			if (thread_chk) chk_thread  () ;
			/**/            chk_writable() ;
			//
			if (sz<=size   ) return ;
			if (sz>Capacity)
				exit( Rc::BadState
				,	"file ",name," capacity has been under-dimensioned at ",Capacity," bytes\n"
				,	"\tconsider to recompile open-lmake with increased corresponding parameter in src/repo.hh\n"
				) ;
			//
			sz = ::max( sz                    , size+(size>>2) ) ;                                                   // ensure remaps are in log(n)
			sz = ::min( round_up<PAGE_SZ>(sz) , Capacity       ) ;                                                   // legalize
			_map( sz , true/*truncate*/ ) ;
		}
		void clear(size_t sz=0) {
			chk_thread  () ;
			chk_writable() ;
			//
			sz = round_up<PAGE_SZ>(sz) ; SWEAR( sz<=Capacity , sz,Capacity ) ;
			//
			::mmap( base , size , PROT_NONE , MAP_NORESERVE|MAP_PRIVATE|MAP_ANONYMOUS , -1/*fd*/  , 0/*offfset*/ ) ; // reset old mapping
			size = 0 ;
			_map( sz , true/*truncate*/ ) ;
		}
		void chk         () const { if (+name    ) SWEAR( base                                                  ) ; }
		void chk_thread  () const { if (ThreadKey) SWEAR( t_thread_key==ThreadKey , ThreadKey,t_thread_key,name ) ; }
		void chk_writable() const { throw_unless( writable , name," is read-only" ) ;                               }
	private :
		void _chk_rc( bool ok , const char* msg ) {
			if (!ok) exit( Rc::System , "cannot ",msg," (",StrErr(),") ",name ) ;
		}
		void _map( size_t sz , bool truncate ) {
			SWEAR(sz>=size) ;
			if (sz==size) return ;
			//
			int  map_prot  = writable ? PROT_READ|PROT_WRITE : PROT_READ                 ;
			int  map_flags = +name    ? MAP_SHARED           : MAP_PRIVATE|MAP_ANONYMOUS ;
			AcFd fd        ;
			//
			if (+name) {
				fd = AcFd( name , {writable?O_RDWR|O_CREAT:O_RDONLY} ) ;
				if (truncate) {
					_chk_rc( ::ftruncate(fd,sz)==0 , "resize" ) ;
					try                       { fd = AcFd( name , {writable?O_RDWR:O_RDONLY} ) ; } // reopen to ensure no race between ftruncate and mmap, .e.g. BeeGFS with tuneCoherentBuffers=0
					catch (::string const& e) { _chk_rc( false/*ok*/ , "reopen" ) ;              }
				}
			}
			//
			_chk_rc( ::mmap( base , sz , map_prot , MAP_FIXED|map_flags , fd , 0/*offset*/ )!=MAP_FAILED , "map" ) ;
			size = sz ;
		}
		// data
	public :
		::string       name     ;
		char*          base     = nullptr ;                                                // address of mapped file
		Atomic<size_t> size     ;                                                          // underlying file size (fake if no file)
		bool           writable = false   ;
	} ;

}
