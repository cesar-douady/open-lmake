# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

'''
	This module is meant to be used to generate debug scripts.
	The generated script runs :
	- python jobs under pudb
	- shell jobs are not supported
'''

from .utils import Job

def gen_script(**kwds) :
	job = Job(kwds)
	assert job.is_python,f'cannot debug shell job with pudb'
	return job.gen_script(runner='lmake_debug.runtime.pudb_')
