# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	class Good1(Rule) :
		check_abs_paths = True
		targets         = { 'REG' : 'good1' , 'LNK' : 'lnk1' }
		cmd             = 'echo . >{REG} ; ln -s no_target {LNK}'

	class Good2(Rule) :
		check_abs_paths = True
		targets         = { 'REG' : 'good2' , 'LNK' : 'lnk2' }
		repo_view       = '/repo'
		cmd             = 'pwd'
		cmd             = 'pwd >{REG} ; ln -s no_target {LNK}'

	class Bad(Rule) :
		check_abs_paths = True
		target          = 'bad'
		targets         = { 'REG' : 'bad' , 'LNK' : 'lnk3' }
		cmd             = 'pwd >{REG} ; ln -s no_target {LNK}'

else :

	import ut

	ut.lmake( 'good1' , 'good2' , 'bad' , done=2 , failed=1 , rc=1 )
