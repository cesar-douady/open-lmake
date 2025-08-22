// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
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

	template<char ThreadKey,size_t Capacity> struct File {
		// statics
	private :
		void _s_chk_rc(int rc) {
			if (rc<0) FAIL_PROD(rc,::strerror(errno)) ;
		}
		size_t _s_round_up(size_t sz) {
			static size_t s_page = ::sysconf(_SC_PAGESIZE) ;
			return round_up( sz , s_page ) ;
		}
		// cxtors & casts
	public :
		File () = default ;
		File ( NewType                                ) { init(New            ) ; }
		File ( ::string const& name_ , bool writable_ ) { init(name_,writable_) ; }
		~File(                                        ) { close()               ; }
		//
		File& operator=(File&& other) {
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
			chk_thread() ;
			//
			_alloc() ;
			if (+name) {
				FdAction action ;
				if (writable) { action = FdAction::CreateRead ; Disk::dir_guard(name) ; }
				else            action = FdAction::Read       ;
				//    vvvvvvvvvvvvvvvvvvvvv
				_fd = AcFd( name , action ) ;                                              // mode is only used if created, which implies writable
				//    ^^^^^^^^^^^^^^^^^^^^^
				if (writable) _s_chk_rc( ::lseek( _fd , 0/*offset*/ , SEEK_END ) ) ;       // ensure writes (when expanding) are done at end of file when resizing
				SWEAR_PROD(+_fd) ;
				Disk::FileInfo fi{_fd} ;
				SWEAR(fi.tag()>=FileTag::Reg) ;
				_map(fi.sz) ;
			}
		}
		void close() {
			chk_thread() ;
			//
			if (!base) return ;
			//
			_dealloc()  ;
			_fd.close() ;
		}
		// accesses
		bool operator+() const { return size ; }
		// services
		void expand(size_t sz) {
			chk_thread  () ;
			chk_writable() ;
			//
			if (sz<=size   ) return ;
			if (sz>Capacity)
				exit( Rc::Param
				,	"file ",name," capacity has been under-dimensioned at ",Capacity," bytes\n"
				,	"consider to recompile open-lmake with increased corresponding parameter in src/types.hh\n"
				) ;
			//
			sz = ::max( sz              , size + (size>>2) ) ;                             // ensure remaps are in log(n)
			sz = ::min( _s_round_up(sz) , Capacity         ) ;                             // legalize
			//                   vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			if (+_fd) _s_chk_rc( ::write( _fd , ::string(sz-size,0).data() , sz-size ) ) ; // /!\ dont use truncate to avoid race in kernel between truncate and write back of dirty pages
			//                   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
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
			//                   vvvvvvvvvvvvvvvvvvvvvvv
			if (+_fd) _s_chk_rc( ::ftruncate( _fd , sz ) ) ;                               // /!\ can use truncate here as there is no mapping, hence no page write back, hence no race
			//                   ^^^^^^^^^^^^^^^^^^^^^^^
			_alloc() ;
			_map(sz) ;
		}
		void chk         () const { if (+_fd     ) SWEAR( base                                                  ) ; }
		void chk_thread  () const { if (ThreadKey) SWEAR( t_thread_key==ThreadKey , ThreadKey,t_thread_key,name ) ; }
		void chk_writable() const { throw_unless( writable , name," is read-only" ) ;                               }
	private :
		void _dealloc() {
			SWEAR(base) ;
			_s_chk_rc( ::munmap( base , Capacity ) ) ;
			base = nullptr ;
		}
		void _alloc() {
			SWEAR(!base) ;
			base = static_cast<char*>( ::mmap( nullptr , Capacity , PROT_NONE , MAP_NORESERVE|MAP_PRIVATE|MAP_ANONYMOUS , -1  , 0 ) ) ;
			if (base==MAP_FAILED) FAIL_PROD(::strerror(errno)) ;
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
			if (actual!=base+size) FAIL_PROD(to_hex(size_t(base)),to_hex(size_t(actual)),size,sz,::strerror(errno)) ;
			size = sz ;
		}
		// data
	public :
		::string       name     ;
		char*          base     = nullptr ;                                                // address of mapped file
		Atomic<size_t> size     = 0       ;                                                // underlying file size (fake if no file)
		bool           writable = false   ;
	private :
		AcFd _fd ;
	} ;

}
