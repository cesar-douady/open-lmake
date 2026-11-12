// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// Job side of the ebpf autodep method.
// The kernel (ebpf.bpf.c) reports, for each syscall of the job accessing files of interest :
// - captures : resolved paths (relative to the repo or to a source dir) together with file info before the syscall modifies anything, and traversed symlinks with their target
// - an End event which closes them and carries the syscall number, its result and its args
// Here, we turn that into the same access reports as the other autodep methods do (cf. record.cc, the semantic of each syscall mirrors what is done there).
// Unlike them, we are not synchronous with the job : we may process an access long after it occurred, when files may have been modified, unlinked or renamed.
// So the rule is that we do not look at the disk : what must be known about files comes from the kernel.
// The exceptions are marked with DISK, they are all corner cases (failed syscalls for which the kernel resolved nothing and the error does not tell whether file exists).

#include <sys/stat.h>
#include <sys/wait.h>

#if HAS_32
	#include "world_32.h"
#endif

#include "app.hh"
#include "process.hh"

#include "record.hh"
#include "syscall_tab.hh"
#include "gather.hh"

#include "ebpf_client.hh"
#include "ebpf_event.h"

#include "ebpf.hh"

#ifndef O_TMPFILE
	#define O_TMPFILE 0 // no check for O_TMPFILE if it does not exist
#endif

using namespace Disk       ;
using namespace Time       ;
using namespace EbpfDaemon ;

static FileInfo _mk_info( EbpfEventHdr const& hdr , ::string const& file={} ) {
	FileTag tag ;
	switch (hdr.file_kind) {
		case EBPF_FILE_USER    :                                                  return {file} ; // XXX : ensure file is a dep or we arrive too late (file may have changed since it was accessed)
		case EBPF_FILE_NO_FILE : tag = FileTag::None                            ; break         ;
		case EBPF_FILE_DIR     : tag = FileTag::Dir                             ; break         ;
		case EBPF_FILE_LNK     : tag = FileTag::Lnk                             ; break         ;
		case EBPF_FILE_REG     : tag = hdr.size ? FileTag::Reg : FileTag::Empty ; break         ;
		case EBPF_FILE_EXE     : tag = FileTag::Exe                             ; break         ;
	DF}
	return { hdr.size , {New,hdr.mtime_ns,tag} } ;
}

static Pdate _now(EbpfEventHdr const& hdr) {
	if (hdr.stamp_ns) return Pdate( New , hdr.stamp_ns ) ;
	else              return        New                  ;
} ;

void AutodepEbpf::Consumer::handle_event( EbpfEventHdr const& hdr , Path const& path , Path const& dst ) {
	SWEAR( hdr.kind>=EBPF_ACCESS , hdr.kind ) ;
	throw_unless(path.root_id==EBPF_ROOT_ID_REPO) ;                                                            // not yet implemented
	throw_unless(!dst                           ) ;                                                            // .
	//
	::string file     = path.path.substr(1/* / */)                                                    ;
	Bool3    write    = No | ( (hdr.kind==EBPF_WRITE||hdr.kind==EBPF_UNLINK) && hdr.flags&EBPF_F_OK ) ;
	Accesses accesses ;
	if (hdr.flags&EBPF_F_DEP_LNK ) accesses |= Access::Lnk  ;
	if (hdr.flags&EBPF_F_DEP_REG ) accesses |= Access::Reg  ;
	if (hdr.flags&EBPF_F_DEP_STAT) accesses |= Access::Stat ;
	AccessDigest ad { .write=write , .read_dir=hdr.kind==EBPF_READDIR , .accesses=accesses } ;
	gather->new_access( _now(hdr) , ::move(file) , ad , _mk_info(hdr) , Yes/*late*/ , Comment::None , CommentExt::Link ) ;
}

// runs in the child, just before execve
// /!\ this function must be malloc free as malloc takes a lock that may be held by another thread at the time process is cloned
int/*rc*/ AutodepEbpf::s_prepare_child(void*) {
	RingBufBase::s_start_job() ;
	return 0 ;
}

AutodepEbpf::AutodepEbpf(Gather& gather) {
	for( [[maybe_unused]] int _ : iota(3) ) {
		if (ring_buf.connect_to_daemon()) goto Connected ;
		Child ebpfd {
			.as_session = true
		,	.cmd_line   = { *g_lmake_root_s+"_bin/lmake_ebpfd" , "lmake" }
		,	.stdout     = Child::PipeFd
		} ;
		ebpfd.spawn() ;
		::string reply = ebpfd.stdout.read() ;
		switch (reply[0]) {
			case '0' : ebpfd.wait     () ; break                                             ; // daemon is already running, wait for this new one that will terminate shortly
			case '1' : ebpfd.mk_daemon() ; break                                             ; // daemon has started       , let it live its daemon life
			default  : ebpfd.wait     () ; throw cat("malformed ebpf daemon reply : ",reply) ;
		}
	}
	throw "cannot connect to nor launch daemon"s ;
Connected :
	ring_buf.init() ;
	ring_buf.consumer.gather = &gather ;
	//
	RingBufBase::s_add_root( EBPF_ROOT_ID_REPO , Record::s_autodep_env().repo_root_s ) ;
}
