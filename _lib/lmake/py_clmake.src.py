# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

'''
	Normally, job support functions defined here are implemented in C++ in lmake.clmake.
	However, in case the Python lib cannot be dynamically imported, this module provides a minimal fall back in pure Python.
'''

import os      as _os
import os.path as _osp

# provide minimal support in pure python
# XXX! : provide support for get_autodep() and set_autodep()

import subprocess as _sp
def _run(cmd_line,**kwds) :
	return _sp.check_output(cmd_line,universal_newlines=True,**kwds)
_bin_dir_s = _osp.dirname(_osp.dirname(_osp.dirname(__file__)))+'/bin/'
def _bin(f) : return _bin_dir_s+f
#
def depend(
	*args
,	critical=False , essential=False , ignore_error=False , required=True , ignore=False
,	follow_symlinks=False , read=False , verbose=False
) :
	assert not verbose,'verbose is not supported without dynamic python librairy'
	cmd_line = [_bin('ldepend')]
	if     critical        : cmd_line.append('--critical'       )
	if     essential       : cmd_line.append('--essential'      )
	if     ignore_error    : cmd_line.append('--ignore-error'   )
	if not required        : cmd_line.append('--no-required'    )
	if     ignore          : cmd_line.append('--ignore'         )
	if     follow_symlinks : cmd_line.append('--follow-symlinks')
	if     read            : cmd_line.append('--read'           )
	cmd_line += args
	_run(cmd_line)
def target(
	*args
,	essential=False , incremental=False , no_uniquify=False , no_warning=False , phony=False , ignore=False , no_allow=False , source_ok=False
,	follow_symlinks=False , write=False
) :
	cmd_line = [_bin('ltarget')]
	if essential       : cmd_line.append('--essential'      )
	if incremental     : cmd_line.append('--incremental'    )
	if no_uniquify     : cmd_line.append('--no-uniquify'    )
	if no_warning      : cmd_line.append('--no-warning'     )
	if phony           : cmd_line.append('--phony'          )
	if ignore          : cmd_line.append('--ignore'         )
	if no_allow        : cmd_line.append('--no-allow'       )
	if source_ok       : cmd_line.append('--source-ok'      )
	if follow_symlinks : cmd_line.append('--follow-symlinks')
	if write           : cmd_line.append('--write'          )
	cmd_line += args
	_run(cmd_line)
def check_deps(verbose=False) :
	if verbose : _run((_bin('lcheck_deps'),'--verbose'))
	else       : _run((_bin('lcheck_deps'),           ))
#
def decode(file,ctx,code         ) : return _run((_bin('ldecode'),'-f',file,'-x',ctx,'-c',code        )          )
def encode(file,ctx,val,min_len=1) : return _run((_bin('lencode'),'-f',file,'-x',ctx,'-l',str(min_len)),input=val)[:-1] # suppress terminating newline
#
def get_autodep(      ) : return True                                                                                   # placeholder
def set_autodep(enable) : pass                                                                                          # .
#
if 'TOP_REPO_ROOT' in _os.environ :
	top_repo_root = _os.environ['TOP_REPO_ROOT']
else :
	repo_root = _os.getcwd()
	while repo_root!='/' and not _osp.exists(repo_root+'/Lmakefile.py') : repo_root = _osp.dirname(repo_root)           # avoid searching Lmakefile.py to avoid new dependency
	if not repo_root : del repo_root
#
autodeps = ()
if "$HAS_LD_AUDIT" : autodeps += ('ld_audit'  ,                              )
if True            : autodeps += ('ld_preload','ld_preload_jemalloc','ptrace')
#
backends = ('local',)
if "$HAS_SGE"   : backends += ('sge'  ,)
if "$HAS_SLURM" : backends += ('slurm',)
