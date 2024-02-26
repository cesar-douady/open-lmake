// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "record.hh"

#pragma once

struct AutodepLock {
    // static data
    static thread_local bool t_active ;
private :
    static ::mutex _s_mutex ;
    // cxtors & casts
public :
    AutodepLock (                                ) = default ;
    AutodepLock (::vmap_s<Accesses>* deps=nullptr) : lock{_s_mutex} {
        SWEAR( !Record::s_deps && !Record::s_deps_err ) ;
        Record::s_deps     = deps ;
        Record::s_deps_err = &err ;
        t_active           = true ;
    }
    ~AutodepLock() {
        Record::s_deps     = nullptr ;
        Record::s_deps_err = nullptr ;
        t_active           = false   ;
    }
    // data
    ::unique_lock<::mutex> lock ;
    ::string               err  ;
} ;
