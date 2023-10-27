# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys

if getattr(sys,'lmake_read_makefiles',False) :

	import lmake

	lmake.sources = ('Lmakefile.py',)

	class Base(lmake.Rule) :
		environ_cmd       = {'PATH'               :'foo1'}
		environ_resources = {'R'                  :'foo2'}
		path              = {'environ_resources.R':'/'   }

	class Test(Base) :
		target            = 'test'
		environ_cmd       = {'PATH':'bar1'}
		environ_resources = {'R'   :'bar2'}
		def cmd() :
			import os
			assert os.environ['PATH'].endswith(':foo1:bar1')
			assert os.environ['R'   ]=='foo2/bar2'

else :

	import ut

	ut.lmake('test',done=1)
