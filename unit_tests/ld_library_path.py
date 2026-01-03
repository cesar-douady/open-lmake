# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import lmake

if __name__!='__main__' :

	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	class Dut(Rule) :
		target    = r'dut.{Autodep:\w+}'
		environ   = { 'LD_LIBRARY_PATH':'a_dir' }
		autodep   = '{Autodep}'
		stderr_ok = True
		cmd       = 'hostname ; hostname'

else :

	import subprocess as sp

	import ut

	for ad in lmake.autodeps :
		ut.lmake( f'dut.{ad}' , done=1 )
		x = sp.run(('lshow','-dv',f'dut.{ad}'),stdout=sp.PIPE,universal_newlines=True).stdout
		assert 'a_dir/libc.so' in x,f'no libc.so in :\n{x}'
