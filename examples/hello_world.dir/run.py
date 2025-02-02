#!/bin/python3
# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

##########################################################################
# this file is meant to be visualized with 200 columns and tabstop as 4. #
##########################################################################

try :
	# ut contains the unit test helper functions used during the lmake build process
	from ut import lmake
except :
	# replace with simple function for interactive experiment by user (non check)
	import subprocess as sp
	def lmake(*cmd_line,**kwds) :
		sp.run( ('lmake',)+cmd_line , stdin=None )

lmake( 'hello-world-sh' , 'hello-world-py' , done=2 , new=2 )     # check targets are out of date : 2 new sources (hello and world), 2 new targets
lmake( 'hello-world-sh' , 'hello-world-py' , done=0 , new=0 )     # check targets are up to date : nothing has changed, nothing is remade
#
lmake( 'world-world-sh' , done=1 )                                # prepare for next call
#
print('hello again',file=open('hello','w'))                       # update sources
#
lmake( 'hello-world-sh' , 'world-world-sh' , done=1 , changed=1 ) # hello has changed, world-world-sh is unaffected
#
print('hello',file=open('hello','w'))                             # revert
#
# hello has changed :
# - hello-world-sh is rebuilt
# - hello-world-py was already built with the right deps (it was not built with the 2nd content)
# - world-world-sh is still unaffected,
lmake( 'hello-world-sh' , 'hello-world-py' , 'world-world-sh' , done=1 , changed=1 )
