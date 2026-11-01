// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <stdarg.h>

#include "sys/time.h"

void vlog( bool with_errno , bool with_nl , const char* fmt , va_list ap ) ;
//                                                                                   with_errno with_nl
inline void log      ( const char* fmt , ... ) { va_list ap ; va_start(ap,fmt) ; vlog( false   , true , fmt , ap ) ; va_end(ap) ; }
inline void log_errno( const char* fmt , ... ) { va_list ap ; va_start(ap,fmt) ; vlog( true    , true , fmt , ap ) ; va_end(ap) ; }

void reset_log() ;
