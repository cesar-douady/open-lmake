// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "sys_config.h"

#include <elf.h>        // NT_PRSTATUS definition on ARM
#include <sys/ptrace.h>

#include "non_portable.hh"
#include "utils.hh"

using namespace std ;

int np_get_fd(::filebuf& fb) {
	struct FileBuf : ::filebuf { int fd() { return _M_file.fd() ; } } ; // a hack to access file descriptor
	return static_cast<FileBuf&>(fb).fd() ;
}

::array<uint64_t,6> np_ptrace_get_args(int pid) {
	struct ::user_regs_struct regs ;
	::array<uint64_t,6>       res  ;
	errno = 0 ;
	#ifdef __x86_64__
		ptrace( PTRACE_GETREGS , pid , nullptr/*addr*/ , &regs ) ;
		res[0] = regs.rdi ;                                                 // from man 2 syscall
		res[1] = regs.rsi ;
		res[2] = regs.rdx ;
		res[3] = regs.r10 ;
		res[4] = regs.r8  ;
		res[5] = regs.r9  ;
	#elif __aarch64__ || __arm__
		struct iovec iov ;
		iov.iov_base = &regs                      ;
		iov.iov_len  = sizeof(unsigned long long) ;                         // read only the 1st register
		long rc = ptrace(PTRACE_GETREGSET, pid, (void*)NT_PRSTATUS, &iov) ;
		SWEAR(rc!=-1) ;
		#if __arm__
			res[0] = regs.r0 ;                                              // from man 2 syscall
			res[1] = regs.r1 ;
			res[2] = regs.r2 ;
			res[3] = regs.r3 ;
			res[4] = regs.r4 ;
			res[5] = regs.r5 ;
		#elif __aarch64__
			res[0] = regs.regs[0] ;                                         // XXX : find a source of info
			res[1] = regs.regs[1] ;
			res[2] = regs.regs[2] ;
			res[3] = regs.regs[3] ;
			res[4] = regs.regs[4] ;
			res[5] = regs.regs[5] ;
		#endif
	#else
		#error "np_get_errno not implemented for this architecture"         // if situation arises, please provide the adequate code using x86_64 case as a template
	#endif
	SWEAR( !errno , errno ) ;
	return res ;
}

// Check : https://chromium.googlesource.com/chromiumos/docs/+/HEAD/constants/syscalls.md
int64_t np_ptrace_get_res(int pid) {
	struct ::user_regs_struct regs ;
	int64_t                   res  ;
	errno = 0 ;
	#ifdef __x86_64__
		ptrace( PTRACE_GETREGS , pid , nullptr/*addr*/ , &regs ) ;
		res = regs.rax ;
	#elif __aarch64__ || __arm__
		struct iovec iov ;
		iov.iov_base = &regs                      ;
		iov.iov_len  = sizeof(unsigned long long) ;                         // read only the 1st register
		long rc = ptrace(PTRACE_GETREGSET, pid, (void*)NT_PRSTATUS, &iov) ;
		SWEAR(rc!=-1) ;
		#if __arm__
			res = regs.r0      ;
		#elif __aarch64__
			res = regs.regs[0] ;
		#endif
	#else
		#error "np_get_errno not implemented for this architecture"         // if situation arises, please provide the adequate code using x86_64 case as a template
	#endif
	SWEAR( !errno , errno ) ;
	return res ;
}

long np_ptrace_get_nr(int pid) {
	errno = 0 ;
	struct ::user_regs_struct regs ;
	#ifdef __x86_64__
		ptrace( PTRACE_GETREGS , pid , nullptr/*addr*/ , &regs ) ;
		if (errno) throw 0 ;
		return regs.orig_rax ;
	#elif __aarch64__ || __arm__
		struct iovec  iov ;
		iov.iov_base = &regs ;
		iov.iov_len  = 9 * sizeof(unsigned long long) ;                      // read/write only 9 registers
		ptrace( PTRACE_GETREGSET , pid , (void*)NT_PRSTATUS , &iov ) ;
		if (errno) throw 0 ;
		#if __arm__
			return regs.r7 ;
		#elif __aarch64__
			return regs.regs[8] ;
		#endif
	#else
		#error "np_ptrace_get_syscall not implemented for this architecture" // if situation arises, please provide the adequate code using x86_64 case as a template
	#endif
}

void np_ptrace_set_res( int pid , long val ) {
	errno = 0 ;
	struct ::user_regs_struct regs ;
	#ifdef __x86_64__
		ptrace( PTRACE_GETREGS , pid , nullptr/*addr*/ , &regs ) ;
		if (errno) throw 0 ;
		regs.rax = val ;
		ptrace( PTRACE_SETREGS , pid , nullptr/*addr*/ , &regs ) ;
		if (errno) throw 0 ;
	#elif __aarch64__ || __arm__
		struct iovec  iov ;
		iov.iov_base = &regs ;
		iov.iov_len  = 9 * sizeof(unsigned long long) ;                      // read/write only 9 registers
		ptrace( PTRACE_GETREGSET , pid , (void*)NT_PRSTATUS , &iov ) ;
		if (errno) throw 0 ;
		#if __arm__
			regs.r0 = val ;                                                  // XXX : to be validated
		#elif __aarch64__
			regs.regs[0] = val ;                                             // XXX : to be validated
		#endif
		ptrace( PTRACE_SETREGSET , pid , (void*)NT_PRSTATUS , &iov ) ;
		if (errno) throw 0 ;
	#else
		#error "np_ptrace_get_syscall not implemented for this architecture" // if situation arises, please provide the adequate code using x86_64 case as a template
	#endif
}
