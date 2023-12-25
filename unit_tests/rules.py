# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

n = 500

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = ('Lmakefile.py',)

	for r in range(n) :                                                     # ensure rules recording can handle a lot of rules
		class Sfx(Rule) :
			name = f'Sfx{r}'
			target = f'{{File:.*}}.{r}'
			cmd = ''
		class Pfx(Rule) :
			name = f'Pfx{r}'
			target = f'{r}.{{File:.*}}'
			cmd = ''

else :

	import ut

	ut.lmake()                                                                 # ensure no crash and reasonable time
