# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import os.path as osp

	import lmake
	from lmake.rules import Rule

	from step import step

	lmake.manifest = [
		'Lmakefile.py'
	,	'step.py'
	]
	if step<=1 :
		lmake.manifest += [
			'../srcs_rel/'
		,	osp.abspath('../srcs_abs')+'/'
		,	'local/'
		]
	if step<=2 :
		lmake.manifest += [
			'src'
		]

	class CpyLocal(Rule) :
		target = r'{File:.*}.lcl'
		cmd    = 'cat local/{File}.src'

	class CpyExtRel(Rule) :
		target = r'{File:.*}.rel'
		cmd    = 'cat ../srcs_rel/{File}.src'

	class CpyExtAbs(Rule) :
		target = r'{File:.*}.abs'
		cmd    = f"cat {osp.abspath('../srcs_abs')}/{{File}}.src"

	class Cpy(Rule) :
		target = r'{File:.*}.cpy'
		cmd    = 'cat {File}'

else :

	import os
	import os.path as osp

	import ut

	os.makedirs('sub/LMAKE',exist_ok=True)
	os.makedirs('srcs_rel' ,exist_ok=True)
	os.makedirs('srcs_abs' ,exist_ok=True)
	os.makedirs('sub/local',exist_ok=True)
	print(open('Lmakefile.py').read(),file=open('sub/Lmakefile.py','w'))

	print(f'src'    ,file=open(f'sub/src'             ,'w'))
	print(f'local'  ,file=open(f'sub/local/local.src' ,'w'))
	print(f'ext_rel',file=open(f'srcs_rel/ext_rel.src','w'))
	print(f'ext_abs',file=open(f'srcs_abs/ext_abs.src','w'))

	os.chdir('sub')

	print('step=1',file=open('step.py','w'))
	ut.lmake( 'local.lcl' , 'ext_rel.rel' , 'ext_abs.abs' , new=3 , done=3 )

	print('step=2',file=open('step.py','w'))
	ut.lmake( 'local.lcl' , 'ext_rel.rel' , 'ext_abs.abs' , rc=4 )                                                     # cannot accept modification of src_dirs, 4==Rc::Format
	os.rename('LMAKE','LMAKE2')
	os.makedirs('LMAKE')
	ut.lmake( 'local.lcl' , 'ext_rel.rel' , 'ext_abs.abs' , quarantined=3 , dangling=1 , dep_error=1 , done=2 , rc=1 ) # local.lcl is dangling

	ut.lmake( 'src.cpy' , new=1 , done=1 )

	print('step=3',file=open('step.py','w'))

	ut.lmake( 'src.cpy' , dangling=1 , rc=1 )

	os.chdir('..')

	assert osp.isfile('srcs_rel/ext_rel.src') # file is now external
	assert osp.isfile('srcs_abs/ext_abs.src') # .
	assert osp.isfile('sub/local/local.src' ) # file is now dangling
