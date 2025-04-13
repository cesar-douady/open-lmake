# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

# /!\ this file must be python2/python3 compatible

"""
Lmakefile.py must :
- define config :
	- update variable lmake.config (cf. thoroughly commented source file named in lmake.config.__file__)
- define rules :
	- either by defining classes inheriting from one of the base rule classes defined in lmake.rules (cf. thoroughly commented source file named in lmake.rules.__file__)
	- or define a callable or a sub-module named rules that does the same thing when calle/imported
- define sources :
	- do nothing : default is to list files in Manifest or by searching git (including sub-modules)
	- define variable lmake.manifest as a list or a tuple that lists sources
	- or define a callable or a sub-module named sources that does the same thing when calle/imported
"""

import sys as _sys
import os  as _os

if _sys.version_info.major>=3 :
	try                        : from clmake     import * # if not in an lmake repo, top_repo_root is not set to current dir
	except ModuleNotFoundError : from .py_clmake import *
else :
	try                        : from clmake2    import * # .
	except ImportError         : from .py_clmake import *

from .utils import *

root_dir     = repo_root     # XXX> : until backward compatibility can be broken
top_root_dir = top_repo_root # XXX> : until backward compatibility can be broken

version = ('$VERSION',$TAG) # substituted at build time

def check_version(major,minor=0) :
	if major!=version[0] or minor>version[1] : raise RuntimeError('required version '+str((major,minor))+' is incompatible with native version '+str(version))

def _maybe_lcl(file) :
	'fast check for local files, avoiding full absolute path generation'
	return not file or file[0]!='/' or file.startswith(top_repo_root)

from .config import config
manifest = []
_rules   = []

class Autodep :
	"""
		context version of the set_autodep function (applies to this process and all processes started in the protected core)
		usage :
			with Autodep(enable) :
				<code with autodep activated (enable=True) or deactivated (enable=False)>
	"""
	IsFake = getattr(set_autodep,'is_fake',False)
	def __init__(self,enable) :
		self.cur  = enable
	def __enter__(self) :
		self.prev = get_autodep()
		set_autodep(self.cur)
	def __exit__(self,*args) :
		set_autodep(self.prev)

#def run_cc(*cmd_line,marker='...',stdin=None) :                             # XXX> : use this prototype when python2 is no more supported
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
	marker = '...'                                                           # XXX> : suppress kwds analysis when python2 is no more supported
	stdin  = None
	for k,v in kwds.items() :
		if   k=='marker' : marker = v
		elif k=='stdin'  : stdin  = v
		else             : raise TypeError('unexpected keyword argument '+k)
	import subprocess as sp                                                  # only import modules when necessary (so avoid import at the top level)
	dirs = set()
	for i,x in enumerate(cmd_line) :
		for k in ('-I','-iquote','-isystem','-dirafter','-L') :
			if   not x.startswith(k)      : continue
			elif x!=k                     : d = x[len(k):]
			elif i==len(cmd_line)-1       : continue
			else                          : d = cmd_line[i+1]
			if   d.startswith('=')        : continue
			if   d.startswith('$SYSROOT') : continue
			dirs.add(d)
	if dirs :
		# an alternative to depend(read=True) is check_deps(verbose=True) :
		# - depend(read=True) provides a proof of existence of marker files, hence their containing dirs
		# - check_deps(verbose=True) would ensure dirs exist when call returns as without verbose, job continues while waiting for server reply,
		#   and may even finish without the dir existing and in that case it would not be rerun
		depend(*(d+'/'+marker for d in dirs),required=False,read=True)       # ensure dirs exist
		check_deps()                                                         # avoid running compiler without proper markers
	return sp.run(
		cmd_line
	,	universal_newlines=True
	,	stdin=stdin , stdout=sp.PIPE
	,	check=True
	)

try :
	(lambda:None).__code__.replace(co_filename='',co_firstlineno=1) # check if we have the replace method
	def _sourcify(func,module,qualname,file_name,firstlineno) :
		func.__code__     = func.__code__.replace( co_filename=file_name , co_firstlineno=firstlineno )
		func.__module__   = module
		func.__qualname__ = qualname
except :                                                            # else revert to heavy old-fashioned code
	def _sourcify(func,module,qualname,file_name,firstlineno) :
		c    = func.__code__
		args = [c.co_argcount]
		if hasattr(c,'co_posonlyargcount') : args.append(c.co_posonlyargcount)
		if hasattr(c,'co_kwonlyargcount' ) : args.append(c.co_kwonlyargcount )
		args += (
			c.co_nlocals , c.co_stacksize , c.co_flags , c.co_code , c.co_consts , c.co_names , c.co_varnames
		,	file_name
		,	c.co_name
		,	firstlineno
		,	c.co_lnotab,c.co_freevars,c.co_cellvars
		)
		func.__code__     = c.__class__(*args)
		func.__module__   = module
		func.__qualname__ = qualname

def _find_cc_ld_library_path(cc) :
	'''
		_find_cc_ld_library_path(my_compiler) returns and adequate content to put in LD_LIBRARY_PATH for programs compiled with my_compiler.
	'''
	import subprocess as sp
	import os.path    as osp
	return sp.run(
		(osp.dirname(osp.dirname(osp.dirname(__file__)))+'/_bin/find_cc_ld_library_path',cc)
	,	universal_newlines=True
	,	stdout=sp.PIPE
	,	check=True
	).stdout.strip()
