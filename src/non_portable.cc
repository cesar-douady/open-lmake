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
static inline int _get_errno(struct user_regs_struct const& regs) {
	#ifdef __x86_64__
		return -regs.rax    ;
	#elif __arm__
		return -regs.r0     ;
	#elif __aarch64__
		return -regs.regs[0];
	#else
		#error "errno not implemented for this architecture"                   // if situation arises, please provide the adequate code using x86_64 case as a template
	#endif
}

static inline void _clear_syscall(struct user_regs_struct& regs) {
	// Clear the system call number
	#ifdef __x86_64__
		regs.orig_rax = -1 ;
	#elif __arm__
		regs.r7       = -1 ;
	#elif __aarch64__
		regs.regs[8]  = -1 ;
	#else
		#error "clear_syscall not implemented for this architecture"           // if situation arises, please provide the adequate code using x86_64 case as a template
	#endif
}

int np_get_errno(int pid) {
	errno = 0;
	struct ::user_regs_struct regs ;
	#ifdef __x86_64__
		ptrace( PTRACE_GETREGS , pid , nullptr/*addr*/ , &regs ) ;
	#elif __aarch64__ || __arm__
		struct iovec  iov;
		iov.iov_base = &regs;
		iov.iov_len  = sizeof(unsigned long long); //read only the 1st register
		long ret = ptrace(PTRACE_GETREGSET, pid, (void*)NT_PRSTATUS, &iov);
		SWEAR(ret != -1);
	#endif
	SWEAR(!errno) ;
	return _get_errno(regs) ;
}

void np_clear_syscall(int pid) {
	errno = 0 ;
	struct ::user_regs_struct regs ;
	#ifdef __x86_64__
		ptrace( PTRACE_GETREGS , pid , nullptr/*addr*/ , &regs ) ;
		if (errno) throw 0 ;
		_clear_syscall(regs) ;
		ptrace( PTRACE_SETREGS , pid , nullptr/*addr*/ , &regs ) ;
		if (errno) throw 0 ;
	#elif __aarch64__ || __arm__
		long          ret;
		struct iovec  iov;
		iov.iov_base = &regs;
		iov.iov_len  = 9 * sizeof(unsigned long long); //read/write only 9 registers
		ret = ptrace(PTRACE_GETREGSET, pid, (void*)NT_PRSTATUS, &iov);
		if (ret==-1 || errno) throw 0 ;
		_clear_syscall(regs) ;
		ret = ptrace(PTRACE_SETREGSET, pid, (void*)NT_PRSTATUS, &iov);
		if (ret==-1 || errno) throw 0 ;
	#endif
}
