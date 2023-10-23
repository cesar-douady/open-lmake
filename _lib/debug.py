# This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys
import pdb

__all__ = ('pdb',)

# hack pdb.cmd.Cmd class so that we can redirect stdin & stdout while still debugging from the console
# console is accessible from file descriptors 3 & 4
class Pdb(pdb.Pdb) :
	def __init__(self,*args,**kwds) :
		if hasattr(sys,'dbg_console') :
			kwds['stdin' ] = sys.dbg_console[0]
			kwds['stdout'] = sys.dbg_console[1]
		super().__init__(*args,**kwds)
pdb.Pdb = Pdb
pdb.set_trace()
