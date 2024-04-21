# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = ('Lmakefile.py',)

	class Base(PyRule) :
		stems = {
			'W'    : r'(\.w)?'
		,	'Wait' : r'\d+'
		,	'Any'  : r'.*'
		}

	class Star(Base) :
		targets = { 'DST' : 'out{W}.{Wait}/{Any*}' }
		def cmd():
			import time
			import os
			time.sleep(int(Wait))
			dir = f'out{Wait}'
			for i in (1,2) :
				if W                     : open     (DST(f'mrkr{i}'),'w').write('bad')
				try                      : os.unlink(DST(f'mrkr{i}'))
				except FileNotFoundError : pass
			print('a_file',file=open(DST('a_file'),'w'))

	class Mrkr(Base) :
		targets = { 'MRKR' : r'{Any}/mrkr{Wait}' } # cannot use target as we want to wait before creating MRKR
		def cmd() :
			import os.path as osp
			import time
			time.sleep(int(Wait))
			print(osp.basename(MRKR),file=open(MRKR,'w'))

	class Res1(Base) :     # check case where mrkr has been unlinked before having been created
		target = 'res1{W}'
		def cmd():
			print(open(f'out{W}.2/mrkr1' ).read().strip())
			print(open(f'out{W}.2/a_file').read().strip())

	class Res2(Base) :     # check case where mrkr has been unlinked after having been created
		target = 'res2{W}'
		def cmd():
			print(open(f'out{W}.1/mrkr2' ).read().strip())
			print(open(f'out{W}.1/a_file').read().strip())

	class Chk(Base) :
		target = 'chk{W}'
		deps = {
			'RES1' : 'res1{W}'
		,	'RES2' : 'res2{W}'
		}
		def cmd() :
			assert open(RES1).read().split()==['mrkr1','a_file']
			assert open(RES2).read().split()==['mrkr2','a_file']

else :

	import ut

	ut.lmake( 'chk'   , new=1 , may_rerun=2 , done=5 ,           was_failed=1 ,           rc=1 )
	ut.lmake( 'chk'   ,                       done=3                                           ) # finish job
	ut.lmake( 'chk.w' , new=0 , may_rerun=2 , done=4 , rerun=1 , was_failed=1 , steady=1 ,rc=1 )
	ut.lmake( 'chk.w' ,                       done=3                                           ) # ensure up to date
