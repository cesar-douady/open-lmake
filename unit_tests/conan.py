# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	class Pip(Rule) :
		stems = { 'ANYFILE':'.*' }
		targets = {
			'PIP'  : 'BUILD/venv/bin/pip',
			'VENV' : 'BUILD/venv/{ANYFILE*}',
		}
		readdir_ok = True
		cmd        = 'python3 -m venv BUILD/venv'

	class Conan(Rule) :
		targets = { 'CONAN'            : 'BUILD/venv/bin/conan' }
		environ = { 'PIP_NO_CACHE_DIR' : 'off'                  }
		deps    = { 'PIP'              : 'BUILD/venv/bin/pip'   }
		cmd     = 'BUILD/venv/bin/python3 -m pip install conan'
		readdir_ok = True

else :

	import ut

	if True :
		print('not implemented yet',file=open('skipped','w'))
		exit()

	ut.lmake( 'BUILD/venv/bin/conan' , done=2 )
