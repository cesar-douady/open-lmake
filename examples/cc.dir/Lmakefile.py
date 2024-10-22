# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

# This files supposes that you have understood what has been explained in the hello_world example.
# It proposes a simple flow for a simple C project, with compilation, link, execution and test.

import lmake

from lmake.rules import Rule,DirRule

# In a real world, your sources would be controlled by git (only source version control supported so far)
# and this list would not be necessary as lmake calls git by default to establish the list of sources.
lmake.manifest = (
	'Lmakefile.py'
,	'hello.c'
,	'hello.h'
,	'world.c'
,	'world.h'
,	'hello_world.c'
,	'hello_world.olst'
,	'smith.in'
,	'world.in'
,	'hello_world-smith.ref'
,	'hello_world-world.ref'
,	'hello_world.tlst'
)

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
class NoComment(Base) :
	target = '{File}.nc'
	dep    = '{File}'
	cmd    = r"sed -e 's: *#.*::' -e '/^$/d'" # stdin is the (unique) dep and stdout is the (unique) target

# By default, PATH contains lmake's bin directory, so all utilities are immediately accessible
class Compile(Base) :
	targets = { 'OBJ':'{File}.o' }
	deps    = { 'SRC':'{File}.c' }
	# lrun_cc runs gcc (its 1st arg), and in addition set dependencies on foo_dir/... as soon as foo_dir appears in an include/library path with -I and the like
	cmd     = 'lrun_cc gcc -c -std=c99 -o{OBJ} {SRC}'

# Linking is based on an "olst" to provide the list of objects to link in.
# Doing so, rather than setting the list here allows a complete decoupling between flow and project.
# Multiple projects can be controlled with the same Lmakefile.py
# Note the .exe suffix here. Without it, there is an infinite recursion :
# if a file foo is needed, lmake looks for foo.olst.nc, which can be built from foo.olst.nc.olst.nc, etc.
# this can be avoided by setting an AntiRule, but this is not the purpose of this example (keep it simple)
class Link(Base) :
	targets = { 'EXE':'{Exe}.exe'     }
	deps    = { 'LST':'{Exe}.olst.nc' }         # note the usage of .nc to filter out comments
	cmd     = r'''
		sed 's:.*:\0.o:' {LST} >$TMPDIR/olst    # $TMPDIR is guaranteed empty at job startup time
		echo link objects : ; cat $TMPDIR/olst
		lrun_cc gcc -o{EXE} $(cat $TMPDIR/olst) # and there is no need to clean it up, this is done automatically by lmake
	'''

# Note the usage of named stems.
# The naming convention used for test results is immediately apparent.
class Run(Base) :
	target = r'{Exe}-{Test}.out'
	deps   = {
		'EXE' : '{Exe}.exe'
	,	'IN'  : '{Test}.in.nc' # note the usage of .nc to filter out comments
	}
	cmd = './{EXE} "$(cat {IN})"'

class Chk(Base) :
	target = '{Exe}-{Test}.ok'
	deps = {
		'DUT' : '{Exe}-{Test}.out'
	,	'REF' : '{Exe}-{Test}.ref.nc' # note the usage of .nc to filter out comments
	}
	cmd = 'diff {REF} {DUT}>&2'

# Same principle for regression lists as for linking.
# The project defines a list, the flow handles it.
# Note the ambiguity with Chk as both targets ends with .ok.
# Lmake will very easily handle that by looking at what deps are buildable.
class Regr(Base) :
	target = '{Exe}.ok'
	dep    = '{Exe}.tlst.nc'               # note the usage of .nc to filter out comments
	cmd = r'''
		ldepend $(sed 's:.*:{Exe}-\0.ok:') # ldepend sets dependencies, so the regression is ok as soon as all tests are ok
	'''
