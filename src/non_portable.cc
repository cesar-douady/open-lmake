// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <elf.h>        // NT_PRSTATUS definition on ARM
#include <sys/ptrace.h>

#include "non_portable.hh"
#include "utils.hh"

using namespace std ;

int np_get_fd(::filebuf& fb) {
	struct FileBuf : ::filebuf { int fd() { return _M_file.fd() ; } } ; // a hack to access file descriptor
	return static_cast<FileBuf&>(fb).fd() ;
}

using UserRegsStruct = struct ::user_regs_struct ;
using Iovec          = struct ::iovec            ;
#ifdef __x86_64__
	using Word = decltype(UserRegsStruct().rdi    ) ;
#elif __arm__
	using Word = decltype(UserRegsStruct().r0     ) ;
#elif __aarch64__
	using Word = decltype(UserRegsStruct().regs[0]) ;
#else
	#error "reg accesses not implemented for this architecture" // if situation arises, please provide the adequate code using x86_64 case as a template
#endif

static void _get_set( pid_t pid , int n_words , UserRegsStruct& regs , bool set ) {
	#ifdef __x86_64__
		(void)n_words ;
		long rc = ptrace( set?PTRACE_SETREGS:PTRACE_GETREGS , pid , nullptr/*addr*/ , &regs ) ;
		SWEAR(rc==0,errno) ;
	#elif __aarch64__ || __arm__
		Iovec iov { .iov_base=&regs , .iov_len=n_words*sizeof(Word) }                                 ; // read/write n_words registers
		long  rc  = ptrace( set?PTRACE_SETREGSET:PTRACE_GETREGSET , pid , (void*)NT_PRSTATUS , &iov ) ;
		SWEAR(rc==0                            ,errno      ) ;
		SWEAR(iov.iov_len==n_words*sizeof(Word),iov.iov_len) ;                                          // check all asked regs have been read/written
	#endif
}

static UserRegsStruct _get( pid_t pid , int n_words                        ) { UserRegsStruct regs={/*zero*/} ; _get_set(pid,n_words,regs,false/*set*/) ; return regs ; }
static void           _set( pid_t pid , int n_words , UserRegsStruct& regs ) {                                  _get_set(pid,n_words,regs,true /*set*/) ;               }

::array<uint64_t,6> np_ptrace_get_args(pid_t pid) {                                            // info come from man 2 syscall
	UserRegsStruct      regs = _get(pid,6/*n_words*/) ;
	::array<uint64_t,6> res  ;
	#ifdef __x86_64__
		res[0] = regs.rdi ;
		res[1] = regs.rsi ;
		res[2] = regs.rdx ;
		res[3] = regs.r10 ;
		res[4] = regs.r8  ;
		res[5] = regs.r9  ;
	#elif __arm__
		res[0] = regs.r0      ; static_assert(offsetof(UserRegsStruct,r0  )<=5*sizeof(Word)) ; // else adjust n_words
		res[1] = regs.r1      ; static_assert(offsetof(UserRegsStruct,r1  )<=5*sizeof(Word)) ; // .
		res[2] = regs.r2      ; static_assert(offsetof(UserRegsStruct,r2  )<=5*sizeof(Word)) ; // .
		res[3] = regs.r3      ; static_assert(offsetof(UserRegsStruct,r3  )<=5*sizeof(Word)) ; // .
		res[4] = regs.r4      ; static_assert(offsetof(UserRegsStruct,r4  )<=5*sizeof(Word)) ; // .
		res[5] = regs.r5      ; static_assert(offsetof(UserRegsStruct,r5  )<=5*sizeof(Word)) ; // .
	#elif __aarch64__
		res[0] = regs.regs[0] ; static_assert(offsetof(UserRegsStruct,regs)==0             ) ; // .
		res[1] = regs.regs[1] ;
		res[2] = regs.regs[2] ;
		res[3] = regs.regs[3] ;
		res[4] = regs.regs[4] ;
		res[5] = regs.regs[5] ;
	#endif
	return res ;
}

// Check : https://chromium.googlesource.com/chromiumos/docs/+/HEAD/constants/syscalls.md
int64_t np_ptrace_get_res(pid_t pid) {
	UserRegsStruct regs = _get(pid,1/*n_words*/) ;
	#ifdef __x86_64__
		return regs.rax     ;
	#elif __arm__
		return regs.r0      ; static_assert(offsetof(UserRegsStruct,r0     )==0) ; // else adjust n_words
	#elif __aarch64__
		return regs.regs[0] ; static_assert(offsetof(UserRegsStruct,regs[0])==0) ; // .
	#endif
}

long np_ptrace_get_nr(pid_t pid) {
	UserRegsStruct regs = _get(pid,9/*n_words*/) ;
	#ifdef __x86_64__
		return regs.orig_rax ;
	#elif __arm__
		return regs.r7       ; static_assert(offsetof(UserRegsStruct,r7     )<=8*sizeof(Word)) ; // else adjust n_words
	#elif __aarch64__
		return regs.regs[8]  ; static_assert(offsetof(UserRegsStruct,regs[8])<=8*sizeof(Word)) ; // .
	#endif
}

void np_ptrace_set_res( pid_t pid , int64_t val ) {
	UserRegsStruct regs = _get(pid,1/*n_words*/)  ;
	#ifdef __x86_64__
		regs.rax     = val ;
	#elif __arm__
		regs.r0      = val ; static_assert(offsetof(UserRegsStruct,r0     )==0) ; // else adjust n_words
	#elif __aarch64__
		regs.regs[0] = val ; static_assert(offsetof(UserRegsStruct,regs[0])==0) ; // .
	#endif
	_set(pid,1,regs) ;
}
