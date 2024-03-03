# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

# /!\ must be Python2/Python3 compatible
# /!\ this file must be able to accept that its own path is not in sys.path, it is read with exec, not with import

# add original debug info to func
def lmake_sourcify(func,module,qualname,filename,firstlineno) :
	try :
		func.__module__   = module
		func.__qualname__ = qualname
		try :                                                                                          # Python version >= 3.10 (simpler, more reliable as we do not need the exact order of arguments)
			func.__code__ = func.__code__.replace( co_filename=filename , co_firstlineno=firstlineno )
		except :                                                                                       # Python version < 3.10 (fall back to more fragile code if we have no choice)
			code = func.__code__
			args = [
				code.co_argcount
			]
			if hasattr(code,'co_posonlyargcount') : args.append(code.co_posonlyargcount)
			if hasattr(code,'co_kwonlyargcount' ) : args.append(code.co_kwonlyargcount )
			args += [
				code.co_nlocals
			,	code.co_stacksize
			,	code.co_flags
			,	code.co_code
			,	code.co_consts
			,	code.co_names
			,	code.co_varnames
			,	filename                                                                               # co_filename
			,	code.co_name
			,	firstlineno                                                                            # co_firstlineno
			,	code.co_lnotab
			,	code.co_freevars
			,	code.co_cellvars
			]
			func.__code__ = code.__class__(*args)
	except : pass                                                                                      # this is purely cosmetic, if it does not work, no harm
