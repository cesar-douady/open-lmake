# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

##########################################################################
# this file is meant to be visualized with 200 columns and tabstop as 4. #
##########################################################################

import lmake # this is necessary in basically all Lmakefile.py's

# import base classes to define rules
# a class becomes a rule as soon as :
# - it inherits from one of the Rule base classes in lmake.rules
# - it defines a target (single target case) or targets (multi-targets case) attribute
# - it defines a cmd attribute (either a str or a function)
from lmake.rules import Rule,PyRule

# you must declare sources
# by default, lmake.manifest is automatically populated from git (including sub-modules)
# note that this very file (Lmakefile.py) is indeed a source
lmake.manifest = (
	'Lmakefile.py'
,	'hello'
,	'world'
)

class CatSh(Rule) :
	'concatenate 2 files using a bash script'
	# targets are made of variable part (here, File1 and File2) and fixed parts (outside {})
	# each variable parts is associated to a regexpr and this defines a whole regexpr for the target
	# a file matches this rule as soon as it matches the regexpr for its target
	# the rule apply as soon as it matches and its deps are buildable
	# a dep is buildable if a rule applies for it or if it is a source (listed in lmake.manifest)
	# note : when containing regexprs, it is safer and more readable to use r-strings
	target = r'{File1:.*}-{File2:.*}-sh'
	# when a file is matched against target as a regexpr, this defines the stems File1 and File2
	# they can be used to describe the deps
	# thus, if hello-world-sh is matched, File1 becomes hello and File2 becomes world
	deps = {
		'FIRST'  : '{File1}'
	,	'SECOND' : '{File2}'
	}
	# because cmd is a str, it is interpreted with bash
	# although written without a leading f, cmd is actually interpreted as an f-string
	# when evaluating this "f-string", targets, deps and stems are defined and can be used
	# when the single target form is used, stdout is redirected to it
	cmd = 'cat {FIRST} {SECOND}'

class CatPy(PyRule) :
	'concatenate 2 files using a python script'
	# regexprs associated to each stem can be defined in the stems attribute
	# this allows sharing of such regexprs among several rules through inheritance
	stems   = { 'File1':r'.*' , 'File2':r'.*' }
	# File1 and File2 regexprs are taken from the stems
	targets = { 'OUT':'{File1}-{File2}-py' }
	deps = {
		'FIRST'  : '{File1}'
	,	'SECOND' : '{File2}'
	}
	# because cmd is a function, it is interpreted as a python function called upon job execution
	# this is not a method : there is no "self" argument
	# however, stems, targets and deps are defined as globals when this function is evaluated
	def cmd() :
		# for multi-target rules, stdout is recorded in the log, accessible with lshow -o
		print('concatenate {FIRST} and {SECOND}')
		with open(OUT,'w') as out :
			for fn in (FIRST,SECOND) :
				with open(fn) as f : out.write(f.read())
