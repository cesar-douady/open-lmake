# This file is part of the lmake distribution (https://wgit.doliam.net/cdy/lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import os
import sys

if getattr(sys,'reading_makefiles',False) :

	import lmake

	lmake.sources = (
		'Lmakefile.py'
	,	'hello.tar'
	)

	class Tar(lmake.Rule) :
		targets = { 'TARGET' : '{File:.*}.tardir/{*:.*}' }
		deps    = { 'TAR'    : '{File}.tar'              }
		cmd     = 'tar mxaf $TAR -C $File.tardir'

else :

	import ut

	print('yes',file=open('inside','w'))
	os.system('tar cf hello.tar inside')
	os.unlink('inside')

	ut.lmake( 'hello.tardir/inside' , done=1 , new=1 )
