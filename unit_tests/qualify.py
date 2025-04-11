# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

bad_target_dep = {
	'' : (
		{ 'tgt':'.'                       , 'dep':'x'                       , 'ok':False }
	,	{ 'tgt':'/'                       , 'dep':'x'                       , 'ok':False }
	,	{ 'tgt':'/{File}'                 , 'dep':'x'                       , 'ok':False }
	,	{ 'tgt':'./{File}'                , 'dep':'x'                       , 'ok':False }
	,	{ 'tgt':'../{File}'               , 'dep':'x'                       , 'ok':False }
	,	{ 'tgt':'{File}/'                 , 'dep':'x'                       , 'ok':False }
	,	{ 'tgt':'{File}/.'                , 'dep':'x'                       , 'ok':False }
	,	{ 'tgt':'{File}/..'               , 'dep':'x'                       , 'ok':False }
	,	{ 'tgt':'{File}//{File}'          , 'dep':'x'                       , 'ok':False }
	,	{ 'tgt':'{File}/./{File}'         , 'dep':'x'                       , 'ok':False }
	,	{ 'tgt':'{File}/../{File}'        , 'dep':'x'                       , 'ok':False }
	,	{ 'tgt':'{File}//.././..//{File}' , 'dep':'x'                       , 'ok':False }
	,	{ 'tgt':'{File}'                  , 'dep':'/{File}'                 , 'ok':False }
	,	{ 'tgt':'.{File}'                 , 'dep':'./{File}'                , 'ok':False }
	,	{ 'tgt':'..{File}'                , 'dep':'../{File}'               , 'ok':False }
	,	{ 'tgt':'{File}'                  , 'dep':'{File}/'                 , 'ok':False }
	,	{ 'tgt':'{File}.'                 , 'dep':'{File}/.'                , 'ok':False }
	,	{ 'tgt':'{File}..'                , 'dep':'{File}/..'               , 'ok':False }
	,	{ 'tgt':'{File}{File}'            , 'dep':'{File}//{File}'          , 'ok':False }
	,	{ 'tgt':'{File}.{File}'           , 'dep':'{File}/./{File}'         , 'ok':False }
	,	{ 'tgt':'{File}..{File}'          , 'dep':'{File}/../{File}'        , 'ok':False }
	,	{ 'tgt':'{File}../.{File}'        , 'dep':'{File}//.././..//{File}' , 'ok':False }
	)
,	'..' : (
		{ 'tgt':'../{File}'     , 'dep':'x'             , 'ok':False }
	,	{ 'tgt':'../x{File}'    , 'dep':'x'             , 'ok':False }
	,	{ 'tgt':'../../{File}'  , 'dep':'x'             , 'ok':False }
	,	{ 'tgt':'..//../{File}' , 'dep':'x'             , 'ok':False }
	,	{ 'tgt':'..{File}'      , 'dep':'../{File}'     , 'ok':True  }
	,	{ 'tgt':'..x{File}'     , 'dep':'../x{File}'    , 'ok':True  }
	,	{ 'tgt':'....{File}'    , 'dep':'../../{File}'  , 'ok':False }
	,	{ 'tgt':'....{File}'    , 'dep':'..//../{File}' , 'ok':False }
	)
,	'/' : (
		{ 'tgt':'x' , 'dep':'y' , 'ok':False }
	,)
,	'/usr' : (
		{ 'tgt':'x' , 'dep':'y' , 'ok':False }
	,)
,	'../base' : (
		{ 'tgt':'..{File}'   , 'dep':'../{File}'     , 'ok':True  }
	,	{ 'tgt':'..x{File}'  , 'dep':'../x{File}'    , 'ok':False }
	,	{ 'tgt':'..x{File}'  , 'dep':'../base{File}' , 'ok':True  }
	,	{ 'tgt':'....{File}' , 'dep':'../../{File}'  , 'ok':False }
	,	{ 'tgt':'....{File}' , 'dep':'..//../{File}' , 'ok':False }
	)
,	'/tmp' : (
		{ 'tgt':'..{File}'   , 'dep':'../{File}'     , 'ok':False }
	,	{ 'tgt':'..x{File}'  , 'dep':'../x{File}'    , 'ok':False }
	,	{ 'tgt':'....{File}' , 'dep':'../../{File}'  , 'ok':False }
	,	{ 'tgt':'....{File}' , 'dep':'..//../{File}' , 'ok':False }
	,	{ 'tgt':'..x{File}'  , 'dep':'/x{File}'      , 'ok':False }
	,	{ 'tgt':'..x{File}'  , 'dep':'/{File}'       , 'ok':True  }
	,	{ 'tgt':'..x{File}'  , 'dep':'/tmp{File}'    , 'ok':True  }
	)
}

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	import step

	import sys

	lmake.manifest = [
		'Lmakefile.py'
	,	'step.py'
	]

	if step.bad :
		print('bad',step.src_dir,step.step,file=sys.stderr)
		if step.src_dir :
			if step.src_dir[-1]=='/' : lmake.manifest.append(step.src_dir    )
			else                     : lmake.manifest.append(step.src_dir+'/')
		class DutConfig(Rule) :
			stems  = { 'File' : r'.*' }
			target = bad_target_dep[step.src_dir][step.step]['tgt']
			dep    = bad_target_dep[step.src_dir][step.step]['dep']
			cmd    = 'cat'
	else :
		print('ok',file=sys.stderr)

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

	import os

	import ut

	for src_dir,lst in bad_target_dep.items() :
		os.system('rm -rf LMAKE')
		for s in range(len(lst)) :
			print(f'bad=True ; src_dir={src_dir!r} ; step={s}',file=open('step.py','w'))
			ut.lmake( rc = 0 if lst[s]['ok'] else 4 )

	os.system('rm -rf LMAKE')
	print(f'bad=False',file=open('step.py','w'))

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
