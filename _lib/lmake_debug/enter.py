# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import os

from .utils import Job,mk_shell_str

def gen_script(**kwds) :
	job        = Job(kwds)
	job.stdin  = None                                                              # we do not want any redirection as we do not execute job
	job.stdout = None                                                              # .
	try    : del job.env['HOME']                                                   # keep HOME and SHLVL to execute interactive sub-shell normally
	except : pass                                                                  # .
	job.keep_env += ('HOME','SHLVL')                                               # .
	start_preamble,start_line = job.starter(mk_shell_str(os.getenv('SHELL')),'-i')
	return (
		job.gen_init()
	+	start_preamble
	+	f'exec {start_line}\n'                                                     # exec so that SHLVL is incremented once only
	)
