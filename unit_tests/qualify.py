# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

bad_target_dep = (
	( '.'                       , 'a'                       )
,	( '/'                       , 'a'                       )
,	( '/{File}'                 , 'a'                       )
,	( './{File}'                , 'a'                       )
,	( '../{File}'               , 'a'                       )
,	( '{File}/'                 , 'a'                       )
,	( '{File}/.'                , 'a'                       )
,	( '{File}/..'               , 'a'                       )
,	( '{File}//{File}'          , 'a'                       )
,	( '{File}/./{File}'         , 'a'                       )
,	( '{File}/../{File}'        , 'a'                       )
,	( '{File}//.././..//{File}' , 'a'                       )
,	( '{File}'                  , '/{File}'                 )
,	( '.{File}'                 , './{File}'                )
,	( '..{File}'                , '../{File}'               )
,	( '{File}'                  , '{File}/'                 )
,	( '{File}.'                 , '{File}/.'                )
,	( '{File}..'                , '{File}/..'               )
,	( '{File}{File}'            , '{File}//{File}'          )
,	( '{File}.{File}'           , '{File}/./{File}'         )
,	( '{File}..{File}'          , '{File}/../{File}'        )
,	( '{File}../.{File}'        , '{File}//.././..//{File}' )
)

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	from step import step

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	)

	if step<len(bad_target_dep) :
		class DutConfig(Rule) :
			stems  = { 'File' : r'.*' }
			target = bad_target_dep[step][0]
			dep    = bad_target_dep[step][1]
			cmd    = 'cat'

	class DutExe1(Rule) :
		stems  = { 'File' : r'.*' }
		target = '1{File}1'
		dep    = '{File}'
		cmd    = 'cat'

	class DutExe2(Rule) :
		stems  = { 'File1' : r'.*' , 'File2' : r'.*' }
		target = '2{File1}2{File2}2'
		dep    = '{File1}{File2}'
		cmd    = 'cat'

else :

	import ut

	for s in range(len(bad_target_dep)) :
		print(f'step={s}',file=open('step.py','w'))
		ut.lmake( rc=4) # check Lmakefile cannot be read

	print(f'step={len(bad_target_dep)}',file=open('step.py','w'))

	ut.lmake( '11'        , bad_dep=1 , rc=1 )
	ut.lmake( '1.1'       , bad_dep=1 , rc=1 )
	ut.lmake( '1/1'       , bad_dep=1 , rc=1 )
	ut.lmake( '1/x1'      , bad_dep=1 , rc=1 )
	ut.lmake( '1./x1'     , bad_dep=1 , rc=1 )
	ut.lmake( '1../x1'    , bad_dep=1 , rc=1 )
	ut.lmake( '1x/1'      , bad_dep=1 , rc=1 )
	ut.lmake( '1x/.1'     , bad_dep=1 , rc=1 )
	ut.lmake( '1x/..1'    , bad_dep=1 , rc=1 )
	ut.lmake( '2x/2/y2'   , bad_dep=1 , rc=1 )
	ut.lmake( '2x/.2/y2'  , bad_dep=1 , rc=1 )
	ut.lmake( '2x/..2/y2' , bad_dep=1 , rc=1 )
