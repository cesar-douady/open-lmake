# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import os
import sys

if getattr(sys,'lmake_read_makefiles',False) :

	import lmake

	from lmake import multi_strip

	lmake.sources = ('Lmakefile.py',)

	file = 'a/b/c'
	class Tar(lmake.Rule) :
		targets = { 'TAR' : r'hello.tar{*:.*}' }
		cmd = multi_strip('''
			cd $TMPDIR
			mkdir -p $(dirname {file})
			echo yes >{file}
			tar cf $ROOT_DIR/hello.tar {file}
		''')
	class Untar(lmake.Rule) :
		targets = { 'TARGET' : '{File:.*}.tardir/{*:.*}' }
		deps    = { 'TAR'    : '{File}.tar'              }
		cmd     = 'tar mxaf {TAR} -C {File}.tardir'

	class Cpy(lmake.Rule) :
		target = 'cpy'
		dep    = f'hello.tardir/{file}'
		cmd    = 'cat'

else :

	import ut

	ut.lmake( 'cpy' , done=3 )
