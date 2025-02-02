# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

##########################################################################
# this file is meant to be visualized with 200 columns and tabstop as 4. #
##########################################################################

# This files supposes that you have understood what has been explained in the hello_world example.
# It proposes a simple flow for a simple C project, with compilation, link, execution and test.

import sys

import lmake

from lmake.rules import Rule,DirRule,PyRule

# This is to guarantee dir existence for those listed in -I (and the like) when calling gcc
# in this example, there are none, but in a more complex flow, there would.
# Here, we use the default ... marker
class Dir(DirRule) : pass

# It is practical to define the "vocabulary" once in a base class.
# A good practice is to define the stems in the most restrictive possible way.
# This way, in case of bug, they will tend to be "no matching rule" rather than "spuriously matching rule",
# which tend to be easier to debug.
class Base(Rule) :
	stems = {
		'File' : r'.+'
	,	'Exe'  : r'.+'
	,	'Test' : r'[^-]+'
	}

# A simple preprocessing to strip comments out.
# So, foo.nc is a version of foo without comments.
class NoComment(Base,PyRule) :
	target = '{File}.nc'                                            # single target gets the stdout of cmd
	dep    = '{File}'                                               # and single dep feeds cmd on stdin
	def cmd() :                                                     # when cmd is a function, it is called to run the job (remember it is transported for remote execution)
		import re
		text     = sys.stdin.read()                                 # sys is imported from the global environment
		filtered = re.sub('(^|\n)\n+',r'\1',re.sub(r'#.*','',text)) # strip comments and empty lines
		print(filtered,end='')

# By default, PATH contains lmake's bin directory, so all utilities are immediately accessible
class Compile(Base) :
	targets = { 'OBJ':'{File}.o' }
	deps    = { 'SRC':'{File}.c' }
	flags   = '-O0'                # this is a user attribute, lmake does nothing except providing it to cmd
	# lrun_cc runs gcc (its 1st arg), and in addition set dependencies on foo_dir/... as soon as foo_dir appears in an include/library path with -I and the like
	cmd     = 'lrun_cc gcc {flags} -c -std=c99 -o{OBJ} {SRC}'

# Linking is based on an "olst" to provide the list of objects to link in.
# Doing so, rather than setting the list here allows a complete decoupling between flow and project.
# Multiple projects can be controlled with the same Lmakefile.py
# Note the .exe suffix here. Without it, there is an infinite recursion :
# if a file foo is needed, lmake looks for foo.olst.nc, which can be built from foo.olst.nc.olst.nc, etc.
# this can be avoided by setting an AntiRule, but this is not the purpose of this example (keep it simple)
class Link(Base) :
	targets = { 'EXE' :  '{Exe}.exe'                 }
	deps    = { 'LST' : ('{Exe}.olst.nc','critical') } # critical means that if modified, job must be re-executed to discover new/old deps before re-executing current deps
	cmd     = r'''
		sed 's:.*:\0.o:' {LST} >$TMPDIR/olst           # $TMPDIR is guaranteed empty at job startup time
		echo link objects : ; cat $TMPDIR/olst
		lrun_cc gcc -o{EXE} $(cat $TMPDIR/olst)        # and there is no need to clean it up, this is done automatically by lmake
	'''

# Note the usage of named stems.
# The naming convention used for test results is immediately apparent.
class Run(Base) :
	target = r'{Exe}-{Test}.out'      # single target gets the stdout of cmd
	deps   = {
		'EXE' : '{Exe}.exe'
	,	'IN'  : '{Exe}-{Test}.scn.nc' # note the usage of .nc to filter out comments
	}
	cmd = './{EXE} "$(cat {IN})"'

class Chk(Base) :
	target = '{Exe}-{Test}.ok'        # single target gets the stdout of cmd
	deps = {
		'DUT' : '{Exe}-{Test}.out'
	,	'REF' : '{Exe}-{Test}.ref.nc' # note the usage of .nc to filter out comments
	}
	cmd = 'diff {REF} {DUT}>&2'

# Here we scatter all the tests from a single, simple, regression description
# Each line contains a test, a scenario and and expected output, separated with ':'
class Scenarios(Base) :
	targets = {
		'LST' : '{Exe}.tlst'
	,	'SCN' : '{Exe}-{Test*}.scn' # this is a star target : it means all tests are generated in a single job execution
	,	'REF' : '{Exe}-{Test*}.ref' # idem
	}
	dep = '{Exe}.regr.nc'
	def cmd() :
		with open(LST,'w') as lst :
			for l in sys.stdin.readlines() :
				test,scn,ref = (x.strip() for x in l.split(':'))
				print(scn ,file=open(SCN(test),'w'))
				print(ref ,file=open(REF(test),'w'))
				print(test,file=lst                )

# Same principle for regression lists as for linking.
# The project defines a list, the flow handles it.
# Note the ambiguity with Chk as both targets ends with .ok.
# Lmake will very easily handle that by looking at what deps are buildable.
class Regr(Base) :
	target =             '{Exe}.ok'                       # single target gets the stdout of cmd
	deps   = { 'LST' : ( '{Exe}.tlst.nc' , 'critical' ) } # critical means that if modified, job must be re-executed to discover new/old deps before re-executing current deps
	cmd = r'''
		ldepend $(sed 's:.*:{Exe}-\0.ok:' {LST})          # ldepend sets dependencies, so the regression is ok as soon as all tests are ok
	'''
