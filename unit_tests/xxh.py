# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__=='__main__' :

	import os
	import subprocess as sp

	import lmake

	crc_py = lmake.xxhsum_file('Lmakefile.py')
	crc_sh = sp.check_output(('xxhsum','Lmakefile.py'),universal_newlines=True).strip()

	assert crc_py==crc_sh
