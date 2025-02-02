# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import os.path as osp

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'../srcs_rel/'
	,	osp.abspath('../srcs_abs')+'/'
	,	'local/'
	)

	class CpyLocal(Rule) :
		target = r'{File:.*}.cpy'
		dep    = 'local/{File}.src'
		cmd    = 'cat - local/{File}2.src'

	class CpyExtRel(Rule) :
		target = r'{File:.*}.cpy'
		dep    = '../srcs_rel/{File}.src'
		cmd    = 'cat ../srcs_rel/../not_a_src - ../srcs_rel/{File}2.src'

	class CpyExtAbs(Rule) :
		target = r'{File:.*}.cpy'
		dep    = f"{osp.abspath('../srcs_abs')}/{{File}}.src"
		cmd    = f"cat {osp.abspath('../srcs_abs')}/../not_a_src - {osp.abspath('../srcs_abs')}/{{File}}2.src"

	class Dflt(Rule) :
		prio   = -1
		target = r'{File:.*}.cpy'
		cmd    = 'echo dflt'

else :

	import os

	import ut

	os.makedirs('sub/LMAKE',exist_ok=True)
	os.makedirs('srcs_rel' ,exist_ok=True)
	os.makedirs('srcs_abs' ,exist_ok=True)
	os.makedirs('sub/local',exist_ok=True)
	print(open('Lmakefile.py').read(),file=open('sub/Lmakefile.py','w'))

	print(file=open('not_a_src','w'))
	for s in ('','2') :
		print(f'local{s}'  ,file=open(f'sub/local/local{s}.src' ,'w'))
		print(f'ext_rel{s}',file=open(f'srcs_rel/ext_rel{s}.src','w'))
		print(f'ext_abs{s}',file=open(f'srcs_abs/ext_abs{s}.src','w'))

	os.chdir('sub')

	ut.lmake( 'local.cpy' , 'ext_rel.cpy' , 'ext_abs.cpy' , new=6 , done  =3        )
	os.unlink('local/local2.src'        )
	os.unlink('../srcs_rel/ext_rel2.src')
	os.unlink('../srcs_abs/ext_abs2.src')
	ut.lmake( 'local.cpy' , 'ext_rel.cpy' , 'ext_abs.cpy' ,         failed=3 , rc=1 )
	os.unlink('local/local.src'        )
	os.unlink('../srcs_rel/ext_rel.src')
	os.unlink('../srcs_abs/ext_abs.src')
	ut.lmake( 'local.cpy' , 'ext_rel.cpy' , 'ext_abs.cpy' ,         done  =3        )
