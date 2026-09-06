# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'../shared/'   # external dir containing a plain source
	)

	class Plain(Rule) :
		target = 'plain'
		cmd    = 'cat ../shared/plain.txt'

else :

	print('denied file permission not yet implemented',file=open('skipped','w'))
	exit()

	import ut

	import os

	os.makedirs('shared'    ,exist_ok=True)
	os.makedirs('repo/LMAKE',exist_ok=True)
	os.symlink('../Lmakefile.py','repo/Lmakefile.py')

	print('unreadable',file=open('shared/plain.txt','w'))
	os.chmod('shared/plain.txt',0o000)                    # pretend the table was filled by another user not giving access to us

	os.chdir('repo')

	ut.lmake( 'plain' , rc=1 , new=1 , failed=1 )
