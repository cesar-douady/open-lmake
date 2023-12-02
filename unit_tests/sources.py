# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import os.path as osp

	import lmake
	from lmake.rules import Rule,SourceRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'../srcs_rel/'
	,	osp.abspath('../srcs_abs')+'/'
	)

	class Src(SourceRule) :
		target = '{File:.*}.src'

	class CpyLocal(Rule) :
		target = '{File:.*}.cpy'
		dep    = '{File}.src'
		cmd    = 'cat - {File}2.src'

	class CpyExtRel(Rule) :
		target = '{File:.*}.cpy'
		dep    = '../srcs_rel/{File}.src'
		cmd    = 'cat - ../srcs_rel/{File}2.src'

	class CpyExtAbs(Rule) :
		target = '{File:.*}.cpy'
		dep    = f"{osp.abspath('../srcs_abs')}/{{File}}.src"
		cmd    = f"cat - {osp.abspath('../srcs_abs')}/{{File}}2.src"

	class Dflt(Rule) :
		prio   = -1
		target = '{File:.*}.cpy'
		cmd    = 'echo dflt'

else :

	import os

	import ut

	os.makedirs('sub/LMAKE',exist_ok=True)
	os.makedirs('srcs_rel' ,exist_ok=True)
	os.makedirs('srcs_abs' ,exist_ok=True)
	print(open('Lmakefile.py').read(),file=open('sub/Lmakefile.py','w'))

	for s in ('','2') :
		print(f'local{s}'  ,file=open(f'sub/local{s}.src'       ,'w'))
		print(f'ext_rel{s}',file=open(f'srcs_rel/ext_rel{s}.src','w'))
		print(f'ext_abs{s}',file=open(f'srcs_abs/ext_abs{s}.src','w'))

	os.chdir('sub')

	ut.lmake( 'local.cpy' , 'ext_rel.cpy' , 'ext_abs.cpy' , new=6 , no_file=0 , done=3          )
	os.unlink('local2.src'              )
	os.unlink('../srcs_rel/ext_rel2.src')
	os.unlink('../srcs_abs/ext_abs2.src')
	ut.lmake( 'local.cpy' , 'ext_rel.cpy' , 'ext_abs.cpy' ,         no_file=3 , failed=3 , rc=1 )
	os.unlink('local.src'              )
	os.unlink('../srcs_rel/ext_rel.src')
	os.unlink('../srcs_abs/ext_abs.src')
	ut.lmake( 'local.cpy' , 'ext_rel.cpy' , 'ext_abs.cpy' ,         no_file=3 , done=3          )
