# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import os.path as _osp
import sys     as _sys

from . import depend

from . import _maybe_local

def _depend_module(module_name,path=None) :
	if path==None : path = _sys.path
	tail = module_name.rsplit('.',1)[-1]
	for dir in path :
		if dir : dir += '/'
		base = dir+tail
		if _maybe_local(base) :
			for suffix in module_suffixes :
				file = base+suffix
				depend(file,required=False,read=True)
				if _osp.exists(file) : return
		else :
			base_init = base+'/__init__'
			for suffix in _std_suffixes :
				if _osp.exists(base     +suffix) : return
				if _osp.exists(base_init+suffix) : return

if _sys.version_info.major==2 :

	_std_suffixes = ['.py','.so'] # standard suffixes are not available with Python2

	def fix_import() :
		'''fix imports so as to be sure all files needed to do an import are correctly reported as deps'''
		# Python accesses pyc files and merely stats the py file to check date
		# Safer to explicitly depend on py file
		class Depend :                               # this a special finder that explicitly depends on searched files, ...
			@staticmethod                            # ... but otherwise finds no module, so that system machinery is actually used to load module
			def find_module(module_name,path=None) :
				_depend_module(module_name,path)
		_sys.meta_path.insert(0,Depend)

else :

	import importlib.machinery as _machinery

	_std_suffixes = _machinery.all_suffixes()

	def fix_import() :
		'''fix imports so as to be sure all files needed to do an import are correctly reported as  deps (not merely those that exist)'''
		# Instead of trying to access file candidates implementing the module, Python optimizes by first reading the englobing dir and only access existing files.
		# This is severely anti-lmake :
		# - if a file can be generated, there will be no dep (as the file is not accessed)
		# - this may lead to a non-existing module without job rerun
		# - or worse : a following file may be used
		# To prevent that, deps are expclitly put on all candidate files before loading module
		class Depend :                                                                         # this a special finder that explicitly depends on searched files, ...
			@staticmethod                                                                      # ... but otherwise finds no module, so that system machinery is actually used to load module
			def find_spec(module_name,path,target=None) :
				_depend_module(module_name,path)
		try    : _sys.meta_path.insert( _sys.meta_path.index(_machinery.PathFinder) , Depend ) # put dependency checker before the first path based finder
		except : _sys.meta_path.append(                                               Depend ) # or at the end if none is found

module_suffixes = [ i+s for i in ('','/__init__') for s in _std_suffixes ]
