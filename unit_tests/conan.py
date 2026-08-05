# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	class Pip(Rule) :
		targets = {
			'PIP'  : 'venv/bin/pip'
		,	'VENV' : r'venv/{*:.*}'
		}
		readdir_ok = True
		cmd        = 'python3 -m venv venv'

	class Conan(Rule) :
		targets    = { 'CONAN'            : 'venv/bin/conan' }
		environ    = { 'PIP_NO_CACHE_DIR' : 'off'            }
		deps       = { 'PIP'              : 'venv/bin/pip'   }
		readdir_ok = True
		cmd        = 'venv/bin/python3 -m pip install conan'

else :

	import ut

	ut.lmake( 'venv/bin/conan' , done=1 , unlinked=... , failed=1 , rc=1 )
