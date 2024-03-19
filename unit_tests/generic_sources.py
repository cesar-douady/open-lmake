# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,SourceRule

	lmake.manifest = ('Lmakefile.py',)

	class XSrc(SourceRule) :
		target = '{File:.*}.src'

	class Cpy(Rule) :
		target = '{File:.*}.cpy'
		dep    = '{File}'
		cmd    = 'cat'

	class Test(Rule) :
		target       = 'test'
		side_targets = { 'SUB' : ( '{*:.*}.src' , 'source_ok','incremental' ) }
		cmd = '''
			echo sub > sub.src
			cat sub.src.cpy
		'''

else :

	import ut

	ut.lmake( 'test' , may_rerun=1 , done=2 )
