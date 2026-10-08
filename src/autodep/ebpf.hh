// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "utils.hh"

namespace AutodepEbpf {
	// must be called before entering namespaces as the daemon and the physical dirs must be reachable, throw a ::string on error
	void load( bool deps_in_system , ::string const& phy_repo_root_s , ::vector_s const& src_dirs_s ) ;
}
