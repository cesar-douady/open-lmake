// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "disk.hh"
#include "trace.hh"

#include "store_utils.hh"

#define NO_HOLE 1 // if true => avoid creating file holes

// always consistent store
// memory leak is acceptable in case of crash
// inconsistent state is never acceptable

namespace Store {

	template<char ThreadKey,size_t Capacity> struct File {
		// cxtors & casts
		File () = default ;
		File ( NewType                                ) { init(New            ) ; }
		File ( ::string const& name_ , bool writable_ ) { init(name_,writable_) ; }
		~File(                                        ) { close()               ; }
		//
		File& operator=(File&& other) ;
		//
		void init( ::string const& name_ , bool writable_ ) ;
		void init( NewType                                ) { init("",true/*writable_*/) ; }
		void close() {
			if (!base) return ;
			chk_thread() ;
			_dealloc()   ;
			::fsync(_fd) ;                                 // XXX> : suppress when bug is found
			_fd.close()  ;
		}
		// accesses
		bool operator+() const { return size ; }
		// services
		void chk_writable() const {
			throw_unless( writable , name," is read-only" ) ;
		}
		void expand(size_t sz) {
			chk_thread() ;
			if (sz<=size) return ;                         // fast path
			size_t old_size = size ;
			_resize_file(::max( sz , size + (size>>2) )) ; // ensure remaps are in log(n)
			_map(old_size) ;
		}
		void clear(size_t sz=0) {
			chk_thread() ;
			_dealloc() ;
			_resize_file(sz) ;
			_alloc() ;
			_map(0) ;
		}
		void chk() const {
			if (+_fd) SWEAR(base) ;
		}
		void chk_thread() const {
			if (ThreadKey) SWEAR(t_thread_key==ThreadKey,t_thread_key) ;
		}
	private :
		void _dealloc() {
			SWEAR(base) ;
			int rc = ::munmap( base , Capacity ) ;
			if (rc!=0) FAIL_PROD(rc,::strerror(errno)) ;
			base = nullptr ;
		}
		void _alloc() {
			SWEAR(!base) ;
			base = static_cast<char*>( ::mmap( nullptr , Capacity , PROT_NONE , MAP_NORESERVE|MAP_PRIVATE|MAP_ANONYMOUS , -1  , 0 ) ) ;
			if (base==MAP_FAILED) FAIL_PROD(::strerror(errno)) ;
		}
		void _map        (size_t old_size) ;
		void _resize_file(size_t sz      ) ;
		// data
	public :
		::string       name      ;
		char*          base      = nullptr ;               // address of mapped file
		Atomic<size_t> size      = 0       ;               // underlying file size (fake if no file)
		bool           writable  = false   ;
	private :
		AcFd _fd ;
	} ;

	template<char ThreadKey,size_t Capacity> File<ThreadKey,Capacity>& File<ThreadKey,Capacity>::operator=(File&& other) {
		chk_thread() ;
		close() ;
		name      = ::move(other.name     ) ;
		base      =        other.base       ; other.base      = nullptr ;
		size      =        other.size       ; other.size      = 0       ;
		writable  =        other.writable   ; other.writable  = false   ;
		_fd       = ::move(other._fd      ) ;
		return self ;
	}

	template<char ThreadKey,size_t Capacity> void File<ThreadKey,Capacity>::init( ::string const& name_ , bool writable_ ) {
		chk_thread() ;
		name     = name_     ;
		writable = writable_ ;
		//
		if (!name) {
			size = 0 ;
		} else {
			int open_flags = O_CLOEXEC ;
			if (writable) { open_flags |= O_RDWR | O_CREAT ; Disk::dir_guard(name) ; }
			else            open_flags |= O_RDONLY         ;
			//      vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			_fd = ::open( name.c_str() , open_flags , 0644 ) ; // mode is only used if created, which implies writable
			//      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			SWEAR_PROD(+_fd) ;
			#if NO_HOLE
				::lseek( _fd , 0/*offset*/ , SEEK_END ) ;      // ensure writes are done at end of file when resizing
			#endif
			Disk::FileInfo fi{_fd} ;
			SWEAR(fi.tag()>=FileTag::Reg) ;
			size = fi.sz ;
		}
		_alloc() ;
		_map(0) ;
	}

	template<char ThreadKey,size_t Capacity> void File<ThreadKey,Capacity>::_map(size_t old_size) {
		SWEAR(size>=old_size) ;
		if (size==old_size) return ;
		//
		int map_prot  = PROT_READ ;
		int map_flags = 0         ;
		if (writable) map_prot  |= PROT_WRITE                  ;
		if (!name   ) map_flags |= MAP_PRIVATE | MAP_ANONYMOUS ;
		else          map_flags |= MAP_SHARED                  ;
		//
		void* actual = ::mmap( base+old_size , size-old_size , map_prot , MAP_FIXED|map_flags , _fd , old_size ) ;
		if (actual!=base+old_size) FAIL_PROD(to_hex(size_t(base)),to_hex(size_t(actual)),old_size,size,::strerror(errno)) ;
	}

	template<char ThreadKey,size_t Capacity> void File<ThreadKey,Capacity>::_resize_file(size_t sz) {
		static size_t s_page = ::sysconf(_SC_PAGESIZE) ;
		if (sz>Capacity) {
			::string err_msg ;
			err_msg<<"file "<<name<<" capacity has been under-dimensioned at "<<Capacity<<" bytes\n"             ;
			err_msg<<"consider to recompile open-lmake with increased corresponding parameter in src/types.hh\n" ;
			exit(Rc::Param,err_msg) ;
		}
		chk_writable() ;
		sz += s_page-1 ; sz = sz-sz%s_page ;                                                                             // round up
		if (+_fd) {
			int rc ;
			#if NO_HOLE
				//                  vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				if (sz>size) rc = ::write    ( _fd , ::string(sz-size,1).data() , sz-size )==ssize_t(sz-size) ? 0 : -1 ; // expand by writing instead of truncate to ensure no file hole ...
				else         rc = ::ftruncate( _fd ,                              sz      )                            ; // ... (and write anything else than full 0)
				//                  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			#else
				//     vvvvvvvvvvvvvvvvvvvvv
				rc = ::ftruncate( _fd , sz ) ;                                                                           // may increase file size
				//     ^^^^^^^^^^^^^^^^^^^^^
			#endif
			if (rc!=0) FAIL_PROD(rc,::strerror(errno)) ;
		}
		fence() ;                                                                                                        // update state when it is legal to do so
		size = sz ;
	}

}
