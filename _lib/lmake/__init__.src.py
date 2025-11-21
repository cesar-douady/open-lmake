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

version = ('$VERSION',$TAG) # substituted at build time

def check_version(major,minor=0) :
	if major!=version[0] or minor>version[1] : raise RuntimeError('required version '+str((major,minor))+' is incompatible with native version '+str(version))

def _maybe_lcl(file) :
	'fast check for local files, avoiding full absolute path generation'
	return not file or file[0]!='/' or file.startswith(top_repo_root)

#
# config, manifest and _rules are meant to be overwritten when import Lmakefile
#

from .config_ import config
manifest       = []
extra_manifest = []
_rules         = []

#
#
#

class Autodep :
	"""
		context version of the set_autodep function (applies to this process and all processes started in the protected core)
		usage :
			with Autodep(enable) :
				<code with autodep activated (enable=True) or deactivated (enable=False)>
	"""
	@staticmethod
	def set_env(var,val) :
		if val is None : _os.environ.pop(var,None)
		else           : _os.environ[var] = val

	IsFake    = getattr(set_autodep,'is_fake',False)
	LdPreload = _os.environ.get('LD_PRELOAD')
	LdAudit   = _os.environ.get('LD_AUDIT'  )
	def __init__(self,enable) :
		self.enable  = enable
	def __enter__(self) :
		self.prev_state      = get_autodep()
		self.prev_ld_preload = _os.environ.get('LD_PRELOAD')
		self.prev_ld_audit   = _os.environ.get('LD_AUDIT'  )
		#
		set_autodep(self.enable)
		self.set_env( 'LD_PRELOAD' , self.LdPreload if self.enable else None ) # propagate to sub-processes if autodep is not ptrace (handled in ptrace code)
		self.set_env( 'LD_AUDIT'   , self.LdAudit   if self.enable else None ) # .
	def __exit__(self,*args) :
		set_autodep(self.prev_state)
		self.set_env( 'LD_PRELOAD' , self.prev_ld_preload )
		self.set_env( 'LD_AUDIT'   , self.prev_ld_audit   )

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
	marker = '...'
	stdin  = None
	for k,v in kwds.items() :                                                # XXX> : suppress kwds analysis when python2 is no more supported
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
	# python version >= 3.10 (simpler, more reliable as we do not need the exact order of arguments)
	(lambda:None).__code__.replace(co_filename='',co_firstlineno=1)
	def _sourcify(func,module,qualname,file_name,firstlineno) :
		func.__code__     = func.__code__.replace( co_filename=file_name , co_firstlineno=firstlineno )
		func.__module__   = module
		func.__qualname__ = qualname
except :
	# python version < 3.10 (fall back to more fragile code if we have no choice)
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
		,	c.co_lnotab,c.co_freevars,c.co_cellvars # co_lnotab is deprecated since 3.12 (but used before 3.10)
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

def rm_target_tree( dir , regexpr=None ) :
	'''
		Remove targets generated in dir and matching regexpr.
		Enclosing dirs that become empty are removed as well if they are dir (if `top`) or below it.
	'''
	import os.path as osp
	assert dir and not (dir+'/').startswith('../') and dir[0]!='/' , 'dir must be a sub-dir, not '+dir
	#
	targets = list_targets(dir,regexpr)
	pfx     = list_root(dir)
	pfx     = ''   if pfx=='.' else pfx+'/'
	dirs    = {''}                                                                 # accumulate seen dirs so as to remove dirs that have become empty
	# remove files
	for t in targets :
		assert t.startswith(pfx) , 'listed target not in asked dir ('+dir+') : '+t
		assert t[0]!='/'         , 'target should be relative : '               +t # defensive programming : ensure no catastrophic unlink
		_os.unlink(t)
		sub_t = t[len(pfx):]
		if not sub_t : continue
		d = osp.dirname(sub_t)
		while d :
			dirs.add(d)
			d = osp.dirname(d)
	# remove dirs
	dirs = sorted(dirs,reverse=True)                                               # sort dirs so that sub-dirs appear before parent
	for d in dirs :
		try    : _os.rmdir(pfx+d)
		except : pass

def cp_target_tree( from_dir , to_dir , regexpr=None ) :
	'''
		Copy targets generated in from_dir and matching regexpr to to_dir.
	'''
	import os.path as osp
	assert from_dir and not (from_dir+'/').startswith('../') and from_dir[0]!='/' , 'from_dir must be a sub-dir, not '+from_dir
	assert to_dir   and not (to_dir  +'/').startswith('../') and to_dir  [0]!='/' , 'to_dir must be a sub-dir, not '  +to_dir
	#
	targets = list_targets(from_dir,regexpr)
	from_   = list_root(from_dir)
	from_s  = ''   if from_ =='.' else from_  +'/'
	to_s    = ''   if to_dir=='.' else to_dir +'/'
	# copy files
	for from_t in targets :
		assert from_t.startswith(from_s) , 'listed target not in asked dir ('+from_dir+') : '+from_t
		assert from_t[0]!='/'            , 'target should be relative : '                    +from_t # defensive programming : ensure no catastrophic unlink
		to_t = to_s+from_t[len(from_s):]
		to_d = osp.dirname(to_t)
		if   to_d               : _os.makedirs(to_d,exist_ok=True)
		if   osp.islink(from_t) : _os.symlink(_os.readlink(from_t),to_t)
		elif osp.isfile(from_t) : open(to_t,'w').write(open(from_t).read())
		else                    : raise RuntimeError('file is neither regular nor a link : '+from_t)

def mv_target_tree( from_dir , to_dir , regexpr=None ) :
	'''
		Move targets generated in from_dir and matching regexpr to to_dir.
	'''
	import os.path as osp
	assert from_dir and not (from_dir+'/').startswith('../') and from_dir[0]!='/' , 'from_dir must be a sub-dir, not '+from_dir
	assert to_dir   and not (to_dir  +'/').startswith('../') and to_dir  [0]!='/' , 'to_dir must be a sub-dir, not '  +to_dir
	#
	targets = list_targets(from_dir,regexpr)
	from_   = list_root(from_dir)
	from_s  = ''   if from_ =='.' else from_ +'/'
	to_s    = ''   if to_dir=='.' else to_dir+'/'
	dirs    = {''}
	# rename files
	for from_t in targets :
		assert from_t.startswith(from_s) , 'listed target not in asked dir ('+from_dir+') : '+from_t
		assert from_t[0]!='/'            , 'target should be relative : '                    +from_t # defensive programming : ensure no catastrophic unlink
		sub_t = from_t[len(from_s):]
		to_t  = to_s+sub_t
		to_d  = osp.dirname(to_t)
		if to_d : _os.makedirs(to_d,exist_ok=True)
		_os.rename(from_t,to_t)
		if not sub_t : continue
		d = osp.dirname(sub_t)
		while d :
			dirs.add(d)
			d = osp.dirname(d)
	# remove dirs
	dirs = sorted(dirs,reverse=True)                                                                 # sort dirs so that sub-dirs appear before parent
	for d in dirs :
		try    : _os.rmdir(pfx+d)
		except : pass
