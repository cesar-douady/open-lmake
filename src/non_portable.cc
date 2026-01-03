// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <elf.h>        // NT_PRSTATUS definition on ARM
#include <sys/ptrace.h>
#include <sys/user.h>

#include "utils.hh"

#include "non_portable.hh"

#if !(__x86_64__||__i386__||__aarch64__||__arm__||__s390__||__s390x__)
	#error "unknown architecture"                                      // if situation arises, please provide the adequate code using other cases as a template
#endif

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
#elif __s390x__ || __s390__
	using Word = decltype(UserRegsStruct().gprs[0]) ;
#endif

template<bool Set> static void _get_set( pid_t pid , [[maybe_unused]] int n_words , UserRegsStruct&/*inout*/ regs ) {
	#if __aarch64__ || __arm__
		Iovec iov { .iov_base=&regs , .iov_len=n_words*sizeof(Word) } ;                                                                                          // read/write n_words registers
		throw_unless( ::ptrace( Set?PTRACE_SETREGSET:PTRACE_GETREGSET , pid , (void*)NT_PRSTATUS , &iov )==0 , "cannot ",Set?"set":"get",' ',n_words," regs" ) ;
		SWEAR( iov.iov_len==n_words*sizeof(Word) , iov.iov_len ) ; // check all asked regs have been handled
	#else
		throw_unless( ::ptrace( Set?PTRACE_SETREGS:PTRACE_GETREGS , pid , nullptr/*addr*/ , &regs )==0 , "cannot ",Set?"set":"get"," regs") ;
	#endif
}
static UserRegsStruct _get( pid_t pid , int n_words                        ) { UserRegsStruct regs={/*zero*/} ; _get_set<false/*Set*/>(pid,n_words,       regs) ; return regs ; }
static void           _set( pid_t pid , int n_words , UserRegsStruct& regs ) {                                  _get_set<true /*Set*/>(pid,n_words,/*out*/regs) ;               }

// info from : https://www.chromium.org/chromium-os/developer-library/reference/linux-constants/syscalls

::array<uint64_t,6> np_ptrace_get_args( pid_t pid , uint8_t word_sz ) {                               // info come from man 2 syscall
	static constexpr int NWords = 6 ;
	SWEAR( word_sz==NpWordSz , word_sz ) ;                                                            // XXX! : implement 32 bits tracee from 64 bits tracer
	UserRegsStruct      regs = _get(pid,NWords) ;
	::array<uint64_t,6> res  ;
	#if __x86_64__
		res[0] = regs.rdi     ;                                                                       // full struct is retrieved with x86
		res[1] = regs.rsi     ;
		res[2] = regs.rdx     ;
		res[3] = regs.r10     ;
		res[4] = regs.r8      ;
		res[5] = regs.r9      ;
	#elif __i386__
		res[0] = regs.ebx     ;                                                                       // full struct is retrieved with x86
		res[1] = regs.ecx     ;
		res[2] = regs.edx     ;
		res[3] = regs.esi     ;
		res[4] = regs.edi     ;
		res[5] = regs.ebp     ;
	#elif __aarch64__
		res[0] = regs.regs[0] ; static_assert(offsetof(UserRegsStruct,regs[0])<NWords*sizeof(Word)) ; // else adjust NWords
		res[1] = regs.regs[1] ; static_assert(offsetof(UserRegsStruct,regs[1])<NWords*sizeof(Word)) ; // .
		res[2] = regs.regs[2] ; static_assert(offsetof(UserRegsStruct,regs[2])<NWords*sizeof(Word)) ; // .
		res[3] = regs.regs[3] ; static_assert(offsetof(UserRegsStruct,regs[3])<NWords*sizeof(Word)) ; // .
		res[4] = regs.regs[4] ; static_assert(offsetof(UserRegsStruct,regs[4])<NWords*sizeof(Word)) ; // .
		res[5] = regs.regs[5] ; static_assert(offsetof(UserRegsStruct,regs[5])<NWords*sizeof(Word)) ; // .
	#elif __arm__
		res[0] = regs.r0      ; static_assert(offsetof(UserRegsStruct,r0     )<NWords*sizeof(Word)) ; // .
		res[1] = regs.r1      ; static_assert(offsetof(UserRegsStruct,r1     )<NWords*sizeof(Word)) ; // .
		res[2] = regs.r2      ; static_assert(offsetof(UserRegsStruct,r2     )<NWords*sizeof(Word)) ; // .
		res[3] = regs.r3      ; static_assert(offsetof(UserRegsStruct,r3     )<NWords*sizeof(Word)) ; // .
		res[4] = regs.r4      ; static_assert(offsetof(UserRegsStruct,r4     )<NWords*sizeof(Word)) ; // .
		res[5] = regs.r5      ; static_assert(offsetof(UserRegsStruct,r5     )<NWords*sizeof(Word)) ; // .
	#elif __s390x__ || __s390__
		res[0] = regs.gprs[2] ;                                                                       // full struct is retrieved with s390
		res[1] = regs.gprs[3] ;
		res[2] = regs.gprs[4] ;
		res[3] = regs.gprs[5] ;
		res[4] = regs.gprs[6] ;
		res[5] = regs.gprs[7] ;
	#endif
	return res ;
}

int64_t np_ptrace_get_res( pid_t pid , uint8_t word_sz ) {
	static constexpr int NWords = 1 ;
	SWEAR( word_sz==NpWordSz , word_sz ) ;                                                          // XXX! : implement 32 bits tracee from 64 bits tracer
	UserRegsStruct regs = _get(pid,NWords) ;
	#if __x86_64__
		return regs.rax     ;                                                                       // full struct is retrieved with x86
	#elif __i386__
		return regs.eax     ;                                                                       // .
	#elif __aarch64__
		return regs.regs[0] ; static_assert(offsetof(UserRegsStruct,regs[0])<NWords*sizeof(Word)) ; // else adjust NWords
	#elif __arm__
		return regs.r0      ; static_assert(offsetof(UserRegsStruct,r0     )<NWords*sizeof(Word)) ; // .
	#elif __s390x__ || __s390__
		return regs.gprs[2] ;                                                                       // full struct is retrieved with s390
	#endif
}

long np_ptrace_get_nr( pid_t pid , uint8_t word_sz ) {
	static constexpr int NWords = 9 ;
	SWEAR( word_sz==NpWordSz , word_sz ) ;                                                           // XXX! : implement 32 bits tracee from 64 bits tracer
	UserRegsStruct regs = _get(pid,NWords) ;
	#if __x86_64__
		return regs.orig_rax ;                                                                       // full struct is retrieved with x86
	#elif __i386__
		return regs.orig_eax ;                                                                       // .
	#elif __aarch64__
		return regs.regs[8]  ; static_assert(offsetof(UserRegsStruct,regs[8])<NWords*sizeof(Word)) ; // else adjust NWords
	#elif __arm__
		return regs.r7       ; static_assert(offsetof(UserRegsStruct,r7     )<NWords*sizeof(Word)) ; // .
	#elif __s390x__ || __s390__
		return regs.gprs[1]  ;                                                                       // full struct is retrieved with s390
	#endif
}

void np_ptrace_set_res( pid_t pid , int64_t val , uint8_t word_sz ) {
	static constexpr int NWords = 1 ;
	SWEAR( word_sz==NpWordSz , word_sz ) ;                                                         // XXX! : implement 32 bits tracee from 64 bits tracer
	UserRegsStruct regs = _get(pid,NWords) ;                                                       // if a single word is needed, no need to prefetch it as it is written to
	#if __x86_64__
		regs.rax     = val ;                                                                       // full struct is retrieved with x86
	#elif __i386__
		regs.eax     = val ;                                                                       // .
	#elif __aarch64__
		regs.regs[0] = val ; static_assert(offsetof(UserRegsStruct,regs[0])<NWords*sizeof(Word)) ; // else adjust NWords
	#elif __arm__
		regs.r0      = val ; static_assert(offsetof(UserRegsStruct,r0     )<NWords*sizeof(Word)) ; // .
	#elif __s390x__ || __s390__
		regs.gprs[2] = val ;                                                                       // full struct is retrieved with s390
	#endif
	_set(pid,NWords,regs) ;
}
