# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	class Dut(Rule) :
		target = 'dut/{File:.*}'
		cmd    = ''

	class Cpy(Rule) :
		target = '{File:.*}.cpy'
		dep    = '{File}'
		cmd     = 'cat'

else :

	import os
	import subprocess as sp

	import ut

	None                                                                      ; None                    ; ut.lmake( 'marked.cpy' ,                      rc=1 )
	sp.run( ('lmark','-af','marked') , check=True , universal_newlines=True ) ; open     ('marked','w') ; ut.lmake( 'marked.cpy' , new=1 , done    =1        )
	sp.run( ('lmark','-cf'         ) , check=True , universal_newlines=True ) ; os.unlink('marked'    ) ; ut.lmake( 'marked.cpy' ,         unlinked=1 , rc=1 )

	None                                                                   ; ut.lmake( 'dut/a' , done    =1        )
	sp.run( ('lmark','-af','dut') , check=True , universal_newlines=True ) ; ut.lmake( 'dut/a' , unlinked=1 , rc=1 )
	sp.run( ('lmark','-cf'      ) , check=True , universal_newlines=True ) ; ut.lmake( 'dut/a' , done    =1        ) # check dut/a recovers its buildable state
