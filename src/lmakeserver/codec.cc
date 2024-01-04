// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh"

using namespace Time ;

namespace Engine {

	void codec_thread_func(CodecClosure&& cc) ;

	static QueueThread<CodecClosure> codec_thread{'D',codec_thread_func} ;

	static ::vmap_s<Ddate> file_dates ;

	void codec_thread_func(CodecClosure&& cc) {
		(void)cc ;
	}

}
