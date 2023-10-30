# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

deps = ()                                                                      # this is overwritten by calling script when debugging before calling hack_* functions

Code = (lambda:None).__code__.__class__

def load_modules() :
	import sys
	import importlib.util
	import os.path as osp
	#
	# load modules containing serialized functions and substitute corresponding code
	# if we do not do that, breakpoints may be put at the wrong place
	sys.lmake_read_makefiles = True                                            # ensure makefiles are read the same way they are read when lmake started
	for f in lmake_func.func_lst :                                             # this lists all serialized functions
		m  = importlib.import_module(f.__module__)
		mf = m                                                                 # find function by doing a lookup with the qualified name, it is done for that
		for c in f.__qualname__.split('.') : mf = getattr(mf,c)
		mf.__code__ = f.__code__                                               # substitute code to ensure breakpoints are put at the right place
	del sys.lmake_read_makefiles
	#
	# load all deps that look like imported modules so as to populate module list and ease setting breakpoints ahead of execution
	# pre-condition path for speed as there may be 1000's of deps
	path = sorted( (osp.abspath(p)+'/' for p in sys.path) , key=lambda x:-len(x) ) # longest first to have the best possible match when trying with startswith
	for d in deps :
		d = osp.abspath(d)
		if d.endswith('.pyc')    : d = importlib.util.source_from_cache(d)
		if not d.endswith('.py') : continue                                    # not an importable module
		for p in path :
			if not d.startswith(p) : continue
			m = d[len(p):].split('/')
			if not all( c.isidentifier() and not keyword.iskeyword(c) for c in m ) : break # no hope to find an alternative as we try longest first
			try    : importlib.import_module('.'.join(m))
			except : pass                                                      # this is a cosmetic improvement, no harm if we fail
			break                                                              # found, go to next dep

def hack_pdb(dbg_dir,redirected) :
	# hack pdb.Pdb class so that we can redirect stdin & stdout while still debugging from the console
	# as pdb.set_trace and other pdb commands call pdb.Pdb without arguments
	import pdb
	load_modules()
	class Pdb(pdb.Pdb) :
		if redirected :
			def __init__(self,*args,stdin=open('/dev/tty','r'),stdout=open('/dev/tty','w'),**kwds) :
				super().__init__(*args,stdin=stdin,stdout=stdout,**kwds)
	pdb.Pdb = Pdb
	return pdb

def hack_pudb(dbg_dir,redirected) :
	import os
	import pudb.debugger
	#
	if True    : os.environ['PUDB_TTY'       ] = '/dev/tty'
	if dbg_dir : os.environ['XDG_CONFIG_HOME'] = dbg_dir+'/config'
	load_modules()
	if pudb.NUM_VERSION==(2023,1) :
		#
		# hack pudb._get_debugger !
		# else, pudb does not start with a provided tty
		#
		def _get_debugger(**kwargs):
			from pudb.debugger import Debugger
			if not Debugger._current_debugger:
				tty_path = pudb._tty_override()
				if tty_path and ("stdin" not in kwargs or "stdout" not in kwargs):
					tty_file, term_size = pudb._open_tty(tty_path)
					kwargs.setdefault("stdin", tty_file)
					kwargs.setdefault("stdout", tty_file)
					kwargs.setdefault("term_size", term_size)
					#tty_file.close()                                          # <======== This line is meaningless !!!
				from pudb.debugger import Debugger
				dbg = Debugger(**kwargs)
				return dbg
			else:
				return Debugger._current_debugger[0]
		pudb._get_debugger = _get_debugger
		#
		# hack pudb.debugger.DebuggerUI.__init__.pick_module.mod_exits !
		# else the 'm' command shows a giant list of ininteresting modules
		#
		def mod_exists(mod) :                                                  # /!\ because we insert the code of this function in another environment, we cannot access globals here
			import os
			import os.path as osp
			import importlib.util
			if not getattr(mod,'__file__',None) : return False
			#
			filename = osp.abspath(mod.__file__)
			#
			if not filename.startswith(os.getcwd()+'/') : return False
			if filename.endswith('.pyc')                : filename = importlib.util.source_from_cache(filename)
			if not filename.endswith('.py')             : return False
			else                                        : return osp.exists(filename)
		#
		# hack pudb.debugger.DebuggerUI.__init__.show_output as it reads from sys.stdin instead of reading from stdin
		# else the 'o' command does not work when debuggee has its input redirected
		#
		def show_output(w,size,key) :
			# we must manage to avoid cell vars as we have no way to access them from this hacked code
			# fortunately, the only debugger is recorded in the _current_debugger singleton
			self   = Debugger._current_debugger[0].ui
			stdin  = self.screen._term_input_file
			stdout = self.screen._term_output_file
			with StoppedScreen(self.screen) :
				stdout.write('press enter to continue')
				stdin.readline()
		#
		ui = pudb.debugger.DebuggerUI.__init__
		for c in ui.__code__.co_consts :
			if isinstance(c,Code) and c.co_name=='pick_module' :
				old_pick_module_code = c
				break
		else :
			raise RuntimeError('cannot hack pudb because cannot find function pick_module in pudb.debugger.DebuggerUI')
		#
		new_pick_module_consts = tuple(
			c                    if not isinstance(c,Code)   else
			mod_exists .__code__ if c.co_name=='mod_exists'  else
			show_output.__code__ if c.co_name=='show_output' else
			c
			for c in old_pick_module_code.co_consts
		)
		new_pick_module_code = _replace_consts(old_pick_module_code,new_pick_module_consts)
		n_diffs = sum( a!=b for a,b in zip(old_pick_module_code.co_consts,new_pick_module_consts) )
		if n_diffs!=1 : raise RuntimeError(f'cannot hack pudb : found {n_diffs} diffs in pudb.DebuggerUI.__init__.pick_module')
		new_init_consts = tuple(
			c                    if not isinstance(c,Code)   else
			new_pick_module_code if c.co_name=='pick_module' else
			show_output.__code__ if c.co_name=='show_output' else
			c
			for c in ui.__code__.co_consts
		)
		n_diffs = sum( a!=b for a,b in zip(ui.__code__.co_consts,new_init_consts) )
		if n_diffs!=2 : raise RuntimeError(f'cannot hack pudb : found {n_diffs} diffs in pudb.DebuggerUI.__init__')
		#
		# staple new code
		#
		new_init_code   = _replace_consts(ui.__code__,new_init_consts)
		ui.__code__     = new_init_code

	else :
		raise RuntimeError(f'cannot hack pudb with unknown version {pudb.NUM_VERSION}')
	return pudb

# add original debug info to func
def lmake_func(func) :
	try :
		module,qual_name,filename,first_line_no = lmake_func.dbg[func.__name__]
		#
		lmake_func.func_lst.append(func)
		#
		func.__module__   = module
		func.__qualname__ = qual_name
		func.__code__     = _replace_filename_firstlineno(func.__code__,filename,first_line_no)
	except : pass                                                                               # this is purely cosmetic, if it does not work, no harm
	return func
lmake_func.func_lst = []

def _replace_filename_firstlineno(code,filename,firstlineno) :
	try :
		# Python version >= 3.10 (simpler, more reliable as we do not need the exact order of arguments)
		return code.replace( co_filename=filename , co_firstlineno=firstlineno )
	except :
		# Python version < 3.10 (fall back to more fragile code if we have no choice)
		return Code(
			code.co_argcount
		,	*( (code.co_posonlyargcount,) if hasattr(Code,'co_posonlyargcount') else () )
		,	*( (code.co_kwonlyargcount ,) if hasattr(Code,'co_kwonlyargcount' ) else () )
		,	code.co_nlocals
		,	code.co_stacksize
		,	code.co_flags
		,	code.co_code
		,	code.co_consts
		,	code.co_names
		,	code.co_varnames
		,	filename                   # co_filename
		,	code.co_name
		,	firstlineno                # co_firstlineno
		,	code.co_lnotab
		,	code.co_freevars
		,	code.co_cellvars
		)

def _replace_consts(code,consts) :
	try :
		# Python version >= 3.10 (simpler, more reliable as we do not need the exact order of arguments)
		return code.replace(co_consts=consts)
	except :
		# Python version < 3.10 (fall back to more fragile code if we have no choice)
		return Code(
			code.co_argcount
		,	*( (code.co_posonlyargcount,) if hasattr(Code,'co_posonlyargcount') else () )
		,	*( (code.co_kwonlyargcount ,) if hasattr(Code,'co_kwonlyargcount' ) else () )
		,	code.co_nlocals
		,	code.co_stacksize
		,	code.co_flags
		,	code.co_code
		,	consts                     # co_consts
		,	code.co_names
		,	code.co_varnames
		,	code.co_filename
		,	code.co_name
		,	code.co_firstlineno
		,	code.co_lnotab
		,	code.co_freevars
		,	code.co_cellvars
		)
