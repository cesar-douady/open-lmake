# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	lmake.config.local_admin_dir  = 'LMAKE_LOCAL'
	lmake.config.remote_admin_dir = 'LMAKE_REMOTE'

	class Test(Rule) :
		target = 'test'
		cmd    = ''

else :

	import ut

	ut.lmake( 'test' , done=1 )
