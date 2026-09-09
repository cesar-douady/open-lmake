# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

# a manually modified target of a frozen job must not be quarantined
# the frozen job adopts what is on disk, but if the file is checked (goal Dsk) before the frozen job is run, it is seen as manual and quarantined

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'src'
	)

	class Opt(Rule) :
		targets = {
			'STATIC' : 'static_dep'
		,	'STAR'   : 'star_dep.{*:.*}'
		}
		cmd = "echo static >{STATIC} ; echo star > {STAR('top')}"

	class Dut(Rule) :
		target = 'dut'
		deps = {
			'SRC' : 'src'          # a dep that changes, giving a run reason before TOP is analyzed
		,	'TOP' : 'star_dep.top'
		}
		cmd = 'cat {SRC} {TOP}'

else :

	import subprocess as sp

	import ut

	print(0,file=open('src','w'))

	ut.lmake( 'dut' , 'static_dep' , new=1 , done=2 )

	print('manual',file=open('star_dep.top','w'))           # user tweaks a target by hand
	sp.run( ('lmark','-f','-a','static_dep') , check=True ) # and freezes the job that produced it
	print(1,file=open('src','w'))

	ut.lmake( 'dut' , changed=1 , changed_frozen=1 , done=1 ) # star_dep.top must be kept (not quarantined)
