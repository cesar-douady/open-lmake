# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import lmake

if __name__!='__main__' :

	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'src'
	)

	def get_rsrc() :
		return open(Rsrc).read().strip()

	class Hide(Rule) :
		target = r'{Rsrc:.*}-hide{D:\d}'
		dep    = '{Rsrc}'
		cmd    = 'cat'

	class DutRsrcs(Rule) :
		targets   = { 'TGT' : r'rsrcs_{Rsrc:.*}_{Sfx:.*}' }
		resources = { 'cpu' : get_rsrc                    }
		cmd = '''
			[ {Sfx} = ko   ] && exit 1
			[ {Sfx} = read ] && cat {Rsrc}
			>{TGT}
		'''

	class DutEnv(Rule) :
		targets           = { 'TGT' : r'env_{Rsrc:.*}_{Sfx:.*}' }
		environ_resources = { 'CPU' : get_rsrc                  }
		cmd = '''
			[ {Sfx} = ko   ] && exit 1
			[ {Sfx} = read ] && cat {Rsrc}
			>{TGT}
		'''

else :

	import ut

	print(1,file=open('src','w'))
	cnt = ut.lmake( 'rsrcs_src_ok'       , 'rsrcs_src_read'       , 'rsrcs_src_ko'       , new=1 , done=2 ,                   failed=1 , rc=1 )
	cnt = ut.lmake( 'env_src_ok'         , 'env_src_read'         , 'env_src_ko'         ,         done=2 , early_rerun=... , failed=1 , rc=1 ) ; assert 1 <= cnt.early_rerun <= 3
	cnt = ut.lmake( 'rsrcs_src-hide1_ok' , 'rsrcs_src-hide1_read' , 'rsrcs_src-hide1_ko' , new=1 , done=3 , early_rerun=... , failed=1 , rc=1 ) ; assert 1 <= cnt.early_rerun <= 3
	cnt = ut.lmake( 'env_src-hide2_ok'   , 'env_src-hide2_read'   , 'env_src-hide2_ko'   ,         done=3 , early_rerun=... , failed=1 , rc=1 ) ; assert 1 <= cnt.early_rerun <= 3

	print(2,file=open('src','w'))
	# check ok is not remade, but read is remade because it read cpu in job
	ut.lmake( 'rsrcs_src_ok'       , 'rsrcs_src_read'       , 'rsrcs_src_ko'       , changed=1 , steady=1 , failed=1 , rc=1 )
	ut.lmake( 'env_src_ok'         , 'env_src_read'         , 'env_src_ko'         ,             steady=1 , failed=1 , rc=1 )
	ut.lmake( 'rsrcs_src-hide1_ok' , 'rsrcs_src-hide1_read' , 'rsrcs_src-hide1_ko' , done=1    , steady=1 , failed=1 , rc=1 )
	ut.lmake( 'env_src-hide2_ok'   , 'env_src-hide2_read'   , 'env_src-hide2_ko'   , done=1    , steady=1 , failed=1 , rc=1 )
