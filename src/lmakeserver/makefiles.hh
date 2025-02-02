// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "time.hh"

#pragma once

namespace Engine::Makefiles {
	::string/*msg*/ refresh        ( bool chk, bool refresh        ) ;
	::string/*msg*/ dynamic_refresh( ::string const& startup_dir_s ) ; // startup dir for diag purpose only
}
