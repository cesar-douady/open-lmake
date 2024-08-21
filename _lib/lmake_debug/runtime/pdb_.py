# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

# /!\ must be Python2/Python3 compatible
# /!\ this file must *not* be named pdb.py or the system pdb module cannot be imported

from .utils import load_modules

def run_py(dbg_dir,deps,func,*args,**kwds) :
	import os
	import pdb
	import sys
	import traceback
	load_modules(func,deps)
	try    : stdin  = open(os.environ['LMAKE_DEBUG_STDIN' ],'r')
	except : stdin  = None
	try    : stdout = open(os.environ['LMAKE_DEBUG_STDOUT'],'w')
	except : stdout = None
	debugger = pdb.Pdb(stdin=stdin,stdout=stdout)
	try :
		debugger.runcall(func,*args,**kwds)
	except BaseException as e :
		traceback.print_exc()
		debugger.interaction(None,sys.exc_info()[2])
