# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import os.path as _osp
import sys     as _sys

from . import depend,SuspendAutodep

module_suffixes = ('.so','.py','/__init__.py') # can be tailored to suit application needs, the lesser the better (less spurious dependencies)

_method = 1

if _method==1 :

	#
	# this version is more pythonic, but does only half of the job : it generates adequate deps, but does not suppress the superfluous ones
	#
	from . import root_dir
	def _maybe_local(file) :
		'fast check for local files, avoiding full absolute path generation'
		return not file or file[0]!='/' or file.startswith(root_dir)
	import importlib.machinery as _machinery
	_std_suffixes = _machinery.all_suffixes()+['/__init__.py'] # account for packages, not included in all_suffixes()

	def fix_import() :
		'''fixes imports so as to be sure all files needed to do an import is correctly reported (not merely those which exist)'''
		class Depend :
			@staticmethod
			def find_spec(module_name,path,target=None) :
				if path==None : path = _sys.path
				tail = module_name.rsplit('.',1)[-1]
				for dir in path :
					if dir : dir += '/'
					base = dir+tail
					if _maybe_local(base) :
						for suffix in module_suffixes :
							file = base+suffix
							depend(file,required=False,essential=False)
							if _osp.exists(file) : return
					else :
						for suffix in _std_suffixes :
							if _osp.exists(base+suffix) : return

		# give priority to system so as to avoid too numerous dependencies
		_sys.path = (
			[ d for d in _sys.path if not _maybe_local(d+'/') ]
		+	[ d for d in _sys.path if     _maybe_local(d+'/') ]
		)

		# put dependency checker before the first path based finder
		for i in range(len(_sys.meta_path)) :
			if _sys.meta_path[i]==_machinery.PathFinder :
				_sys.meta_path.insert(i,Depend)
				break
		else :
			_sys.meta_path.append(Depend)

elif method==2 :

	def fix_import() :
		'''replace __import__ by a semantically equivalent function (that actually call the original one) to generate clean deps'''
		if _sys.version_info.major==2 : import __builtins__ as builtins
		else                          : import   builtins
		orig_import = builtins.__import__
		# Instead of trying to access file candidates implementing the module, Python optimizes by first reading the englobing dir and only access existing files.
		# This is severely anti-lmake :
		# - if a file can be generated, there will be no dep (as the file is not accessed), this may lead to a non-existing module without job rerun or worse, a following file may be used
		# - a lot of sibling files will be accessed, triggering potentially unwanted deps
		# to prevent that, deps are managed explicitly
		def new_import(name,glbs={},*args,**kwds) :
			if name.startswith('.') :
				pkg = glbs['__package__']
				while name.startswith('..') :
					pkg  = pkg.rsplit('.',1)[0]
					name = name[1:]
				full_name = pkg+name
			else :
				full_name = name
			if full_name not in _sys.modules :
				file_name = full_name.replace('.','/')
				for d in _sys.path :
					if d : b = d+'/'+file_name
					else : b =       file_name
					for sfx in module_suffixes :
						f = b+sfx
						depend(f,required=False)
						if _osp.exists(f) : break
					else : continue
					break
			with SuspendAutodep() :
				return orig_import(name,glbs,*args,**kwds)
		builtins.__import__ = new_import

