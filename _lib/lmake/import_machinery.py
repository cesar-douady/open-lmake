# Ths file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys as _sys

from . import report_import # from clmake

from . import _maybe_lcl

def _fix_path() :
	try :
		if not _sys.path[0] : _sys.path.append(_sys.path.pop(0)) # put entry invented by python at the end to avoid too numerous deps
	except :
		pass

if _sys.version_info.major==2 :

	_std_suffixes = ['.py','.so'] # standard suffixes are not available with python2

	# python accesses pyc files and merely stats the py file to check date
	# Safer to explicitly depend on py file
	class _Depend :                              # this a special finder that explicitly depends on searched files, ...
		@staticmethod                            # ... but otherwise finds no module, so that system machinery is actually used to load module
		def find_module(module_name,path=None) :
			report_import(module_name,path,module_suffixes)

	def fix_import() :
		'''fix imports so as to be sure all files needed to do an import are correctly reported as deps'''
		if _Depend in _sys.meta_path : return                                                              # already called
		#
		_sys.meta_path.insert(0,_Depend)
		_fix_path()

else :

	import importlib.machinery as _machinery

	_std_suffixes = _machinery.all_suffixes()

	# Instead of trying to access file candidates implementing the module, python optimizes by first reading the englobing dir and only access existing files.
	# This is severely anti-lmake :
	# - if a file can be generated, there will be no dep (as the file is not accessed)
	# - this may lead to a non-existing module without job rerun
	# - or worse : a following file may be used
	# To prevent that, deps are expclitly put on all candidate files before loading module
	class _Depend :                                   # this a special finder that explicitly depends on searched files, ...
		@staticmethod                                 # ... but otherwise finds no module, so that system machinery is actually used to load module
		def find_spec(module_name,path,target=None) :
			if path and not isinstance(path,(tuple,list)) : path = tuple(path)
			report_import(module_name,path,module_suffixes)

	def fix_import() :
		'''fix imports so as to be sure all files needed to do an import are correctly reported as  deps (not merely those that exist)'''
		if _Depend in _sys.meta_path : return                                                                                             # already called
		#
		try    : _sys.meta_path.insert( _sys.meta_path.index(_machinery.PathFinder) , _Depend ) # put dependency checker before the first path based finder
		except : _sys.meta_path.append(                                               _Depend ) # or at the end if none is found
		# python3 optimizes imports by reading dirs in path and only access files found there, defeating autodep ability to discover dep to inexistent files
		report_import()                                                                         # Python does some imports at start-up and read the dirs in sys.path
		_fix_path()

	def load_module( name , file=None ) :
		'''
			load a module from name and file (if provided, else use python machinery)
			do not update sys.modules
		'''
		import importlib.util as import_util
		if file : spec = import_util.spec_from_file_location( name , file )
		else    : spec = import_util.find_spec(name)
		if spec is None : raise ImportError('module '+name+' not found') # cannot use f-string as syntax must be python2 compatible
		mod = import_util.module_from_spec(spec)
		spec.loader.exec_module(mod)
		return mod

module_suffixes = [ i+s for i in ('','/__init__') for s in _std_suffixes ]
