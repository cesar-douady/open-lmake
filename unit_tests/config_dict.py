# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,ConfigRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'config.py'
	,	'dut_a.ref'
	,	'dut_a.b.ref'
	,	'dut_c.ref'
	,	'dut_c.0.ref'
	,	'dut_c.1.ref'
	)

	class MyConfigRule(ConfigRule) : pass

	class Dut(Rule) :
		target = 'dut_{File:.*}.val'
		def cmd() :
			v = lmake.config_dict('config')
			for k in File.split('.') : v = v[k]
			print(v())

	class Chk(Rule) :
		target = '{File:.*}.ok'
		deps = {
			'DUT' : '{File}.val'
		,	'REF' : '{File}.ref'
		}
		cmd = 'diff -w {REF} {DUT} >&2'

else :

	import os
	import os.path as osp

	import ut

	print( "config = {'a':{'b':1},'c':[2,[3]]}" , file=open('config.py'  ,'w') )
	print(               "{'b':1}"              , file=open('dut_a.ref'  ,'w') )
	print(                    "1"               , file=open('dut_a.b.ref','w') )
	print(                           "[2,[3]]"  , file=open('dut_c.ref'  ,'w') )
	print(                            "2"       , file=open('dut_c.0.ref','w') )
	print(                              "[3]"   , file=open('dut_c.1.ref','w') )

	cnt = ut.lmake( 'dut_a.ok' , 'dut_a.b.ok' , 'dut_c.ok' , 'dut_c.0.ok' , 'dut_c.1.ok' , new=7 , may_rerun=... , rerun=... , done=11 )
	assert 1 <= cnt.may_rerun+cnt.rerun <= 5                                                                                             # timing dependent

	print( "config = {'a':{'b':1},'c':[2,[4]]}" , file=open('config.py'  ,'w') )
	print(                           "[2,[4]]"  , file=open('dut_c.ref'  ,'w') )
	print(                              "[4]"   , file=open('dut_c.1.ref','w') )
	ut.lmake( 'dut_a.ok' , 'dut_a.b.ok' , 'dut_c.ok' , 'dut_c.0.ok' , 'dut_c.1.ok' , changed=3 , done=3 , steady=2 ) # only jobs actually reading config.c.1.0 are rerun
