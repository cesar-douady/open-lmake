# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

'''
	This module is meant to be used to generate debug scripts.
	The generated script does not run jobs, but opens a shell in the same environment as the job.
	This includes :
	- environment variables
	- chroot dir
	- lmake_view mapping
	- repo_view  mapping
	- tmp_view   mapping
	- views      mapping
'''

import os
import os.path as osp
import pwd

from .utils import Job,mk_shell_str

def gen_script(**kwds) :
	job = Job(kwds)
	# we do not want any redirection as we do not execute job
	job.stdin  = None
	job.stdout = None
	# prepare a comfortable interactive environment
	job.env.pop('HOME' ,None)
	job.env.pop('SHLVL',None)
	job.keep_env += ('HOME','SHLVL')
	#
	try          : passwd = pwd.getpwuid(os.getuid())
	except       : passwd = None
	if passwd    : shell  = passwd.pw_shell
	else         : shell  = None
	if not shell : shell  = '/bin/bash'
	#
	home = None
	if job.chroot_dir and passwd :
		for i in range(100) :
			home = f"/home_ldebug{i or ''}"
			if not osp.exists(home) :
				job.views[home] = passwd.pw_dir
				break
		else :
			assert False,'cannot find a top-level name to mount home dir'
	#
	start_preamble,start_line = job.starter(mk_shell_str(shell))
	return (
		job.gen_init()
	+	( f'export HOME={home}\n' if home else '' )
	+	start_preamble
	+	f'exec {start_line}\n' # exec so that SHLVL is incremented once only
	)
