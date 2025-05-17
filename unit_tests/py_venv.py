# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake

	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	class PyEnv(Rule) :
		targets   = { 'DST' : r'py_env/{*:.*}'   }
		side_deps = { 'PY'  : ('.','readdir_ok') } # python reads dirs listed in sys.path and this is not handled by PyRule for shell rules
		cmd = '''
			ldepend -D py_env
			python3 -m venv py_env
			. py_env/bin/activate
		'''

else :

	try :
		import venv
		venv.main(('py_env_trial',))
		open('py_env_trial/bin/activate')
	except :
		print('python venv not available',file=open('skipped','w'))

	import ut

	ut.lmake('py_env/pyvenv.cfg',done=1)
