// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <sys/ptrace.h>
#include <sys/user.h>

#include "world_32.h"

#include "utils.hh"
#include "disk.hh"

#include "non_portable.hh"

namespace NonPortable {

	#if HAS_32
		template<bool Is32=false> using UserRegsStruct = ::conditional_t<Is32,struct World32::user_regs_struct,struct ::user_regs_struct> ;
	#else
		template<bool Is32=false> using UserRegsStruct = ::conditional_t<Is32,void                            ,struct ::user_regs_struct> ;
	#endif

	using Iovec = struct ::iovec ;

	bool is_32_from_audit_arch(uint32_t arch) {
		if (arch==Arch) return false ;
		#if __x86_64__
			if (arch==AUDIT_ARCH_I386) return true ;
		#elif __aarch64__
			if (arch==AUDIT_ARCH_ARM ) return true ;
		#elif __s390x__
			if (arch==AUDIT_ARCH_S390) return true ;
		#endif
		throw cat("unrecognized architecture") ; // NO_COV
	}

	Bool3 is_32_from_elf(const char* elf_hdr) {
		if (::strncmp(elf_hdr,"\177ELF",4)!=0) return  Maybe ; // not an elf
		switch (elf_hdr[EI_CLASS]) {
			case ELFCLASS64 : return Maybe&IS_32 ;             // 64-bit elf are not recognized in 64-bit hosts
			case ELFCLASS32 : break              ;             // need to further analyze to manage -mx32 mode which appears as 32-bit elf with 64-bits architecture
			default         : return Maybe       ;             // not an elf (or at least not reconizable)
		}
		if (reinterpret_cast<Elf32_Ehdr const*>(elf_hdr)->e_machine==EM_X86_64) return Maybe&IS_32 ; // -mx32, only recognized in 64-bit hosts
		else                                                                    return No   &IS_64 ; // real 32-bit, only reported as 32-bit in 64-bit hosts
	}

	template<bool Set,bool Is32=false> static void _get_set( pid_t pid , UserRegsStruct<Is32>&/*inout*/ regs ) {
		errno = 0 ;
		#if __aarch64__ || __arm__
			Iovec iov { .iov_base=&regs , .iov_len=sizeof(regs) }                                           ; // read/write n_words registers
			long  rc  = ::ptrace( Set?PTRACE_SETREGSET:PTRACE_GETREGSET , pid , (void*)NT_PRSTATUS , &iov ) ;
			SWEAR_PROD( iov.iov_len==sizeof(regs) , iov.iov_len ) ;                                           // check all asked regs have been handled
		#else
			long rc = ::ptrace( Set?PTRACE_SETREGS:PTRACE_GETREGS , pid , nullptr/*addr*/ , &regs ) ;
		#endif
		throw_unless( rc==0 , "cannot ",Set?"set":"get"," (",StrErr(),") regs" ) ;
	} //!                                                                                                                                              Set
	template<bool Is32=false> static UserRegsStruct<Is32> _get(pid_t pid                           ) { UserRegsStruct<Is32> regs={/*zero*/} ; _get_set<false,Is32>(pid,       regs) ; return regs ; }
	template<bool Is32=false> static void                 _set(pid_t pid,UserRegsStruct<Is32>& regs) {                                        _get_set<true ,Is32>(pid,/*out*/regs) ;               }

	// info from : https://www.chromium.org/chromium-os/developer-library/reference/linux-constants/syscalls

	bool ptrace_is_32(pid_t pid) {
		constexpr size_t Sz = sizeof(UserRegsStruct<false/*Is32*/>) ;
		#if HAS_32
			constexpr size_t Sz32  = sizeof(UserRegsStruct<true/*Is32*/>) ;
			constexpr size_t BufSz = ::max(Sz,Sz32)+sizeof(size_t)        ;                                 // an impossible buf size larger than what can possibly be needed
		#else
			constexpr size_t BufSz = Sz+sizeof(size_t)                    ;                                 // .
		#endif
		//
		alignas(size_t) char buf[BufSz] ;                                                                  // no need for init as buf is not read
		Iovec                iov        { .iov_base=buf , .iov_len=BufSz }                               ;
		long                 rc         = ::ptrace( PTRACE_GETREGSET , pid , (void*)NT_PRSTATUS , &iov ) ;
		throw_unless( rc==0 , "cannot get"," (",StrErr(),") regs from pid ",pid ) ;
		//
		#if HAS_32                                                                                          // cannot check size if no 32-bit support
			SWEAR( iov.iov_len==Sz || iov.iov_len==Sz32 , iov.iov_len ) ;
		#endif
		return iov.iov_len!=Sz ;
	}

	::array<uint64_t,6> ptrace_get_args( pid_t pid , bool is_32 ) { // info come from man 2 syscall
		::array<uint64_t,6> res ;
		#if HAS_32
			if (is_32) {
				UserRegsStruct<true/*Is32*/> regs = _get<true/*Is32*/>(pid) ;
				#if __x86_64__
					res[0] = regs.ebx     ;                         // full struct is retrieved with x86
					res[1] = regs.ecx     ;
					res[2] = regs.edx     ;
					res[3] = regs.esi     ;
					res[4] = regs.edi     ;
					res[5] = regs.ebp     ;
				#elif __aarch64__
					res[0] = regs.r0      ;
					res[1] = regs.r1      ;
					res[2] = regs.r2      ;
					res[3] = regs.r3      ;
					res[4] = regs.r4      ;
					res[5] = regs.r5      ;
				#elif __s390x__
					res[0] = regs.gprs[2] ;                         // full struct is retrieved with s390
					res[1] = regs.gprs[3] ;
					res[2] = regs.gprs[4] ;
					res[3] = regs.gprs[5] ;
					res[4] = regs.gprs[6] ;
					res[5] = regs.gprs[7] ;
				#else
					#error "cannot has HAS_32 within 32-bit hosts"
				#endif
				return res ;
			}
		#endif
		SWEAR_PROD(!is_32) ;
		UserRegsStruct<> regs = _get(pid) ;
		#if __x86_64__
			res[0] = regs.rdi     ;                                 // full struct is retrieved with x86
			res[1] = regs.rsi     ;
			res[2] = regs.rdx     ;
			res[3] = regs.r10     ;
			res[4] = regs.r8      ;
			res[5] = regs.r9      ;
		#elif __i386__
			res[0] = regs.ebx     ;                                 // full struct is retrieved with x86
			res[1] = regs.ecx     ;
			res[2] = regs.edx     ;
			res[3] = regs.esi     ;
			res[4] = regs.edi     ;
			res[5] = regs.ebp     ;
		#elif __aarch64__
			res[0] = regs.regs[0] ;
			res[1] = regs.regs[1] ;
			res[2] = regs.regs[2] ;
			res[3] = regs.regs[3] ;
			res[4] = regs.regs[4] ;
			res[5] = regs.regs[5] ;
		#elif __arm__
			res[0] = regs.r0      ;
			res[1] = regs.r1      ;
			res[2] = regs.r2      ;
			res[3] = regs.r3      ;
			res[4] = regs.r4      ;
			res[5] = regs.r5      ;
		#elif __s390x__ || __s390__
			res[0] = regs.gprs[2] ;                                 // full struct is retrieved with s390
			res[1] = regs.gprs[3] ;
			res[2] = regs.gprs[4] ;
			res[3] = regs.gprs[5] ;
			res[4] = regs.gprs[6] ;
			res[5] = regs.gprs[7] ;
		#else
			#error "unrecognized architecture"
		#endif
		return res ;
	}

	int64_t ptrace_get_res( pid_t pid , bool is_32 ) {
		#if HAS_32
			if (is_32) {
				UserRegsStruct<true/*Is32*/> regs = _get<true/*Is32*/>(pid) ;
				#if __x86_64__
					return regs.eax ;
				#elif __aarch64__
					return regs.r0  ;
				#elif __s390x__
					return regs.gprs[2] ;
				#else
					#error "cannot has HAS_32 within 32-bit hosts"
				#endif
			}
		#endif
		SWEAR_PROD(!is_32) ;
		UserRegsStruct<> regs = _get(pid) ;
		#if __x86_64__
			return regs.rax     ;
		#elif __i386__
			return regs.eax     ;
		#elif __aarch64__
			return regs.regs[0] ;
		#elif __arm__
			return regs.r0      ;
		#elif __s390x__ || __s390__
			return regs.gprs[2] ;
		#else
			#error "unrecognized architecture"
		#endif
	}

	long ptrace_get_nr( pid_t pid , bool is_32 ) {
		#if HAS_32
			if (is_32) {
				UserRegsStruct<true/*Is32*/> regs = _get<true/*Is32*/>(pid) ;
				#if __x86_64__
					return regs.orig_eax ;
				#elif __aarch64__
					return regs.r7       ;
				#elif __s390x__
					return regs.gprs[1]  ;
				#else
					#error "cannot has HAS_32 within 32-bit hosts"
				#endif
			}
		#endif
		SWEAR_PROD(!is_32) ;
		UserRegsStruct<> regs = _get(pid) ;
		#if __x86_64__
			return regs.orig_rax ;
		#elif __i386__
			return regs.orig_eax ;
		#elif __aarch64__
			return regs.regs[8]  ;
		#elif __arm__
			return regs.r7       ;
		#elif __s390x__ || __s390__
			return regs.gprs[1]  ;
		#else
			#error "unrecognized architecture"
		#endif
	}

	void ptrace_set_res( pid_t pid , int64_t val , bool is_32 ) {
		#if HAS_32
			if (is_32) {
				UserRegsStruct<true/*Is32*/> regs = _get<true/*Is32*/>(pid) ;
				#if __x86_64__
					regs.eax     = int32_t(val) ;
				#elif __aarch64__
					regs.r0      = int32_t(val) ;
				#elif __s390x__
					regs.gprs[2] = int32_t(val) ;
				#else
					#error "cannot has HAS_32 within 32-bit hosts"
				#endif
				_set<true/*Is32*/>(pid,regs) ;
				return ;
			}
		#endif
		SWEAR_PROD(!is_32) ;
		UserRegsStruct<> regs = _get(pid) ;
		#if __x86_64__
			regs.rax     = val ;
		#elif __i386__
			regs.eax     = val ;
		#elif __aarch64__
			regs.regs[0] = val ;
		#elif __arm__
			regs.r0      = val ;
		#elif __s390x__ || __s390__
			regs.gprs[2] = val ;
		#else
			#error "unrecognized architecture"
		#endif
		_set(pid,regs) ;
	}

}
