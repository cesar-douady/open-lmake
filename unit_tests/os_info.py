# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import os

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	class Bad(Rule) :
		os_info = r'bad'
		target  = 'bad'
		cmd     = ''

	class Good(Rule) :
		os_info = 'go+d\n'
		os_info_file = os.getcwd()+'/LMAKE/lmake/os_info'
		target  = 'good'
		cmd     = ''

else :

	import ut

	os.makedirs('LMAKE/lmake',exist_ok=True)
	print('good',file=open('LMAKE/lmake/os_info','w'))

	ut.lmake( 'good' , 'bad' , done=1 , failed=1 , rc=1 )
