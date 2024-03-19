# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,HomelessRule,RustRule

	from step import step,has_jemalloc

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	,	'hello/Cargo.toml'
	,	'hello/src/main.rs'
	,	'hello.in'
	,	'hello.ref'
	)

	class CompileRust(HomelessRule,RustRule) :
		targets      = { 'EXE'        :   '{Dir:.+/|}{Module:[^/]+}/target/debug/{Module}'                   }
		side_targets = { 'SCRATCHPAD' : ( '{Dir:.+/|}{Module:[^/]+}/{*:.*}'                , 'Incremental' ) }
		deps    = {
			'PKG' : '{Dir}{Module}/Cargo.toml'
		,	'SRC' : '{Dir}{Module}/src/main.rs'
		}
		if   step==1      : autodep     = 'ld_preload_jemalloc'
		elif step==2      : autodep     = 'ptrace'
		if   has_jemalloc : environ_cmd = { 'LD_PRELOAD' : 'libjemalloc.so' }
		allow_stderr = True
		cmd          = 'cd  {Dir}{Module} ; cargo build'

	class RunRust(RustRule) :
		targets = { 'OUT' : '{Dir:.+/|}{Module:[^/]+}.out'        }
		deps    = { 'EXE' : '{Dir}{Module}/target/debug/{Module}' }
		cmd     = './{EXE}'

	class Cmp(Rule) :
		target = '{File:.*}.ok'
		deps   = {
			'OUT' : '{File}.out'
		,	'REF' : '{File}.ref'
		}
		cmd = 'diff {REF} {OUT}'

else :

	import os
	import subprocess as sp
	import sys

	import ut

	sav = os.environ.get('LD_PRELOAD')
	os.environ['LD_PRELOAD'] = 'libjemalloc.so'
	has_jemalloc             = not sp.run(('/usr/bin/echo',),check=True,stderr=sp.PIPE).stderr
	if sav is None : del os.environ['LD_PRELOAD']
	else           :     os.environ['LD_PRELOAD'] = sav

	try    : sp.check_output('cargo') # dont test rust if rust is not installed
	except :
		print('cargo not available',file=sys.stderr)
		exit()

	os.makedirs('hello/src',exist_ok=True)
	toml = open('hello/Cargo.toml','w')
	print(
		'[package]'
	,	'name    = "hello"'
	,	'version = "0.1.0"'
	,	'edition = "2021"'
	,	sep  = '\n'
	,	file = open('hello/Cargo.toml','w')
	)
	print('''
		use std::fs::File       ;
		use std::io::prelude::* ;

		fn main() -> std::io::Result<()> {
			let mut file     = File::open("hello.in")? ;
			let mut contents = String::new()           ;
			file.read_to_string(&mut contents)?        ;

			let mut file = File::create("hello.out")? ;
			file.write_all(b"hello world\n")?         ;

			return Ok(()) ;
		}
	''',file=open('hello/src/main.rs','w'))
	print('hello world',file=open('hello.in' ,'w'))
	print('hello world',file=open('hello.ref','w'))

	print(f'step=1 ; has_jemalloc={has_jemalloc}',file=open('step.py','w'))
	ut.lmake( 'hello.ok' , done=3 , new=4 )

	print(file=open('hello/src/main.rs','a'))
	ut.lmake( 'hello.ok' , steady=1 , changed=1 ) # check cargo can run twice with no problem

	print(f'step=2 ; has_jemalloc={has_jemalloc}',file=open('step.py','w'))
	os.system('rm -rf hello/target hello.ok') # force cargo to regenerate everything
	ut.lmake( 'hello.ok' , steady=1 )         # check ptrace works with cargo
