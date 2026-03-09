// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <elf.h>         // NT_PRSTATUS definition on ARM
#include <linux/audit.h> // AUDIT_ARCH_*


#include "utils.hh"

namespace NonPortable {

	static constexpr char ErrnoSymbolName[] = "__errno_location" ; // XXX! : find a way to stick to documented interfaces

	#if __x86_64__
		constexpr uint32_t Arch = AUDIT_ARCH_X86_64  ;
	#elif __i386__
		constexpr uint32_t Arch = AUDIT_ARCH_I386    ;
	#elif __aarch64__
		constexpr uint32_t Arch = AUDIT_ARCH_AARCH64 ;
	#elif __arm__
		constexpr uint32_t Arch = AUDIT_ARCH_ARM     ;
	#elif __s390x__
		constexpr uint32_t Arch = AUDIT_ARCH_S390X   ;
	#elif __s390__
		constexpr uint32_t Arch = AUDIT_ARCH_S390    ;
	#endif


	bool is_32_from_audit_arch(uint32_t arch) ;

	static constexpr size_t ElfHdrSz = ::max<size_t>({ EI_CLASS+1 , EI_DATA+1 , offsetof(Elf32_Ehdr,e_machine)+2 , offsetof(Elf64_Ehdr,e_machine)+2 }) ;
	Bool3 is_32_from_elf(const char* elf_hdr) ; // Maybe means elf_hdr is not recognized, else must point to the ElfHdrSz first bytes of a Elf32_Ehdr or a Elf64_Ehdr

	bool                ptrace_is_32   ( pid_t pid                                  ) ;
	::array<uint64_t,6> ptrace_get_args( pid_t pid ,               bool is_32=false ) ;
	int64_t             ptrace_get_res ( pid_t pid ,               bool is_32=false ) ;
	long                ptrace_get_nr  ( pid_t pid ,               bool is_32=false ) ;
	void                ptrace_set_res ( pid_t pid , int64_t val , bool is_32=false ) ;

}
