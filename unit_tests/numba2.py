# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import lmake
from lmake.rules import Rule,_lmake_dir,root_dir

lmake.manifest = ('Lmakefile.py',)

lmake.config.backends.slurm = {
	'n_max_queued_jobs' : 10
}

class TestNumba(Rule):
   target  = 'test'
   autodep = 'ld_preload'
   backend = 'slurm'
   resources = {
       'cpu' : 1
   ,   'mem' : '256M'
   }
   python      = ('/usr/bin/python','-B','-tt')
   environ_cmd = { 'PYTHONPATH' : ':'.join((_lmake_dir+'/lib',root_dir)) }
   def cmd():
       import sys
       def callAndLog(cmd, log=sys.stdout, err=sys.stderr):
           from subprocess import check_call
           print(f'Executing : {cmd}')
           sys.stdout.flush()
           sys.stderr.flush()
           check_call(cmd, stdout=log, stderr=err, shell=True)
           sys.stdout.flush()
           sys.stderr.flush()
       from numba import vectorize, guvectorize, void, uint16, int32, uint32, float32
       @vectorize([uint32(int32, int32, int32), uint32(uint32, uint32, uint32)], target='parallel')
       def clipUi32_(x, minv, maxv):
           return uint32(minv if x < minv else (maxv if x > maxv else x))
       callAndLog('hostname')
