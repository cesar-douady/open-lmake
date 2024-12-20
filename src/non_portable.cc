// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <elf.h>        // NT_PRSTATUS definition on ARM
#include <sys/ptrace.h>

#include "utils.hh"

#include "non_portable.hh"

#if __x86_64__ + __i386__ + __aarch64__ + __arm__ != 1
	#error "unknown architecture"                      // if situation arises, please provide the adequate code using other cases as a template
#endif

using namespace std ;

	using UserRegsStruct = struct ::user_regs_struct ;
	using Iovec          = struct ::iovec            ;
	#if __x86_64__
		using Word = decltype(UserRegsStruct().rdi    ) ;
	#elif __i386__
		using Word = decltype(UserRegsStruct().ebx    ) ;
	#elif __aarch64__
		using Word = decltype(UserRegsStruct().regs[0]) ;
	#elif __arm__
		using Word = decltype(UserRegsStruct().r0     ) ;
	#endif

	static void _get_set( pid_t pid , int n_words , UserRegsStruct& regs , bool set ) {
		#if __x86_64__ || __i386__
			(void)n_words ;
			if ( ::ptrace( set?PTRACE_SETREGS:PTRACE_GETREGS , pid , nullptr/*addr*/ , &regs )<0 ) throw "cannot "s+(set?"set":"get")+" regs" ;
		#elif __aarch64__ || __arm__
			Iovec iov { .iov_base=&regs , .iov_len=n_words*sizeof(Word) } ;                                                                                       // read/write n_words registers
			if ( ::ptrace( set?PTRACE_SETREGSET:PTRACE_GETREGSET , pid , (void*)NT_PRSTATUS , &iov )<0 ) throw "cannot "s+(set?"set":"get")+' '+n_words+" regs" ;
			SWEAR(iov.iov_len==n_words*sizeof(Word),iov.iov_len) ; // check all asked regs have been read/written
		#endif
	}
	//!                                                                                                                                       set
	static UserRegsStruct _get( pid_t pid , int n_words                        ) { UserRegsStruct regs={/*zero*/} ; _get_set(pid,n_words,regs,false) ; return regs ; }
	static void           _set( pid_t pid , int n_words , UserRegsStruct& regs ) {                                  _get_set(pid,n_words,regs,true ) ;               }

	// info from : https://www.chromium.org/chromium-os/developer-library/reference/linux-constants/syscalls

::array<uint64_t,6> np_ptrace_get_args( pid_t pid , uint8_t word_sz ) {                 // info come from man 2 syscall
	SWEAR(word_sz==NpWordSz,word_sz) ;                                                  // XXX : implement 32 bits tracee from 64 bits tracer
	UserRegsStruct      regs = _get(pid,6/*n_words*/) ;
	::array<uint64_t,6> res  ;
	#if __x86_64__
		res[0] = regs.rdi ;                                                             // full struct is retrieved with x86
		res[1] = regs.rsi ;
		res[2] = regs.rdx ;
		res[3] = regs.r10 ;
		res[4] = regs.r8  ;
		res[5] = regs.r9  ;
	#elif __i386__
		res[0] = regs.ebx ;                                                             // full struct is retrieved with x86
		res[1] = regs.ecx ;
		res[2] = regs.edx ;
		res[3] = regs.esi ;
		res[4] = regs.edi ;
		res[5] = regs.ebp ;
	#elif __aarch64__
		res[0] = regs.regs[0] ; static_assert(offsetof(UserRegsStruct,regs)==0) ;       // else adjust n_words
		res[1] = regs.regs[1] ;
		res[2] = regs.regs[2] ;
		res[3] = regs.regs[3] ;
		res[4] = regs.regs[4] ;
		res[5] = regs.regs[5] ;
	#elif __arm__
		res[0] = regs.r0 ; static_assert(offsetof(UserRegsStruct,r0)<=5*sizeof(Word)) ; // else adjust n_words
		res[1] = regs.r1 ; static_assert(offsetof(UserRegsStruct,r1)<=5*sizeof(Word)) ; // .
		res[2] = regs.r2 ; static_assert(offsetof(UserRegsStruct,r2)<=5*sizeof(Word)) ; // .
		res[3] = regs.r3 ; static_assert(offsetof(UserRegsStruct,r3)<=5*sizeof(Word)) ; // .
		res[4] = regs.r4 ; static_assert(offsetof(UserRegsStruct,r4)<=5*sizeof(Word)) ; // .
		res[5] = regs.r5 ; static_assert(offsetof(UserRegsStruct,r5)<=5*sizeof(Word)) ; // .
	#endif
	return res ;
}

int64_t np_ptrace_get_res( pid_t pid , uint8_t word_sz ) {
	SWEAR(word_sz==NpWordSz,word_sz) ;                                             // XXX : implement 32 bits tracee from 64 bits tracer
	UserRegsStruct regs = _get(pid,1/*n_words*/) ;
	#if __x86_64__
		return regs.rax     ;                                                      // full struct is retrieved with x86
	#elif __i386__
		return regs.eax     ;                                                      // .
	#elif __aarch64__
		return regs.regs[0] ; static_assert(offsetof(UserRegsStruct,regs[0])==0) ; // else adjust n_words
	#elif __arm__
		return regs.r0      ; static_assert(offsetof(UserRegsStruct,r0     )==0) ; // .
	#endif
}

long np_ptrace_get_nr( pid_t pid , uint8_t word_sz ) {
	SWEAR(word_sz==NpWordSz,word_sz) ;                                                           // XXX : implement 32 bits tracee from 64 bits tracer
	UserRegsStruct regs = _get(pid,9/*n_words*/) ;
	#if __x86_64__
		return regs.orig_rax ;                                                                   // full struct is retrieved with x86
	#elif __i386__
		return regs.orig_eax ;                                                                   // .
	#elif __aarch64__
		return regs.regs[8]  ; static_assert(offsetof(UserRegsStruct,regs[8])<=8*sizeof(Word)) ; // else adjust n_words
	#elif __arm__
		return regs.r7       ; static_assert(offsetof(UserRegsStruct,r7     )<=8*sizeof(Word)) ; // .
	#endif
}

void np_ptrace_set_res( pid_t pid , int64_t val , uint8_t word_sz ) {
	SWEAR(word_sz==NpWordSz,word_sz) ;                                            // XXX : implement 32 bits tracee from 64 bits tracer
	UserRegsStruct regs = _get(pid,1/*n_words*/)  ;
	#if __x86_64__
		regs.rax     = val ;                                                      // full struct is retrieved with x86
	#elif __i386__
		regs.eax     = val ;                                                      // .
	#elif __aarch64__
		regs.regs[0] = val ; static_assert(offsetof(UserRegsStruct,regs[0])==0) ; // else adjust n_words
	#elif __arm__
		regs.r0      = val ; static_assert(offsetof(UserRegsStruct,r0     )==0) ; // .
	#endif
	_set(pid,1,regs) ;
}
