// This file is part of the lmake distribution (https://wgit.doliam.net/cdy/lmake.git)
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

	template<bool AutoLock> struct File ;

	template<bool AutoLock> using UniqueLock = ::conditional_t<AutoLock,::unique_lock<::shared_mutex>,NoLock<::shared_mutex>> ;
	template<bool AutoLock> using SharedLock = ::conditional_t<AutoLock,::shared_lock<::shared_mutex>,NoLock<::shared_mutex>> ;

	template<bool AutoLock> struct File {
		using ULock = UniqueLock<AutoLock> ;
		using SLock = SharedLock<AutoLock> ;
		// cxtors & casts
		File (                                                           ) = default ;
		File ( NewType               , size_t capacity_                  ) { init(New  ,capacity_          ) ; }
		File ( ::string const& name_ , size_t capacity_ , bool writable_ ) { init(name_,capacity_,writable_) ; }
		~File(                                                           ) { close() ;                         }
		//
		void init( NewType               , size_t capacity_                  ) { init("",capacity_,true) ; }
		void init( ::string const& name_ , size_t capacity_ , bool writable_ ) {
			name     = name_     ;
			writable = writable_ ;
			if (!g_page) g_page = ::sysconf(_SC_PAGESIZE) ;
			capacity = round_up( capacity_ , g_page ) ;
			//
			int map_prot  = PROT_READ     ;
			int map_flags = MAP_NORESERVE ;
			if (name.empty()) {
				size       = capacity ;
				map_flags |= MAP_PRIVATE | MAP_ANONYMOUS ;
			} else {
				int open_flags = O_LARGEFILE | O_CLOEXEC ;
				if (writable) { open_flags |= O_RDWR | O_CREAT ; Disk::dir_guard(name) ; }
				else          { open_flags |= O_RDONLY         ;                         }
				//      vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				_fd = ::open( name.c_str() , open_flags , 0644 ) ;
				//      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				SWEAR_PROD(+_fd) ;
				Disk::FileInfo fi{_fd} ;
				SWEAR(fi.is_reg()) ;
				size       = fi.sz      ;
				map_flags |= MAP_SHARED ;
			}
			if (writable) map_prot |= PROT_WRITE ;
			//                          vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			base = static_cast<char*>(::mmap( nullptr , capacity , map_prot , map_flags , _fd , 0 )) ;
			//                          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			swear(base!=MAP_FAILED,strerror(errno)) ;
		}
		void close() {
			if (base) {
				int rc = ::munmap( base , capacity ) ;
				SWEAR(rc==0) ;
			}
			_fd.close() ;
		}
		// accesses
		bool empty() const { SLock lock{_mutex} ; return !size ; }
		// services
		void lock           ()       {        _mutex.lock           () ; }     // behave as the mutex, so file can be used in ::unique_lock et al.
		bool try_lock       ()       { return _mutex.try_lock       () ; }     // but if AutoLock, then there is no reason to access it from outside
		void unlock         ()       {        _mutex.unlock         () ; }     // .
		void lock_shared    () const {        _mutex.lock_shared    () ; }     // .
		bool try_lock_shared() const { return _mutex.try_lock_shared() ; }     // .
		void unlock_shared  () const {        _mutex.unlock_shared  () ; }     // .
		void expand(size_t size_) {
			if (size_<=size) return ;                                          // fast path
			ULock lock{_mutex} ;
			if ( AutoLock && size_<=size ) return ;                            // redo size check, now that we have the lock
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			_resize(::max( size_ , size + (size>>2) )) ;                       // ensure remaps are in log(n)
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		}
		void clear(size_t size_=0) {
			ULock lock{_mutex} ;
			_resize(size_) ;
		}
		void chk() const {
			if (+_fd) SWEAR(base) ;
		}
	private :
		void _resize(size_t size_) {
			SWEAR_PROD(writable       ) ;
			SWEAR     (size_<=capacity) ;
			size_ = round_up( size_ , g_page ) ;
			if (+_fd) {
				//         vvvvvvvvvvvvvvvvvvvvvvvv
				int rc = ::ftruncate( _fd , size_ ) ;                          // may increase file size
				//         ^^^^^^^^^^^^^^^^^^^^^^^^
				SWEAR_PROD(rc==0) ;
			}
			fence() ;                                                          // update state when it is legal to do so
			size = size_ ;
		}
		// data
	public :
		::string name     ;
		char*    base     = nullptr ;                                          // address of mapped file
		size_t   size     = 0       ;                                          // underlying file size (fake if no file)
		size_t   capacity = 0       ;                                          // max size that can ever be allocated
		bool     writable = false   ;
	protected :
		mutable ::shared_mutex _mutex ;
	private :
		AutoCloseFd _fd ;
	} ;

}
