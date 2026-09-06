# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	lmake.user_environ.get('') # reading an empty-string env key must not crash the server

	class Dut(Rule) :
		target = 'dut'
		cmd    = 'echo hello'

else :

	import ut

	p = ut.lmake( wait=False , keep_stderr=True , rc=8 , no_dump=True ) # check Dut cannot be read
	p()

	assert 'cannot access' in p.stderr and 'empty environment variable' in p.stderr , p.stderr # check clean error message
