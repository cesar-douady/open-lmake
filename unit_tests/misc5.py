# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake

	from lmake.rules import PyRule

	lmake.manifest = ('Lmakefile.py',)

	class PyEnv(PyRule) :
		targets = { 'DST' : 'py_env/{*:.*}' }
		cmd = '''
			python3 -m venv py_env
			. py_env/bin/activate
		'''

else :

	import ut

	ut.lmake('py_env/pyvenv.cfg',done=1)
