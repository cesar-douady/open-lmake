# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

'This module is a runtime support for running python jobs under pudb control'

# /!\ must be Python2/Python3 compatible
# /!\ this file must *not* be named pudb.py or the system pdb module cannot be imported

from .utils import Code,load_modules

def run_py(dbg_dir,deps,func,*args,**kwds) :
	import os
	import pudb.debugger
	#
	if True    : os.environ['PUDB_TTY'       ] = '/dev/tty'
	if dbg_dir : os.environ['XDG_CONFIG_HOME'] = dbg_dir+'/config'
	load_modules(func,deps)
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
