# This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys

if getattr(sys,'reading_makefiles',False) :

	import os

	import lmake

	lmake.sources = ('Lmakefile.py',)

	class Home(lmake.Rule) :
		target = 'home'
		cmd    = '[ $HOME = $ROOT_DIR ]'

	class Homeless1(lmake.HomelessRule) :
		target = 'homeless1'
		cmd    = '[ $HOME = $TMPDIR ]'

	class Homeless2(lmake.HomelessRule) :
		target = 'homeless2'
		cmd    = '[ {0}$HOME = {0}$TMPDIR ]'                                   # force python execution of f-string

	class Homeless3(lmake.HomelessRule) :
		target = 'homeless3'
		def cmd() :
			assert os.environ['HOME'] == os.environ['TMPDIR']

else :

	import ut

	ut.lmake( 'home','homeless1','homeless2','homeless3' , done=4 )
