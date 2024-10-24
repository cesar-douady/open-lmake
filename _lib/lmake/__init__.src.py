# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

# /!\ this file must be Python2/Python3 compatible

"""
Lmakefile.py must :
- define config :
	- update variable lmake.config (cf. thoroughly commented source file named in lmake.config.__file__)
- define rules :
	- either by defining classes inheriting from one of the base rule classes defined in lmake.rules (cf. thoroughly commented source file named in lmake.rules.__file__)
	- or set lmake.config.rules_module to specify a module that does the same thing when imported
- define sources :
	- do nothing : default is to list files in Manifest or by searching git (including sub-modules)
	- define variable lmake.manifest as a list or a tuple that lists sources
	- set lmake.config.sources_module to specify a module that does the same thing when imported
"""

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
	if major!=version[0] or minor>version[1] : raise RuntimeError('required version '+str((major,minor))+' is incompatible with native version '+str(version))

manifest = []
_rules   = []

if _os.environ.get('LMAKE_ACTION')=='config' :
	from .config import config

class Autodep :
	"""
		context version of the set_autodep function (applies to this process and all processes started in the protected core)
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

#def run_cc(*cmd_line,marker='...',stdin=None) :                             # XXX : use this prototype when Python2 is no more supported
def run_cc(*cmd_line,**kwds) :
	'''
		Run cmd_line assuming it is a C/C++ compiler.
		In addition to executing cmd_line and returning a CompletedProcess object with the stdout and stderr attributes,
		arguments of the form '-I somedir' are processed to ensure the directories exist
		as neither gcc nor clang would report deps inside non-existent directories.
		This function is meant to work in presence of a DirRule rule that will simply create markers upon demand.
		You can pass keyword argument marker (defaults to '...') to specify the marker (a file inside dirs) that guarantees dir existences.
		You can exceptionnally pass stdin as the stdin argument.
	'''
	marker = '...'                                                           # XXX : suppress kwds analysis when Python2 is no more supported
	stdin  = None
	for k,v in kwds.items() :
		if   k=='marker' : marker = v
		elif k=='stdin'  : stdin  = v
		else             : raise TypeError('unexpected keyword argument '+k)
	import os                                                                # only import modules when necessary (so avoid import at the top level)
	import subprocess as sp                                                  # .
	dirs = set()
	for i,x in enumerate(cmd_line) :
		for k in ('-I','-iquote','-isystem','-dirafter','-L') :
			if not x.startswith(k)        : continue
			elif x!=k                     : d = x[len(k):]
			elif i==len(cmd_line)-1       : continue
			else                          : d = cmd_line[i+1]
			if   d.startswith('=')        : continue
			if   d.startswith('$SYSROOT') : continue
			dirs.add(d)
	if dirs :
		for d in dirs : os.makedirs(d,exist_ok=True)                         # this will suffice most of the time
		depend(*(d+'/'+marker for d in dirs),required=False)                 # this guarantees that in all cases, dirs will not be removed by server finding some dirs useless
		check_deps()                                                         # avoid running compiler without proper markers
	return sp.run(
		cmd_line
	,	universal_newlines=True
	,	stdin=stdin , stdout=sp.PIPE , stderr=sp.PIPE
	,	check=True
	)

def find_cc_ld_library_path(cc) :
	'''
		find_cc_ld_library_path(my_compiler) returns and adequate content to put in LD_LIBRARY_PATH for programs compiled with my_compiler.
	'''
	import subprocess as sp
	import os.path    as osp
	return sp.run(
		(osp.dirname(osp.dirname(osp.dirname(__file__)))+'/bin/find_cc_ld_library_path',cc)
	,	universal_newlines=True
	,	stdout=sp.PIPE
	,	check=True
	).stdout.strip()
