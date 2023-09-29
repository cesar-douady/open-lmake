// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "time.hh"

#pragma once

namespace Engine {

	struct Makefiles {
		// statics
		static void                          s_refresh_makefiles(bool chk) ;
		static ::string/*reason to re-read*/ s_chk_makefiles    (        ) { Time::Ddate _ ; return _s_chk_makefiles(_) ; }
	private :
		static ::string/*reason to re-read*/ _s_chk_makefiles(Time::Ddate& latest_makefile/*output*/) ;
		// static data
	public :
		static ::string s_makefiles    ;                   // file that contains makefiles read while reading Lmakefiles.py
		static ::string s_no_makefiles ;                   // marker that says that previous file is invalid (so that it can be written early to improve date compararisons)
		static ::string s_config_file  ;
	} ;

}
