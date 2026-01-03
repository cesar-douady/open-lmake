# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

'This module is a runtime support for running python jobs under pdb control'

# /!\ must be python2/python3 compatible
# /!\ this file must *not* be named pdb.py or the system pdb module cannot be imported

from .utils import load_modules

def run_py(dbg_dir,deps,func,*args,**kwds) :
	import os
	import pdb
	import sys
	import traceback
	load_modules(func,deps)
	stdin = os.environ.get('LMAKE_DEBUG_STDIN',None)
	if stdin :
		try    : stdin = open(stdin,'r')
		except : raise RuntimeError('cannot debug with pdb when stdin is a dep and views are used')
	debugger = pdb.Pdb(stdin=stdin,stdout=sys.stderr)
	try :
		debugger.runcall(func,*args,**kwds)
	except BaseException as e :
		traceback.print_exc()
		debugger.interaction(None,sys.exc_info()[2])
