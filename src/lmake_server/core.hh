// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "py.hh" // /!\ must be first as Python.h must be first

#include "repo.hh"

#include "disk.hh"
#include "hash.hh"
#include "msg.hh"
#include "process.hh"
#include "thread.hh"
#include "time.hh"
#include "trace.hh"

namespace Engine {
	using FileSig     = Disk::FileSig     ;
	using Crc         = Hash::Crc         ;
	using Delay       = Time::Delay       ;
	using CoarseDelay = Time::CoarseDelay ;
	using Ddate       = Time::Ddate       ;
	using Pdate       = Time::Pdate       ;
	using SigDate     = Disk::SigDate     ;
}

#define STRUCT_DECL
#include "core.x.hh"
#undef STRUCT_DECL

#define STRUCT_DEF
#include "core.x.hh"
#undef STRUCT_DEF

#define INFO_DEF
#include "core.x.hh"
#undef INFO_DEF

#define DATA_DEF
#include "core.x.hh"
#undef DATA_DEF

#define IMPL
#include "core.x.hh"
#undef IMPL
