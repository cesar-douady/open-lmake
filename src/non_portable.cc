// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "sys_config.h"

#include "non_portable.hh"

using namespace std ;

int np_get_fd(::filebuf& fb) {
	struct FileBuf : ::filebuf { int fd() { return _M_file.fd() ; } } ; // a hack to access file descriptor
	return static_cast<FileBuf&>(fb).fd() ;
}

int np_get_errno(struct user_regs_struct const& regs) {
	#ifdef __x86_64__
		return -regs.rax ;
	#else
		#error "errno not implemented for this architecture"                   // if situation arises, please provide the adequate code using x86_64 case as a template
	#endif
}

void np_clear_syscall(struct user_regs_struct& regs) {
	#ifdef __x86_64__
		regs.orig_rax = -1 ;
	#else
		#error "clear_syscall not implemented for this architecture"           // if situation arises, please provide the adequate code using x86_64 case as a template
	#endif
}
