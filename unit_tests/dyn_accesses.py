# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import lmake

if __name__!='__main__' :

	import os

	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'one'
	,	'two'
	,	'step.py'
	)

	from step import step

	class Deps(Rule) :
		target = 'deps'
		def deps() :
			if step==1 :
				open('hello')
			return {}
		def cmd() : pass

	class Resources(Rule) :
		target    = r'resources'
		deps      = { 'ONE' : 'one' }
		if step==1 : resources = { 'cpu' : "{open('two').read().strip()}" }
		else       : resources = { 'cpu' : "{open(ONE  ).read().strip()}" }
		cmd       = 'echo {cpu}'

else :

	import ut

	print(1       ,file=open('one','w'))
	print(2       ,file=open('two','w'))

	print('step=1',file=open('step.py','w'))
	ut.lmake('deps','resources',new=2,no_deps=1,rerun=1,steady=1,rc=1) # resources can have dynamic deps

	print('step=2',file=open('step.py','w'))
	ut.lmake('deps','resources',done=1)
