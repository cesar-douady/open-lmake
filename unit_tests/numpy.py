# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys

if __name__!='__main__' :

	import lmake

	try                        : import numpy                                  # check we can import numpy
	except ModuleNotFoundError : numpy = None                                  # but ignore test if module does not exist

	lmake.sources = ('Lmakefile.py',)
else :

	import ut

	ut.lmake(done=0,new=0)                                                     # just check we can load Lmakefile.py
