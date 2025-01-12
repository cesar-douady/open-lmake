# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import os.path as _osp
import sys     as _sys

from . import depend,Autodep

module_suffixes = ('.so','.py','/__init__.py') # can be tailored to suit application needs, the lesser the better (less spurious dependencies)

from . import maybe_local

def _depend_module(module_name,path=None) :
	if path==None : path = _sys.path
	tail = module_name.rsplit('.',1)[-1]
	for dir in path :
		if dir : dir += '/'
		base = dir+tail
		if maybe_local(base) :
			for suffix in module_suffixes :
				file = base+suffix
				depend(file,required=False,read=True)
				if _osp.exists(file) : return
		else :
			for suffix in _std_suffixes :
				if _osp.exists(base+suffix) : return

if _sys.version_info.major==2 :

	_std_suffixes = ['.py','.so','/__init__.py'] # standard suffixes are not available with Python2

	def _gen_module_deps() :
		'''fix imports so as to be sure all files needed to do an import is correctly reported (not merely those that exist)'''
		# Instead of trying to access file candidates implementing the module, Python optimizes by first reading the englobing dir and only access existing files.
		# This is severely anti-lmake :
		# - if a file can be generated, there will be no dep (as the file is not accessed)
		# - this may lead to a non-existing module without job rerun
		# - or worse : a following file may be used
		# To prevent that, deps are expclitly put on all candidate files before loading module
		class Depend :
			@staticmethod
			def find_module(module_name,path=None) :
				_depend_module(module_name,path)
		# put dependency checker before accessing standard path based module loader
		_sys.meta_path.append(Depend)

	def fix_import() :
		_gen_module_deps()

else :

	import importlib.machinery as _machinery

	_std_suffixes = _machinery.all_suffixes()+['/__init__.py'] # account for packages, not included in all_suffixes()

	def _gen_module_deps() :
		'''fix imports so as to be sure all files needed to do an import are correctly reported (not merely those that exist)'''
		class Depend :
			@staticmethod
			def find_spec(module_name,path,target=None) :
				_depend_module(module_name,path)
		try    : _sys.meta_path.insert( _sys.meta_path.index(_machinery.PathFinder) , Depend ) # put dependency checker before the first path based finder
		except : _sys.meta_path.append(                                               Depend ) # or at the end if none is found

	def _mask_python_deps() :
		'''replace __import__ by a semantically equivalent function (that actually calls the original one) to suppress python generated deps'''
		# During import, python triggers deps on a lot of unwanted sibling files will be accessed
		# To prevent that, autodep is deactivated during import search, but re-enabled when actually importing
		import builtins
		orig_import = builtins.__import__
		def new_import(*args,**kwds) :
			with Autodep(False) :
				return orig_import(*args,**kwds)
		try :
			from importlib._bootstrap_external import _LoaderBasics
			orig_exec_module = _LoaderBasics.exec_module
			def new_exec_module(*args,**kwds) :
				with Autodep(True) :
					return orig_exec_module(*args,**kwds)
			_LoaderBasics.exec_module = orig_exec_module
		except :
			raise RuntimeError('masking python deps during import is not available for python%d.%d'%_sys.version_info[:2])
		builtins.__import__ = new_import                                                                                   # wrap at the end to avoid wraping our own imports

	def fix_import() :
		_mask_python_deps()
		_gen_module_deps ()
