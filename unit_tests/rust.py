# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,AntiRule,RustRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'hello.rs'
	,	'hello.in'
	,	'hello.ref'
	)

	class CompileRust(RustRule) :
		targets = { 'EXE' : '{File:.*}' }
		deps    = { 'SRC' : '{File}.rs' }
		cmd     = 'rustc -g -o {EXE} {SRC}'

	class AntiRustRust(AntiRule) :
		target = '{:.*}.rs.rs'

	class RunRust(RustRule) :
		targets = { 'OUT' : '{File:.*}.out' }
		deps    = { 'EXE' : '{File}'        }
		cmd     = './{EXE} {File}.in {OUT}'

	class Cmp(Rule) :
		target = '{File:.*}.ok'
		deps   = {
			'OUT' : '{File}.out'
		,	'REF' : '{File}.ref'
		}
		cmd = 'diff {REF} {OUT}>&2'

else :

	import subprocess as sp
	import sys

	import ut

	try    : sp.check_output('rustc') # dont test rust if rust in not installed
	except :
		print('rustc not available',file=open('skipped','w'))
		exit()

	print('''

		use std::env            ;
		use std::fs::File       ;
		use std::io::prelude::* ;

		fn main() -> std::io::Result<()> {
			let     args     : Vec<_> = env::args().collect() ;
			let mut file              = File::open(&args[1])? ;
			let mut contents          = String::new()         ;
			file.read_to_string(&mut contents)? ;

			let mut file = File::create(&args[2])? ;
			file.write(contents.as_bytes())? ;

			return Ok(()) ;
		}
	''',file=open('hello.rs','w'))
	print('hello world',file=open('hello.in' ,'w'))
	print('hello world',file=open('hello.ref','w'))

	ut.lmake( 'hello.ok' , done=3 , new=3 )

	print('hello world2',file=open('hello.in' ,'w'))
	print('hello world2',file=open('hello.ref','w'))
	ut.lmake( 'hello.ok' , done=1 , steady=1 , changed=2 ) # check we have acquired hello.in as a dep
