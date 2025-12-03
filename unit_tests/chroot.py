# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import os.path as osp
dn = osp.dirname

import lmake

root = dn(dn(dn(lmake.__file__)))
def image_root        (os) : return f'{dn(root)}/dockers/{os}'
def lmake_install_root(os) : return f'{dn(root)}/open-lmake-{os}'

if __name__!='__main__' :

	from lmake.rules import Rule,PyRule

	lmake.manifest = ('Lmakefile.py','../')

	class Cat(Rule) :
		target     = 'dut-{Os:.*}'
		chroot_dir = '{image_root(Os)}'
		os_info    = r'.*'                      # XXX put a more restrictive os-dependent criteria here
		lmake_root = '{lmake_install_root(Os)}'
		autodep    = 'ld_preload'               # works on all os
		cmd        = 'echo dut'

else :

	os_candidates = (
		'centos7'
	,	'debian12'
	,	'rocky8'   , 'rocky9'
	,	'suse154'  , 'suse155'  , 'suse156'
	,	'ubuntu20' , 'ubuntu22' , 'ubuntu24'
	)
	os_known = [ os for os in os_candidates if osp.isdir(image_root(os)) and osp.isdir(lmake_install_root(os)) ]
	if not os_known :
		print('no foreign os available',file=open('skipped','w'))
		exit()

	import ut

	ut.lmake( *(f'dut-{os}' for os in os_known) , done=len(os_known) )
