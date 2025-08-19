# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys

import re
import time

from lmake.rules import Rule,PyRule

version = 1

def balanced(n) :
	if not n : return r'[^{}]*'
	p = balanced(n-1)
	return fr'{p}(\{{{p}\}}{p})*'
class BaseRule(Rule) :
	stems = {
		'File'    : r'.*'
	,	'SubExpr' : balanced(0)
	,	'Expr'    : balanced(1)
	,	'Digit'   : r'\d'
	}
	stems['Expr1'] = stems['Expr']
	stems['Expr2'] = stems['Expr']
	shell = Rule.shell + ('-e',)

class Auto(BaseRule) :
	target = 'auto{Digit}'
	cmd    = "echo '#auto'{Digit}"

class Cpy(BaseRule) :
	target = '{File}.cpy'
	dep    = '{File}'
	cmd    = 'cat'

class Mv(BaseRule) :
	targets = {
		'TARGET' : '{File}.mv'
	,	'TT'     : '{File}.tmp'
	}
	dep     = '{File}'
	autodep = 'ld_preload'
	cmd     = 'cat >{TARGET} ; mv {TARGET} {TT} ; cp {TT} {TARGET}'

class Par(BaseRule) :
	target = '{{{SubExpr}}}'
	dep    = '{SubExpr}'
	cmd    = 'cat'

class Hide(BaseRule) :
	target    = '{File}.hide'
	stderr_ok = True
	cmd       = 'cat {File} || :'

class Version(BaseRule,PyRule) :
	target = '{File}.version'
	dep    = '{File}'
	def cmd() :
		sys.stdout.write(sys.stdin.read())
		print(f'#version{version}')

for to in (None,5) :
	to_ = '' if to==None else str(to)
	class Wait(BaseRule,PyRule) :
		name    = f'wait{to_}'
		stems   = { 'Wait' : r'r?w\d+|r?\d+w?|\d+rw?'       }
		targets = { 'TGT'  : f'{{File}}.{{Wait}}.wait{to_}' }
		deps    = { 'SRC' : '{File}' }
		timeout = to
		def cmd() :
			span = re.search(r'\d+',Wait)
			read_before = 'r' not in Wait[span.end  ():            ]
			write_after = 'w' not in Wait[            :span.start()]
			cnt         = int(       Wait[span.start():span.end  ()])
			print('before',flush=True)
			if     read_before : text = open(SRC).read()
			if not write_after : open(TGT,'w').write(text)
			time.sleep(cnt)
			if not read_before : text = open(SRC).read()
			if     write_after : open(TGT,'w').write(text)
			print('after',flush=True)

class Cat(BaseRule) :
	target = '{Expr1}+{Expr2}'
	deps = {
		'FIRST'  : '{Expr1}'
	,	'SECOND' : '{Expr2}'
	}
	cmd = 'cat {FIRST} {SECOND}'

class Dup(BaseRule,PyRule) :
	targets = {
		'DST1' : '{File}.dup1'
	,	'DST2' : '{File}.dup2'
	}
	dep = '{File}'
	def cmd() :
		text = sys.stdin.read()
		open(DST1,'w').write(text)
		open(DST2,'w').write(text)

class Star(BaseRule,PyRule) :
	targets = { 'DST' : '{File}.star{Digit*}' }
	dep     = '{File}'
	def cmd() :
		text = sys.stdin.read()
		open(f'{File}.star1','w').write(text)
		open(f'{File}.star2','w').write(text)

class Cmp(BaseRule) :
	target = '{File}.ok'
	deps = {
		'DUT' : '{File}'
	,	'REF' : '{File}.ref'
	}
	cmd = 'diff {REF} {DUT}'

class Inf(BaseRule,PyRule) :
	target = '{File}.inf'
	dep    = '{File}.inf.inf'
	def cmd() : print(sys.stdin.read(),'inf')

class CircularDeps(BaseRule,PyRule) :
	target = '{File}.circ'
	dep    = '{File}.circ.cpy'
	def cmd() :
		print(sys.stdin.read(),'circ')

class CircularHiddenDeps(BaseRule,PyRule) :
	target = '{File}.hcirc'
	def cmd() :
		print( open(f'{File}.hcirc.cpy').read() , 'hcirc' )

class Force(PyRule) :
	target = 'force'
	force  = True
	def cmd() :
		print('force')

class Import(BaseRule,PyRule) :
	target = '{File}.import'
	dep    = '{File}'
	def cmd() :
		import hello
		print(hello.hello(sys.stdin.read()))

class DynAttr(BaseRule) :
	target  = '{File}.dyn_attr'
	dep     = '{File}'
	environ = { 'DEP' : '{File}' }
	cmd     = 'echo $DEP'

class DynImport(BaseRule,PyRule) :
	target = '{File}.dyn_import'
	dep    = '{File}'
	def cmd() :
		import auto1_hello
		print(auto1_hello.hello(sys.sdin.read()))

def dec(x) :
	x = int(x)
	if x==1 : return ''
	else    : return x-1
class Fstring(Cpy) :
	target = '{File}.cpy{Digit}'
	dep    = '{File}.cpy{dec(Digit)}'

def add_rule() :
	class CpyNew(Cpy) :
		target = '{File}.cpyn'
