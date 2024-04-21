// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "utils.hh"

::pair<char*,ssize_t> fix_cwd( char* buf , size_t buf_sz , ssize_t sz , Bool3 allocated=No ) noexcept ;

struct SyscallDescr {
	static constexpr long NSyscalls = 1024 ;              // must larger than higher syscall number, 1024 is plenty, actual upper value is around 450
	using Tab = ::array<SyscallDescr,NSyscalls> ;         // must be an array and not an umap so as to avoid calls to malloc before it is known to be safe
	// static data
	static Tab const& s_tab() ;                           // ptrace does not support tmp mapping, which simplifies table a bit
	// accesses
	constexpr bool operator+() const { return prio    ; } // prio=0 means entry is not allocated
	constexpr bool operator!() const { return !+*this ; }
	// data
	void           (*entry)( void*& , Record& , pid_t , uint64_t args[6] , const char* comment ) = nullptr ;
	int64_t/*res*/ (*exit )( void*  , Record& , pid_t , int64_t res                            ) = nullptr ;
	int            filter                                                                        = 0       ; // argument to filter on when known to require no processing
	uint8_t        prio                                                                          = 0       ; // prio for libseccomp (0 means entry is not allocated)
	bool           data_access                                                                   = false   ;
	const char*    comment                                                                       = nullptr ;
} ;
