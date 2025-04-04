// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "utils.hh"

namespace Backends::Slurm::SlurmApi {

	extern ::umap<size_t,off_t>        version_offset_tab ; // map slurm_conf_t size to version field offset (hoping there are no ambiguities)
	extern ::umap_s<::function<void()> init_tab           ; // map slurm version to init function
	extern void*                       lib_handler        ; // handler for libslurm.so as returned by ::dlsym

}
