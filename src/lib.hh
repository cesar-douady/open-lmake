// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "utils.hh"

static constexpr char AdminDir[] = "LMAKE" ;
static constexpr char DfltTmp [] = "/tmp"  ;

ENUM( LnkSupport
,	None
,	File
,	Full
)

// lib_complete may be called before main(), which means static strings are forbidden as they could be initialized by the compiler after us
extern ::string* g_tmp_dir   ;         // pointer to avoid init/fini order hazard, absolute              , tmp dir
extern ::string* g_root_dir  ;         // pointer to avoid init/fini order hazard, absolute              , root of repository

::pair_ss search_root_dir(::string const& cwd) ;
::pair_ss search_root_dir(                   ) ;

void lib_init(::string const& root_dir) ;
