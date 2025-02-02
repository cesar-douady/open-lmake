# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ( 'Lmakefile.py',)

	class Re(Rule) :
		stems = {
			'Dir'  : r'(.*/)?'
		,	'File' : r'(([^+/])*)'
		,	'Opt'  : r'([^+]*)'
		}
		targets = {
			'T1' :   '{Dir}{File}+{Opt}.t1'
		,	'T2' : ( '{Dir}{File}+{Opt}.t2' , 'incremental' )
		}
		cmd = 'echo >{T1} ; echo >>{T2}'

else :

	import os

	import ut

	ut.lmake( 'd.dir/f+o.t1' , done=1 )
	os.unlink('d.dir/f+o.t1')
	ut.lmake( 'd.dir/f+o.t1' , done=1 )
