# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	)

	from step import step

	class Base(Rule) :
		targets      = { 'A' : 'base_target.{File*:.*}' }
		side_targets = { 'B' : 'base_side_target.x'     }
		side_deps    = { 'E' : 'base_side_dep.x'        }
		if step==2 :
			side_deps['E2'] = 'base_side_dep.x2'

	class Derived(Base) :
		targets      = { 'C' : 'base_side_target.{File*:.*}' } # becomes a target
		side_targets = { 'D' : 'base_target.x'               } # stay a target
		cmd = '>base_side_target.x ; >base_target.x'

	class Test(Rule) :
		target = 'test'
		deps = {
			'B' : 'base_side_target.x' # available as it has become a target in Derived
		,	'D' : 'base_target.x'      # available as it stays a target in Base although declared side_target in Derived
		}
		cmd = ''

	if step==1 :
		class Dummy(Rule) :
			target = 'base_side_target.x'
			dep    = 'foo'
			cmd    = ''

else :

	import ut

	print('step=1',file=open('step.py','w'))
	ut.lmake( 'test' , done=2 )

	print('step=2',file=open('step.py','w'))
	ut.lmake( 'test' , steady=1 )
