# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.config.backends.local.environ = { 'FROM_BACKEND':'from_backend' }

	lmake.manifest = ('Lmakefile.py',)

	class Dut(Rule) :
		target  = 'dut.ok'
		environ = {
			'FROM_BACKEND' : ...
		,	'FROM_RULE'    : 'from_rule'
		}
		cmd = '''
			[ "$FROM_BACKEND" = from_backend ] || echo bad '$FROM_BACKEND' : $FROM_BACKEND != from_backend >&2
			[ "$FROM_RULE"    = from_rule    ] || echo bad '$FROM_RULE'    : $FROM_RULE    != from_rule    >&2
		'''

else :

	import ut

	ut.lmake( 'dut.ok' , done=1 )
