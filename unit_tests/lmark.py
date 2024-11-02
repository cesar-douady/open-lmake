# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'src'
	)

	class Cpy(Rule) :
		target = r'{File:.*}.cpy'
		dep    = '{File}'
		cmd    = 'cat'

	class Cpy2(Rule) :
		target = r'{File:.*}.cpy2'
		dep    = '{File}.cpy'
		cmd    = 'cat'

else :

	import subprocess as sp

	import ut

	print(0,file=open('src','w'))

	# freeze
	None                                              ; None                              ; ut.lmake( 'test.cpy' ,                                 rc=1 ) # check no rule
	sp.run(('lmark','-f','-a','test'    ),check=True) ; None                              ; ut.lmake( 'test.cpy' , missing=1 ,                     rc=1 ) # check missing dep
	None                                              ; print(1,file=open('test','w'))    ; ut.lmake( 'test.cpy' , new    =1 , done=1 ,            rc=0 ) # check ok
	sp.run(('lmark','-f','-d','test'    ),check=True) ; None                              ; ut.lmake( 'test.cpy' ,                      unlink=1 , rc=1 ) # check no_rule unlink
	sp.run(('lmark','-f','-a','test'    ),check=True) ; None                              ; ut.lmake( 'test.cpy' ,             done=1 ,            rc=0 ) # check test.cpy is regenerated
	None                                              ; print(2,file=open('test','w'))    ; ut.lmake( 'test.cpy' , changed=1 , done=1 ,            rc=0 ) # check rebuild
	sp.run(('lmark','-f','-a','test.cpy'),check=True) ; print(3,file=open('test','w'))    ; ut.lmake( 'test.cpy' , frozen =1 ,                     rc=0 ) # check frozen

	None                                              ; None                              ; ut.lmake( 'src.cpy2' , new           =1 , done=2 , rc=0 )
	sp.run(('lmark','-f','-a','src.cpy' ),check=True) ; print(4,file=open('src.cpy','w')) ; ut.lmake( 'src.cpy2' , changed_frozen=1 , done=1 , rc=0 )
	sp.run(('lmark','-f','-c'           ),check=True) ; None                              ; ut.lmake( 'src.cpy2' ,                    done=2 , rc=0 )

	# no-trigger
	None                                         ; print(5,file=open('src'    ,'w')) ; ut.lmake( 'src.cpy' , changed=1 , done=1 ) # check out of date
	None                                         ; print(6,file=open('src'    ,'w')) ; ut.lmake( 'src.cpy' , changed=1 , done=1 ) # check up to date
	sp.run(('lmark','-t','-a','src'),check=True) ; None                              ; ut.lmake( 'src.cpy'                      ) # check up to date
	None                                         ; print(7,file=open('src'    ,'w')) ; ut.lmake( 'src.cpy' , changed=1          ) # check up to date despite src modified
	sp.run(('lmark','-t','-d','src'),check=True) ; None                              ; ut.lmake( 'src.cpy' ,             done=1 ) # check out of date now that src is no more no-trigger

	# manual
	None                              ; ut.lmake( 'src.cpy2' ,             done=1        ) # check src.cpy up to date
	print(0,file=open('src.cpy','w')) ; ut.lmake( 'src.cpy2'                             ) # check up to date, therefore no check
	print(8,file=open('src'    ,'w')) ; ut.lmake( 'src.cpy2' , changed=1 , done=2 , rc=0 ) # check out of date, therefore manual (quarantined)
	print(0,file=open('src.cpy','w')) ; ut.lmake( 'src.cpy2'                             ) # check up to date
