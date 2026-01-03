# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import json
import os
import re
import sys
import textwrap

import lmake

from lmake.rules import Rule,DirRule,PyRule,TraceRule

exe_lst = ('hello_world',) # executables for generation of compile_commands.json (for IDE integration)

class Dir(DirRule) : pass # create file '...' in any directory

class Base(Rule) :
	stems = {
		'File' : r'.+'
	,	'Exe'  : r'.+'
	,	'Test' : r'[^-]+'
	}

# for IDE integration as per : https://clang.llvm.org/docs/JSONCompilationDatabase.html
class CompCmds(Base,PyRule) :
	target = 'compile_commands.json'
	def cmd() :
		first = True
		print('[')
		for i,exe in enumerate(exe_lst) :
			if first : first = False
			else     : print(',')
			cmds = open(f'{exe}.compile_commands.json').read()
			assert cmds[:2]=='[\n' and cmds[-2:]==']\n'        # suppress leading and trailing []
			cmds = cmds[2:-2]                                  # .
			print(cmds,end='')
		print(']')

# suppress comments, so that editing source files is more comfortable
class NoComment(Base) :
	target = '{File}.nc'
	dep    = '{File}'
	cmd    = "sed -e 's: *#.*::' -e '/^$/ d' "

flags = '-O0'                            # updated by run script to simulate a modification of cmd
for ext in ('c','cc') :
	class Compile(Base,PyRule) :
		name    = f'compile {ext}'
		targets = {
			'OBJ' : '{File}.o'
		,	'LST' : '{File}.d'           # contains actually included .h files
		}
		deps    = { 'SRC':f'{{File}}.{ext}' }
		def cmd() :
			tf = f"{os.environ['TMPDIR']}/lst"
			lmake.run_cc( 'gcc' , '-MMD' , '-MF' , tf , flags , '-c' , '-o' , OBJ , SRC )
			lst = open(tf).read()
			lst = re.sub(r'\\\n','',lst) # suppress multi-line
			lst = lst.split(':',1)[1]    # suppress target: prefix
			with open(LST,'w') as fd :
				for f in lst.split() :
					if   f.endswith('.h' ) : print(f[:-2],file=fd)
					elif f.endswith('.hh') : print(f[:-3],file=fd)

	# generate fragment of compile_commands.json for IDE integration
	class CompCmdObj(Base,PyRule) :
		name   = f'gen compile_command {ext}'
		target = '{File}.compile_command.json'
		deps   = { 'SRC':f'{{File}}.{ext}' }
		def cmd() :
			print(json.dumps(
				{	'directory' : '.'
				,	'arguments' : ('gcc','-c','-o',f'{File}.o',SRC)
				,	'output'    : f'{File}.o'
				,	'file'      : SRC
				}
			,	indent = 4
			))

# generate transitive closure of included .h files
class ObjLst(Base,PyRule) :
	target = '{File}.lst'
	deps   = { 'LST':'{File}.d' }
	def cmd() :
		lst = {}                                # dont care of val, just need a set with order preserved as in dict
		def handle(d) :
			try    : l = open(d).read().split()
			except : return                     # if d does not exist, this must be a '.h only' lib and there is no object to link
			for o in l :
				if o in lst : continue
				lst[o] = None                   # insert o in object list
				handle(f'{o}.d')                # recurse
		handle(LST)
		print(f'{File}')
		for o in lst : print(o)

class Link(Base,PyRule) :
	targets = { 'EXE' :  '{Exe}.exe'                }
	deps    = { 'LST' : ('{Exe}.lst.nc','critical') }
	def cmd() :
		lmake.run_cc('gcc',f'-o{EXE}',*(f'{obj.strip()}.o' for obj in open(LST)))

# generate fragment of compile_commands.json for IDE integration
class CompCmdLnk(Base,PyRule) :
	target = '{Exe}.compile_commands.json'
	deps   = { 'LST' : ('{Exe}.lst.nc','critical') }
	def cmd() :
		first = True
		print('[')
		for obj in open(LST) :
			if first : first = False
			else     : print(',')
			obj = obj.strip()
			print(textwrap.indent(open(f'{obj}.compile_command.json').read(),'\t'),end='')
		print(']')

class Run(Base) :
	target = r'{Exe}-{Test}.out'
	deps   = {
		'EXE' : '{Exe}.exe'
	,	'IN'  : '{Exe}-{Test}.scn.nc'
	}
	cmd = './{EXE} "$(cat {IN})"'

class Chk(Base) :        # generic comparison rule
	target = '{File}.ok'
	deps = {
		'DUT' : '{File}'
	,	'REF' : '{File}.ref.nc'
	}
	cmd = 'diff {REF} {DUT}>&2'

# Here we scatter all the tests from a single, simple, regression description
# Each line contains a test, a scenario and and expected output, separated with ':'
class Scenarios(Base,PyRule) :
	targets = {
		'LST' : '{Exe}.tlst'
	,	'SCN' : '{Exe}-{Test*}.scn'     # this is a star target : it means all tests are generated in a single job execution
	,	'REF' : '{Exe}-{Test*}.out.ref' # idem
	}
	dep = '{Exe}.regr.nc'
	def cmd() :
		with open(LST,'w') as lst :
			for l in sys.stdin.readlines() :
				test,scn,ref = (x.strip() for x in l.split(':'))
				print(scn ,file=open(SCN(test),'w'))
				print(ref ,file=open(REF(test),'w'))
				print(test,file=lst                )

# here we gather results of scattered tests
class Regr(Base) :
	target =             '{Exe}.tok'
	deps   = { 'LST' : ( '{Exe}.tlst.nc' , 'critical' ) }
	cmd = r'''
		ldepend $(sed 's:.*:{Exe}-\0.out.ok:' {LST})
	'''
