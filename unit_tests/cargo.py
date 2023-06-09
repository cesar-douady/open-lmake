# This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys

if getattr(sys,'reading_makefiles',False) :

	import lmake

	lmake.sources = (
		'Lmakefile.py'
	,	'hello/Cargo.toml'
	,	'hello/src/main.rs'
	,	'hello.in'
	,	'hello.ref'
	)

	class CompileRust(lmake.HomelessRule,lmake.RustRule) :
		targets = {
			'EXE'        :   '{Dir:.+/|}{Module:[^/]+}/target/debug/{Module}'
		,	'SCRATCHPAD' : ( '{Dir:.+/|}{Module:[^/]+}/{*:.*}'                , '-Crc','-Dep','Incremental','-Match' )
		}
		deps    = {
			'PKG' : '{Dir}{Module}/Cargo.toml'
		,	'SRC' : '{Dir}{Module}/src/main.rs'
		}
		cmd     = 'cd  $Dir$Module ; cargo build 2>&1'

	class RunRust(lmake.RustRule) :
		targets = { 'OUT' : '{Dir:.+/|}{Module:[^/]+}.out' }
		deps    = { 'EXE' : '{Dir}{Module}/target/debug/{Module}' }
		cmd     = './$EXE'

	class Cmp(lmake.Rule) :
		target = '{File:.*}.ok'
		deps   = {
			'OUT' : '{File}.out'
		,	'REF' : '{File}.ref'
		}
		cmd = 'diff $REF $OUT'

else :

	import os
	import subprocess as sp

	import ut

	try    : sp.check_output('cargo')                                          # dont test rust if rust is not installed
	except : exit()

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

	ut.lmake( 'hello.ok' , done=3 , new=4 )

	print(file=open('hello/src/main.rs','a'))
	ut.lmake( 'hello.ok' , steady=1 , new=1 )                                  # check cargo can run twice with no problem
