# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import importlib.machinery as _machinery
import os.path             as _osp
import sys                 as _sys

from . import root_dir,depend

module_suffixes = ('.so','.py','/__init__.py')                                 # can be tailored to suit application needs, the lesser the better (less spurious dependencies)

_std_suffixes = _machinery.all_suffixes()+['/__init__.py']                     # account for packages, not included in all_suffixes()
_local_admin  = 'LMAKE/'
_global_admin = root_dir+'/'+_local_admin

def _maybe_local(file) :
	'fast check for local files, avoiding full absolute path generation'
	return not file or file[0]!='/' or file.startswith(root_dir)

def fix_imports() :
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
