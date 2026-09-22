# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import SourceRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'src_dir/a'
	)

	class Src(SourceRule) :
		target = '{File:.*}'

else :

	import os

	import ut

	os.makedirs( 'src_dir' , exist_ok=True )
	print('a',file=open('src_dir/a','w'))

	ut.lmake( 'src_dir' , rc=1 )
