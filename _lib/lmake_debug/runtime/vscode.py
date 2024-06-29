# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

# /!\ must be Python2/Python3 compatible
# /!\ this file must be able to accept that its own path is not in sys.path, it is read with exec, not with import

from . import utils

def run_py(dbg_dir,deps,func,*args,**kwds) :
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
