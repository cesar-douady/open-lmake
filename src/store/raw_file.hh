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
		// statics
	private :
		static size_t _s_round_up(size_t sz) {
			static size_t s_page = ::sysconf(_SC_PAGESIZE) ;
			return round_up( sz , s_page ) ;
		}
		// cxtors & casts
	public :
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
			_fd      = ::move(other._fd     ) ;
			return self ;
		}
		//
		void init( NewType                                ) { init({},true/*writable_*/) ; }
		void init( ::string const& name_ , bool writable_ ) {
			name     = name_     ;
			writable = writable_ ;
			//
			_alloc() ;
			if (+name) {
				//    vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				_fd = AcFd( name , {writable?O_RDWR|O_CREAT:O_RDONLY,0666/*mod*/} ) ;  // mode is only used if created, which implies writable
				//    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				if (writable) _chk_rc( ::lseek(_fd,0/*offset*/,SEEK_END) , "lseek" ) ; // ensure writes (when expanding) are done at end of file when resizing
				SWEAR_PROD(+_fd) ;
				Disk::FileInfo fi{_fd} ;
				SWEAR( fi.tag()>=FileTag::Reg , fi ) ;
				_map(fi.sz) ;
			}
		}
		void close() {
			if (!base) return ;
			//
			_dealloc()  ;
			_fd.close() ;
		}
		// accesses
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
			sz = ::max( sz              , size+::min(size>>2,size_t(1<<24)) ) ;        // ensure remaps are in log(n) (up to a reasonable size increase)
			sz = ::min( _s_round_up(sz) , Capacity                          ) ;        // legalize
			if (+_fd)
				try                     { _fd.write(::string(sz-size,0/*ch*/)) ; }     // /!\ dont use ftruncate to avoid race in kernel between ftruncate and write back of dirty pages
				catch (::string const&) { _chk_rc(-1,"expand") ;                 }     // NO_COV
			_map(sz) ;
		}
		void clear(size_t sz=0) {
			chk_thread  () ;
			chk_writable() ;
			//
			sz = _s_round_up(sz) ;
			SWEAR( sz<=Capacity , sz,Capacity ) ;
			//
			_dealloc() ;
			//                 vvvvvvvvvvvvvvvvvvv
			if (+_fd) _chk_rc( ::ftruncate(_fd,sz) , "truncate" ) ;                    // /!\ can use ftruncate here as there is no mapping, hence no page write back, hence no race
			//                 ^^^^^^^^^^^^^^^^^^^
			_alloc() ;
			_map(sz) ;
		}
		void chk         () const { if (+_fd     ) SWEAR( base                                                  ) ; }
		void chk_thread  () const { if (ThreadKey) SWEAR( t_thread_key==ThreadKey , ThreadKey,t_thread_key,name ) ; }
		void chk_writable() const { throw_unless( writable , name," is read-only" ) ;                               }
	private :
		void _chk_rc( int rc, const char* msg ) {
			if (rc<0) fail_prod("cannot",msg,'(',StrErr(),") for file :",name) ;
		}
		void _dealloc() {
			SWEAR(base) ;
			_chk_rc( ::munmap(base,Capacity) , "unmap" ) ;
			base = nullptr ;
		}
		void _alloc() {
			SWEAR(!base) ;
			base = static_cast<char*>( ::mmap( nullptr/*addr*/ , Capacity , PROT_NONE , MAP_NORESERVE|MAP_PRIVATE|MAP_ANONYMOUS , -1/*d*/  , 0/*offfset*/ ) ) ;
			if (base==MAP_FAILED) FAIL_PROD(StrErr()) ;
			size = 0 ;
		}
		void _map(size_t sz) {
			SWEAR(sz>=size) ;
			if (sz==size) return ;
			//
			int map_prot  = PROT_READ ;
			int map_flags = 0         ;
			if (writable) map_prot  |= PROT_WRITE                  ;
			if (!name   ) map_flags |= MAP_PRIVATE | MAP_ANONYMOUS ;
			else          map_flags |= MAP_SHARED                  ;
			//
			void* actual = ::mmap( base+size , sz-size , map_prot , MAP_FIXED|map_flags , _fd , size ) ;
			if (actual!=base+size) FAIL_PROD(to_hex(size_t(base)),to_hex(size_t(actual)),size,sz,StrErr()) ;
			size = sz ;
		}
		// data
	public :
		::string       name     ;
		char*          base     = nullptr ;                                            // address of mapped file
		Atomic<size_t> size     = 0       ;                                            // underlying file size (fake if no file)
		bool           writable = false   ;
	private :
		AcFd _fd ;
	} ;

}
