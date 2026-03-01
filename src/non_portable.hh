// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <elf.h>        // NT_PRSTATUS definition on ARM

#include "std.hh"

static constexpr char NpErrnoSymbolName[] = "__errno_location" ; // XXX! : find a way to stick to documented interfaces

#if __x86_64__ || __aarch64__ || __s390x__
	static constexpr uint8_t NpWordSz = 64 ;
#elif __i386__ || __arm__     || __s390__
	static constexpr uint8_t NpWordSz = 32 ;
#endif

uint8_t np_word_sz_from_audit_arch(uint32_t arch) ;
static constexpr size_t NpElfHdrSz = ::max<size_t>({ EI_CLASS+1 , EI_DATA+1 , offsetof(Elf32_Ehdr,e_machine)+2 , offsetof(Elf64_Ehdr,e_machine)+2 }) ;
uint8_t np_word_sz_from_elf(const char* elf_hdr) ; // must point to the ElfHdrSz first bytes of a Elf32_Ehdr or a Elf64_Ehdr

::array<uint64_t,6> np_ptrace_get_args( pid_t pid ,               uint8_t word_sz=NpWordSz ) ; // word_sz must be 32 or 64
int64_t             np_ptrace_get_res ( pid_t pid ,               uint8_t word_sz=NpWordSz ) ; // .
long                np_ptrace_get_nr  ( pid_t pid ,               uint8_t word_sz=NpWordSz ) ; // .
void                np_ptrace_set_res ( pid_t pid , int64_t val , uint8_t word_sz=NpWordSz ) ; // .
