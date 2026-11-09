// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "ebpf_client.hh"

#include "utils.hh"

struct AutodepEbpf {
	struct Consumer {
		void handle_path_res(                       EbpfDaemon::Path const& path                               ) ;
		void handle_event   ( EbpfEventHdr const& , EbpfDaemon::Path const& file , EbpfDaemon::Path const& dst ) ;
	} ;
	static int/*rc*/  s_prepare_child    (void*                      ) ; // must be called from child
	static bool/*ok*/ s_register_mount_ok(::string const& mount_point) ;
	// cxtors & co
	AutodepEbpf(NewType) ;
	// services
	int/*wstatus*/ process(pid_t child_pid) ;
	// data
	EbpfDaemon::RingBuf<Consumer> ring_buf ;
	::vector_s                    roots    ;
} ;

