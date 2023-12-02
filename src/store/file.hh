// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <sys/mman.h>                                                          // mmap, mremap, munmap

#include "disk.hh"
#include "trace.hh"

#include "store_utils.hh"

// always consistent store
// memory leak is acceptable in case of crash
// inconsistent state is never acceptable

namespace Store {

	extern size_t g_page ;                                                     // cannot initialize directly as this may occur after first call to cxtor

	template<bool AutoLock> using UniqueLock = ::conditional_t<AutoLock,::unique_lock<::shared_mutex>,NoLock<::shared_mutex>> ;
	template<bool AutoLock> using SharedLock = ::conditional_t<AutoLock,::shared_lock<::shared_mutex>,NoLock<::shared_mutex>> ;

	template<bool AutoLock> struct File {
		using ULock = UniqueLock<AutoLock> ;
		using SLock = SharedLock<AutoLock> ;
		// cxtors & casts
		File (                                                           ) = default ;
		File ( NewType               , size_t capacity_                  ) { init(New  ,capacity_          ) ; }
		File ( ::string const& name_ , size_t capacity_ , bool writable_ ) { init(name_,capacity_,writable_) ; }
		~File(                                                           ) { if (base) close() ;               }
		//
		File& operator=(File&& other) {
			close() ;
			name     = ::move(other.name    ) ;
			base     =        other.base      ; other.base     = nullptr ;
			size     =        other.size      ; other.size     = 0       ;
			capacity =        other.capacity  ; other.capacity = 0       ;
			writable =        other.writable  ; other.writable = false   ;
			_fd      = ::move(other._fd     ) ;
			return *this ;
		}
		//
		void init( NewType               , size_t capacity_                  ) { init("",capacity_,true) ; }
		void init( ::string const& name_ , size_t capacity_ , bool writable_ ) {
			name     = name_     ;
			writable = writable_ ;
			if (!g_page) g_page = ::sysconf(_SC_PAGESIZE) ;
			capacity = round_up( capacity_ , g_page ) ;
			//
			ULock lock{_mutex} ;
			if (name.empty()) {
				size = 0 ;
			} else {
				int open_flags = O_LARGEFILE | O_CLOEXEC ;
				if (writable) { open_flags |= O_RDWR | O_CREAT ; Disk::dir_guard(name) ; }
				else          { open_flags |= O_RDONLY         ;                         }
				//      vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				_fd = ::open( name.c_str() , open_flags , 0644 ) ;             // mode is only used if created, which implies writable
				//      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				SWEAR_PROD(+_fd) ;
				Disk::FileInfo fi{_fd} ;
				SWEAR(fi.is_reg()) ;
				size = fi.sz ;
			}
			_resize_file(size) ;
			_alloc() ;
			_map() ;
		}
		void close() {
			ULock lock{_mutex} ;
			_dealloc() ;
			_fd.close() ;
		}
		// accesses
		bool empty() const { return !size ; }
		// services
		void lock           ()       {        _mutex.lock           () ; }     // behave as a mutex, so file can be used in ::unique_lock et al.
		bool try_lock       ()       { return _mutex.try_lock       () ; }     // but if AutoLock, then there is no reason to access it from outside
		void unlock         ()       {        _mutex.unlock         () ; }     // .
		void lock_shared    () const {        _mutex.lock_shared    () ; }     // .
		bool try_lock_shared() const { return _mutex.try_lock_shared() ; }     // .
		void unlock_shared  () const {        _mutex.unlock_shared  () ; }     // .
		void expand(size_t sz) {
			if (sz<=size) return ;                                             // fast path
			ULock lock{_mutex} ;
			if ( AutoLock && sz<=size ) return ;                               // redo size check, now that we have the lock
			_resize_file(::max( sz , size + (size>>2) )) ;                     // ensure remaps are in log(n)
			_map() ;
		}
		void clear(size_t sz=0) {
			ULock lock{_mutex} ;
			_dealloc() ;
			_resize_file(sz) ;
			_alloc() ;
			_map() ;
		}
		void chk() const {
			if (+_fd) SWEAR(base) ;
		}
	private :
		void _dealloc() {
			SWEAR(base) ;
			int rc = ::munmap( base , capacity ) ;
			if (rc!=0) FAIL_PROD(rc,strerror(errno)) ;
			base = nullptr ;
		}
		void _alloc() {
			SWEAR(!base) ;
			base = static_cast<char*>( ::mmap( nullptr , capacity , PROT_NONE , MAP_NORESERVE|MAP_PRIVATE|MAP_ANONYMOUS , -1  , 0 ) ) ;
			if (base==MAP_FAILED) FAIL_PROD(strerror(errno)) ;
		}
		void _map() {
			if (!size) return ;
			//
			int map_prot  = PROT_READ ;
			int map_flags = 0         ;
			if (writable    ) map_prot  |= PROT_WRITE ;
			if (name.empty()) map_flags |= MAP_PRIVATE | MAP_ANONYMOUS ;
			else              map_flags |= MAP_SHARED ;
			//
			void* actual = ::mmap( base , size , map_prot , MAP_FIXED|map_flags , _fd , 0 ) ;
			if (base!=actual) FAIL_PROD(hex,size_t(base),size_t(actual),dec,size,strerror(errno)) ;
		}
		void _resize_file(size_t sz) {
			swear_prod( writable , name , " is read-only" ) ;
			SWEAR     ( sz<=capacity , sz , capacity      ) ;
			sz = round_up(sz,g_page) ;
			if (+_fd) {
				//         vvvvvvvvvvvvvvvvvvvvv
				int rc = ::ftruncate( _fd , sz ) ;                             // may increase file size
				//         ^^^^^^^^^^^^^^^^^^^^^
				if (rc!=0) FAIL_PROD(rc,strerror(errno)) ;
			}
			fence() ;                                                          // update state when it is legal to do so
			size = sz ;
		}
		// data
	public :
		::string         name     ;
		char*            base     = nullptr ;                                  // address of mapped file
		::atomic<size_t> size     = 0       ;                                  // underlying file size (fake if no file)
		size_t           capacity = 0       ;                                  // max size that can ever be allocated
		bool             writable = false   ;
	protected :
		mutable ::shared_mutex _mutex ;
	private :
		AutoCloseFd _fd ;
	} ;

}
