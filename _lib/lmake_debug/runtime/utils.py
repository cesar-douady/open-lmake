# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

# /!\ must be python2/python3 compatible

Code = (lambda:None).__code__.__class__

def load_modules(func,deps) :
	import sys
	import keyword
	import os.path as osp
	from importlib import import_module
	if sys.version_info.major<3 :
		import re
		is_id_re = re.compile(r'[a-zA-Z_]\w*\Z')
		def is_id(x) :
			return is_id_re.match(x) and not keyword.iskeyword(x)
		def source_from_cache(pyc) :
			assert pyc[-1]=='c'
			return pyc[:-1]
	elif sys.version_info<(3,4) :
		def is_id(x) :
			return x.isidentifier() and not keyword.iskeyword(x)
		def source_from_cache(pyc) :
			dir,base = ('/'+pyc).rsplit('/__pycache__/',1)                         # __pycache__ can be at the top level
			if dir : dir = dir[1:]+'/'                                             # remove initial /, add a final /, if empty, these 2 operations cancel each other
			base = base.split('.',1)[0]
			return dir+base+'.py'
	else :
		def is_id(x) :
			return x.isidentifier() and not keyword.iskeyword(x)
		from importlib.util import source_from_cache
	#
	# load modules containing serialized functions and substitute corresponding code
	# if we do not do that, breakpoints may be put at the wrong place
	func_lst = []
	try :
		for k,f in func.__globals__.items() :
			try                   : f.__qualname__ ; m = f.__module__
			except AttributeError : continue
			func_lst.append((f,import_module(m)))
	except :
		return                                                                     # do not pretend we are in original source code if we have trouble importing any required module
	for f,m in func_lst :
		mf = m                                                                     # find function by doing a lookup with the qualified name, it is done for that
		for c in f.__qualname__.split('.') : mf = getattr(mf,c)
		try                   : mf.im_func.__code__ = f.__code__                   # substitute code to ensure breakpoints are put at the right place
		except AttributeError : mf.        __code__ = f.__code__                   # .
	#
	# load all deps that look like imported modules so as to populate module list and ease setting breakpoints ahead of execution
	# pre-condition path for speed as there may be 1000's of deps
	path = sorted( (osp.abspath(p)+'/' for p in sys.path) , key=lambda x:-len(x) ) # longest first to have the best possible match when trying with startswith
	for d in deps :
		d = osp.abspath(d)
		if     d.endswith('.pyc') : d = source_from_cache(d)
		if not d.endswith('.py' ) : continue                                       # not an importable module
		for p in path :
			if not d.startswith(p) : continue
			m = d[len(p):].split('/')
			if not all( is_id(c) for c in m ) : break                              # no hope to find an alternative as we try longest first
			try    : import_module('.'.join(m))
			except : pass                                                          # this is a cosmetic improvement, no harm if we fail
			break                                                                  # found, go to next dep
