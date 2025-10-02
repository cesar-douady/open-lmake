// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "record.hh"

#pragma once

Record& auditor() ;

struct AutodepLock {
	// static data
	static thread_local bool t_active ;
private :
	static Mutex<MutexLvl::Autodep> _s_mutex ;
	// cxtors & casts
public :
	AutodepLock(                                 ) = default ;
	AutodepLock(::vmap_s<DepDigest>* deps=nullptr) ;
	//
	~AutodepLock() ;
	// data
	Lock<Mutex<MutexLvl::Autodep>> lock ;
	::string                       err  ;
} ;
