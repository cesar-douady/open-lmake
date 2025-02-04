# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.config.backends.local.environ = { 'DUT':'dut' }

	lmake.manifest = ('Lmakefile.py',)

	class Dut(Rule) :
		target  = 'dut.ok'
		environ = { 'DUT':... }
		cmd = '''
			[ "$DUT" = dut ] || echo bad '$DUT :' "$DUT != dut" >&2
		'''

else :

	import ut

	ut.lmake( 'dut.ok' , done=1 )
