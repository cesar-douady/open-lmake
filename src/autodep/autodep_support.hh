// This file is part of the lmake distribution (https://wgit.doliam.net/cdy/lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "rpc_job.hh"

struct Cmd {
	bool sync     = false ;
	bool has_args = false ;
	bool has_ok   = false ;
	bool has_crcs = false ;
} ;

extern const umap<JobExecRpcProc,Cmd> g_proc_tab ;

struct Ctx {
	int get_errno     () const { return errno ; }
	void save_errno   ()       {                }
	void restore_errno()       {                }
} ;

struct Lock {
	static bool s_busy() { return false ; }
} ;

#include "autodep_ld.hh"
#include "gather_deps.hh"

struct AutodepSupport : AutodepEnv {
	// cxtors & casts
	AutodepSupport(       ) = default ;
	AutodepSupport(NewType) : AutodepEnv{get_env("LMAKE_AUTODEP_ENV")} {}
	// services
	JobExecRpcReply req(JobExecRpcReq const& jerr) ;
} ;
