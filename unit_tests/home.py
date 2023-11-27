# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import os

	import lmake
	from lmake.rules import Rule,HomelessRule

	lmake.manifest = ('Lmakefile.py',)

	class Home(Rule) :
		target = 'home'
		cmd    = '[ $HOME = $ROOT_DIR ]'

	class Homeless1(HomelessRule) :
		target = 'homeless1'
		cmd    = '[ $HOME = $TMPDIR ]'

	class Homeless2(HomelessRule) :
		target = 'homeless2'
		cmd    = '[ {0}$HOME = {0}$TMPDIR ]'                                   # force python execution of f-string

	class Homeless3(HomelessRule) :
		target = 'homeless3'
		def cmd() :
			assert os.environ['HOME'] == os.environ['TMPDIR']

else :

	import ut

	ut.lmake( 'home','homeless1','homeless2','homeless3' , done=4 )
