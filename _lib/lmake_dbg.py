# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

# /!\ must be Python2/Python3 compatible
# /!\ this file must be able to accept that its own path is not in sys.path, it is read with exec, not with import

deps = () # this is overwritten by calling script when debugging before calling hack_* functions

Code = (lambda:None).__code__.__class__

def load_modules(func) :
	import sys
	import keyword
	import os.path as osp
	from importlib import import_module
	#if sys.version_info<(3,6) : return                                            # importing user code implies importing lmake.rules code, which is written for Python3.6+
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
			try                   : m = f.__module__
			except AttributeError : continue
			func_lst.append((f,import_module(f.__module__)))
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

def run_pdb(dbg_dir,redirected,func,*args,**kwds) :
	import pdb
	import sys
	import traceback
	load_modules(func)
	if redirected : debugger = pdb.Pdb(stdin=open('/dev/tty','r'),stdout=open('/dev/tty','w'))
	else          : debugger = pdb.Pdb(                                                      )
	try :
		debugger.runcall(func,*args,**kwds)
	except BaseException as e :
		traceback.print_exc()
		debugger.reset()
		debugger.interaction(None,sys.exc_info()[2])

def run_vscode(dbg_dir,redirected,func,*args,**kwds) :
	import json
	import os
	try :
		# Write python process information to vscode debug workspace to allow gdb to attache to it
		workspace = dbg_dir + '/vscode/ldebug.code-workspace'
		if os.path.exists(workspace) :
			data = json.load(open(workspace))
			for elmt in data['launch']['configurations'] :
				if elmt.get('type')=='by-gdb' and 'processId' in elmt : elmt['processId'] = os.getpid()
			with open(workspace,'w') as out :
				json.dump(data,out,indent='\t')
				out.write('\n')
		# call cmd
		func(*args,**kwds)
	except BaseException as e :
		import traceback
		traceback.print_exception(e)

def run_pudb(dbg_dir,redirected,func,*args,**kwds) :
	import os
	import pudb.debugger
	#
	if True    : os.environ['PUDB_TTY'       ] = '/dev/tty'
	if dbg_dir : os.environ['XDG_CONFIG_HOME'] = dbg_dir+'/config'
	load_modules(func)
	if pudb.NUM_VERSION==(2023,1) :
		def replace_consts(code,consts) :
			try :
				# Python version >= 3.10 (simpler, more reliable as we do not need the exact order of arguments)
				return code.replace(co_consts=consts)
			except :
				# Python version < 3.10 (fall back to more fragile code if we have no choice)
				args = [
					code.co_argcount
				]
				if hasattr(Code,'co_posonlyargcount') : args.append(code.co_posonlyargcount)
				if hasattr(Code,'co_kwonlyargcount' ) : args.append(code.co_kwonlyargcount )
				args += [
					code.co_nlocals
				,	code.co_stacksize
				,	code.co_flags
				,	code.co_code
				,	consts
				,	code.co_names
				,	code.co_varnames
				,	code.co_filename
				,	code.co_name
				,	code.co_firstlineno
				,	code.co_lnotab
				,	code.co_freevars
				,	code.co_cellvars
				]
				return Code(*args)
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
					kwargs.setdefault("stdin"    ,tty_file )
					kwargs.setdefault("stdout"   ,tty_file )
					kwargs.setdefault("term_size",term_size)
					#tty_file.close()                        # <======== This line is meaningless !!!
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
		def mod_exists(mod) :                                # /!\ because we insert the code of this function in another environment, we cannot access globals here
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
		new_pick_module_code = replace_consts(old_pick_module_code,new_pick_module_consts)
		n_diffs = sum( a!=b for a,b in zip(old_pick_module_code.co_consts,new_pick_module_consts) )
		if n_diffs!=1 : raise RuntimeError('cannot hack pudb : found '+str(n_diffs)+' diffs in pudb.DebuggerUI.__init__.pick_module')
		new_init_consts = tuple(
			c                    if not isinstance(c,Code)   else
			new_pick_module_code if c.co_name=='pick_module' else
			show_output.__code__ if c.co_name=='show_output' else
			c
			for c in ui.__code__.co_consts
		)
		n_diffs = sum( a!=b for a,b in zip(ui.__code__.co_consts,new_init_consts) )
		if n_diffs!=2 : raise RuntimeError('cannot hack pudb : found '+str(n_diffs)+' diffs in pudb.DebuggerUI.__init__')
		#
		# staple new code
		#
		new_init_code = replace_consts(ui.__code__,new_init_consts)
		ui.__code__   = new_init_code
	else :
		raise RuntimeError('cannot hack pudb with unknown version '+str(pudb.NUM_VERSION))
	try    : pudb.runcall(func,*args,**kwds)
	except : pass
