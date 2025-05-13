// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "utils.hh"

enum class LnkSupport : uint8_t {
	None
,	File
,	Full
} ;

struct SearchRootResult {
	::string top_s     ;
	::string sub_s     ;
	::string startup_s ;
} ;

SearchRootResult search_root(::string const& cwd_s={}) ; // use cwd_s() by default
