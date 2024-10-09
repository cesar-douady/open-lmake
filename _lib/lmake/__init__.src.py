# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys     as _sys
import os      as _os
import os.path as _osp

try :
	if _sys.version_info.major<3 : from clmake2 import * # if not in an lmake repo, root_dir is not set to current dir
	else                         : from clmake  import * # .
	_has_clmake = True
except :
	_has_clmake = False
	from py_clmake import *

from .utils import *

version = ('$VERSION',0) # substituted at build time

def check_version(major,minor=0) :
	'''check version'''
	if major!=version[0] or minor>version[1] : raise RuntimeError('required version '+str((major,minor))+' is incompatible with native version '+str(version))

# Lmakefile must :
# - update variable lmake.config : the server configuration, default is a reasonable configuration
# - define rules :
#	- either by defining classes inheriting from one of the base rule classes : lmake.Rule, lmake.Antirule, lmake.PyRule, etc.
#	- or set lmake.config.rules_module to specify a module that does the same thing when imported
# - define sources :
#	- do nothing : default is to list files in Manifest or by searching git (including sub-modules)
#	- define variable lmake.manifest as a list or a tuple that lists sources
#	- set lmake.config.sources_module to specify a module that does the same thing when imported

manifest = []
_rules   = []

if _os.environ.get('LMAKE_ACTION')=='config' :
	from .config import config

class Autodep :
	"""context version of the set_autodep function (applies to this process and all processes started in the protected core)
usage :
	with Autodep(enable) :
		<code with autodep activated (enable=True) or deactivated (enable=False)>
	"""
	IsFake = not _has_clmake
	def __init__(self,enable) :
		self.cur  = enable
	def __enter__(self) :
		self.prev = get_autodep()
		set_autodep(self.cur)
	def __exit__(self,*args) :
		set_autodep(self.prev)
