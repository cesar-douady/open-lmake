# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

# use a function to ensure no name pollution to be totally transparent
def do_job() :

	g = globals()   # avoid calling globals() too many times
	del g['do_job'] # dont even leave a single name (only automatic dunders will be left and this is important for our identity)

	import os
	import os.path as osp
	import sys

	import lmake.import_machinery as lim

	#
	# payload : ensure proper deps when importing modules
	#

	lim.fix_import()

	#
	# chain other potential other usercustomize modules
	#

	try                   : file = os.fspath(__file__)
	except AttributeError : file =           __file__

	cwd          = os.getcwd()                        # avoid multiple calls to getcwd()
	abs_file     =   osp.normpath(osp.join(cwd,file))
	abs_sys_path = [ osp.normpath(osp.join(cwd,p   )) for p in sys.path ]

	try:
		my_idx  = abs_sys_path.index(osp.dirname(abs_file))
	except ValueError:
		my_idx = None
	if my_idx != None :
		prev_dirs = set(abs_sys_path[:my_idx+1])
		for dir in abs_sys_path[my_idx+1:] :
			target = osp.join( dir , 'usercustomize.py' )
			if dir in prev_dirs       : continue                     # ignore duplicates
			if not osp.exists(target) : continue
			#
			next_mod = lim.load_module( __name__+"_next" , target )
			for k,v in next_mod.__dict__.items() : g.setdefault(k,v) # inject defs from next module
			break

do_job()
