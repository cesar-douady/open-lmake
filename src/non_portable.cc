// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "sys_config.h"

#include <sys/ptrace.h>

#include "non_portable.hh"
#include "utils.hh"

using namespace std ;

int np_get_fd(::filebuf& fb) {
	struct FileBuf : ::filebuf { int fd() { return _M_file.fd() ; } } ; // a hack to access file descriptor
	return static_cast<FileBuf&>(fb).fd() ;
}

// Check : https://chromium.googlesource.com/chromiumos/docs/+/HEAD/constants/syscalls.md
int np_ptrace_get_errno(int pid) {
	struct ::user_regs_struct regs ;
	int                       res  ;
	errno = 0 ;
	#ifdef __x86_64__
		ptrace( PTRACE_GETREGS , pid , nullptr/*addr*/ , &regs ) ;
		res = -regs.rax ;
	#elif __aarch64__ || __arm__
		struct iovec iov ;
		iov.iov_base = &regs                      ;
		iov.iov_len  = sizeof(unsigned long long) ;                            // read only the 1st register
		long rc = ptrace(PTRACE_GETREGSET, pid, (void*)NT_PRSTATUS, &iov) ;
		SWEAR(rc!=-1) ;
		#if __arm__
			res = -regs.r0      ;
		#elif __aarch64__
			res = -regs.regs[0] ;
		#endif
	#else
		#error "np_get_errno not implemented for this architecture"            // if situation arises, please provide the adequate code using x86_64 case as a template
	#endif
	SWEAR( !errno , errno ) ;
	return res ;
}

void np_ptrace_clear_syscall(int pid) {
	errno = 0 ;
	struct ::user_regs_struct regs ;
	#ifdef __x86_64__
		ptrace( PTRACE_GETREGS , pid , nullptr/*addr*/ , &regs ) ;
		if (errno) throw 0 ;
		regs.orig_rax = -1 ;
		ptrace( PTRACE_SETREGS , pid , nullptr/*addr*/ , &regs ) ;
		if (errno) throw 0 ;
	#elif __aarch64__ || __arm__
		long          rc  ;
		struct iovec  iov ;
		iov.iov_base = &regs ;
		iov.iov_len  = 9 * sizeof(unsigned long long) ;                        // read/write only 9 registers
		rc  = ptrace( PTRACE_GETREGSET , pid , (void*)NT_PRSTATUS , &iov ) ;
		if ( rc==-1 || errno ) throw 0 ;
		#if __arm__
			regs.r7       = -1 ;
		#elif __aarch64__
			regs.regs[8]  = -1 ;
		#endif
		rc = ptrace( PTRACE_SETREGSET , pid , (void*)NT_PRSTATUS , &iov ) ;
		if ( rc==-1 || errno ) throw 0 ;
	#else
		#error "np_clear_syscall not implemented for this architecture"        // if situation arises, please provide the adequate code using x86_64 case as a template
	#endif
}
