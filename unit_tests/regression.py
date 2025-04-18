# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import os
	import os.path as osp
	import re
	import sys

	import lmake
	from lmake.rules import PyRule

	from step import step

	lmake.manifest = (
		'Lmakefile.py'
	,	'refs'
	,	'step.py'
	)

	class Base(PyRule) :
		stems = {
			'File' : '.+'
		,	'Any'  : '.+'
		}

	class Produce(Base) :
		target = '{File}.dut'
		def cmd() :
			print(f'{target}-{step}') # use step to trigger modifications

	class Chk(Base) :
		targets = {
			'REF_OK'  :  '{File}.chk'
		,	'NEW_REF' : ('{File}.new','phony')
		}
		deps = {
			'DUT' : '{File}'
		}
		side_deps = {
			'REF'  : '{File}.ref' # Set as side_deps to be optionnal, so that it is possible to add new ref
		}
		def cmd() :
			try                      : ref_crc = open(REF).read().strip()
			except FileNotFoundError : ref_crc = None
			dut_crc = open(DUT).read().strip()
			print(dut_crc,file=open(REF_OK,'w'))
			if dut_crc!=ref_crc :
				print(f'bad ref : {dut_crc} != {ref_crc}',file=sys.stderr)
				open(NEW_REF,'w')

	class ExpandRef(Base) :
		targets = { 'REF'  : '{File*}.ref' }
		deps    = { 'REFS' : 'refs'        }
		def cmd() :
			for l in open(REFS) :
				file,crc = l.split()
				print(crc,file=open(REF(file),'w'))

	class Update(Base) :
		force = True
		targets = {
			'TGT' : '{File}.update'
		}
		side_targets = {
			'REFS' : ( '{File}'     , 'source_ok','incremental' )
		,	'NEW'  : ( '{Any*}.new' , 'ignore'                  )
		}
		side_deps = {
			'CHK' : ( '{Any*}.chk' , 'ignore_error' )
		}
		def cmd():
			new_re     = re.compile(NEW.reg_expr)
			new_refs   = ''
			known_refs = set()
			for l in open(REFS) :
				file,ref_crc = l.split()
				try    : dut_crc = open(CHK(file)).read().strip()
				except : dut_crc = None
				new_refs += f'{file} {dut_crc}\n'
				known_refs.add(file)
			for path,_,files in os.walk('.') :
				path_s = (path+'/')[2:] # remove prefix './' (but removeprefix is not available in python3.6)
				for name in files :
					file_new = path_s+name
					match    = new_re.fullmatch(file_new)
					if not match : continue
					file = match.groupdict()['Any']
					if file in known_refs : continue
					dut_crc   = open(CHK(file)).read().strip()
					new_refs += f'{file} {dut_crc}\n'
					os.unlink(file_new)
			open(TGT,'w')
			open(REFS,'w').write(new_refs)

else :

	from lmake import multi_strip

	import ut

	open('refs','w').write(multi_strip('''
		1.dut 1.dut-bad
	'''))
	print('step=1',file=open('step.py','w'))
	#
	# udpate
	#
	ut.lmake( 'refs.update' , done=3   , may_rerun=2 , failed=1        )
	ut.lmake( '1.dut.chk'   , done=2                                   ) # refs has been updated
	ut.lmake( '2.dut.chk'   , done=1   ,               failed=1 , rc=1 ) # no ref available
	ut.lmake( 'refs.update' , steady=1 ,                               ) # refs added

	print('step=2',file=open('step.py','w'))
	ut.lmake( 'refs.update' , done=3   , failed=2 , steady=1 , was_failed=2 ) # refs recomputed
	ut.lmake( '1.dut.chk'   , done=2                                        ) # refs has been updated
