# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

# ut contains the unit test helper functions
# ut.lmake runs an lmake command and counts output lines per keyword
# e.g. done=2 means there are 2 lines with the done keyword
import ut

print('hello',file=open('hello','w'))                                # create sources
print('world',file=open('world','w'))                                # .
#
ut.lmake( 'hello-world-sh' , 'hello-world-py' , done=2 , new=2 )     # check targets are out of date : 2 new sources (hello and world), 2 new targets
ut.lmake( 'hello-world-sh' , 'hello-world-py' , done=0 , new=0 )     # check targets are up to date : nothing has changed, nothing is remade
#
ut.lmake( 'world-world-sh' , done=1 )                                # prepare for next call
#
print('hello again',file=open('hello','w'))                          # update sources
#
ut.lmake( 'hello-world-sh' , 'world-world-sh' , done=1 , changed=1 ) # hello has changed, world-world-sh is unaffected
#
print('hello',file=open('hello','w'))                                # revert
#
# hello has changed :
# - hello-world-sh is rebuilt
# - hello-world-py was already built with the right deps (it was not built with the 2nd content)
# - world-world-sh is still unaffected,
ut.lmake( 'hello-world-sh' , 'hello-world-py' , 'world-world-sh' , done=1 , changed=1 )
