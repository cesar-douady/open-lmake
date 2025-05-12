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
	//                                                                                             Shared Shared                            Shared
	template<bool AutoLock> using UniqueLock = ::conditional_t<AutoLock,::Lock<Mutex<MutexLvl::File,true>,false>,NoLock<Mutex<MutexLvl::File,true>>> ;
	template<bool AutoLock> using SharedLock = ::conditional_t<AutoLock,::Lock<Mutex<MutexLvl::File,true>,true >,NoLock<Mutex<MutexLvl::File,true>>> ;

	template<bool AutoLock,size_t Capacity> struct File {
		using ULock = UniqueLock<AutoLock> ;
		using SLock = SharedLock<AutoLock> ;
		// cxtors & casts
		File () = default ;
		File ( NewType                                ) { init(New            ) ; }
		File ( ::string const& name_ , bool writable_ ) { init(name_,writable_) ; }
		~File() {
			if      (keep_open) _fd.detach() ;
			else if (base     ) close()      ;
		}
		//
		File& operator=(File&& other) ;
		//
		void init( ::string const& name_ , bool writable_ ) ;
		void init( NewType                                ) { init("",true/*writable_*/) ; }
		void close() {
			ULock lock{_mutex} ;
			_dealloc() ;
			_fd.close() ;
		}
		// accesses
		bool operator+() const { return size ; }
		// services
		void chk_writable() const {
			throw_unless( writable , name," is read-only" ) ;
		}
		void expand(size_t sz) {
			if (sz<=size) return ;                         // fast path
			ULock lock{_mutex} ;
			if ( AutoLock && sz<=size ) return ;           // redo size check, now that we have the lock
			size_t old_size = size ;
			_resize_file(::max( sz , size + (size>>2) )) ; // ensure remaps are in log(n)
			_map(old_size) ;
		}
		void clear(size_t sz=0) {
			ULock lock{_mutex} ;
			_clear(sz) ;
		}
		void chk() const {
			if (+_fd) SWEAR(base) ;
		}
	protected :
		void _clear(size_t sz=0) {
			_dealloc() ;
			_resize_file(sz) ;
			_alloc() ;
			_map(0) ;
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
		bool           keep_open = false   ;
	protected :
		Mutex<MutexLvl::File,true/*Shared*/> mutable _mutex ;
	private :
		AcFd _fd ;
	} ;

	template<bool AutoLock,size_t Capacity> File<AutoLock,Capacity>& File<AutoLock,Capacity>::operator=(File&& other) {
		close() ;
		name      = ::move(other.name     ) ;
		base      =        other.base       ; other.base      = nullptr ;
		size      =        other.size       ; other.size      = 0       ;
		writable  =        other.writable   ; other.writable  = false   ;
		keep_open =        other.keep_open  ; other.keep_open = false   ;
		_fd       = ::move(other._fd      ) ;
		return self ;
	}

	template<bool AutoLock,size_t Capacity> void File<AutoLock,Capacity>::init( ::string const& name_ , bool writable_ ) {
		name     = name_     ;
		writable = writable_ ;
		//
		ULock lock { _mutex } ;
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
			Disk::FileInfo fi{_fd} ;
			SWEAR(fi.tag()>=FileTag::Reg) ;
			size = fi.sz ;
		}
		_alloc() ;
		_map(0) ;
	}

	template<bool AutoLock,size_t Capacity> void File<AutoLock,Capacity>::_map(size_t old_size) {
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

	template<bool AutoLock,size_t Capacity> void File<AutoLock,Capacity>::_resize_file(size_t sz) {
		static size_t s_page = ::sysconf(_SC_PAGESIZE) ;
		if (sz>Capacity) {
			::string err_msg ;
			err_msg<<"file "<<name<<" capacity has been under-dimensioned at "<<Capacity<<" bytes\n"             ;
			err_msg<<"consider to recompile open-lmake with increased corresponding parameter in src/types.hh\n" ;
			exit(Rc::Param,err_msg) ;
		}
		chk_writable() ;
		sz += s_page-1 ; sz = sz-sz%s_page ;   // round up
		if (+_fd) {
			//         vvvvvvvvvvvvvvvvvvvvv
			int rc = ::ftruncate( _fd , sz ) ; // may increase file size
			//         ^^^^^^^^^^^^^^^^^^^^^
			if (rc!=0) FAIL_PROD(rc,::strerror(errno)) ;
		}
		fence() ;                              // update state when it is legal to do so
		size = sz ;
	}

}
