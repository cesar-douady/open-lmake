// This file is part of the lmake distribution (https://wgit.doliam.net/cdy/lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"

#include "pycxx.hh"

namespace Py {

	void init() {
		static bool once=false ; if (once) return ; else once = true ;
		Py_Initialize() ;
	}
	static bool _inited = at_init(1,init) ;

	::ostream& operator<<( ::ostream& os , Pattern const& pat ) {
		return os << "Pattern(" << pat.pattern() << ")" ;
	}

}
