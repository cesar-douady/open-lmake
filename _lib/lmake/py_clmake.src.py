# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

'''
	Normally, job support functions defined here are implemented in C++ in lmake.clmake.
	However, in case the python lib cannot be dynamically imported, this module provides a minimal fall back in pure python.
'''

import os      as _os
import os.path as _osp

# provide minimal support in pure python
# XXX? : for now, it is best effort (e.g. $LMAKE_AUTODEP_ENV is not fully resistant to pathalogical cases)
# XXX! : provide support for get_autodep() and set_autodep()

import subprocess as _sp
def _run(cmd_line,**kwds) :
	return _sp.check_output(cmd_line,universal_newlines=True,**kwds)
_lmake_root = _osp.dirname(_osp.dirname(_osp.dirname(__file__)))
def _bin(f) : return _lmake_root_s+'/bin/'+f

# python3 prototype would be (but not available with python2) : XXX> : restore better prototype when python2 no longer needs to be supported
#	def depend(
#		*args
#	,	critical=False , essential=False , ignore_error=False , required=True , ignore=False
#	,	follow_symlinks=False , read=False , verbose=False
#	)
def depend( *args , **kwds ) :
	assert not kwds.get('verbose',None),'verbose is not supported without dynamic python library'
	cmd_line = [_bin('ldepend')]
	if     kwds.get('critical'       ,False) : cmd_line.append('--critical'       )
	if     kwds.get('essential'      ,False) : cmd_line.append('--essential'      )
	if     kwds.get('ignore_error'   ,False) : cmd_line.append('--ignore-error'   )
	if not kwds.get('required'       ,True ) : cmd_line.append('--no-required'    )
	if     kwds.get('ignore'         ,False) : cmd_line.append('--ignore'         )
	if     kwds.get('follow_symlinks',False) : cmd_line.append('--follow-symlinks')
	if     kwds.get('read'           ,False) : cmd_line.append('--read'           )
	cmd_line += args
	_run(cmd_line)

# python3 prototype would be (but not available with python2) : XXX> : restore better prototype when python2 no longer needs to be supported
#	def target(
#		*args
#	,	essential=False , incremental=False , no_uniquify=False , no_warning=False , phony=False , ignore=False , no_allow=False , source_ok=False
#	,	follow_symlinks=False , write=False
#	)
def target( *args , **kwds ) :
	cmd_line = [_bin('ltarget')]
	if kwds.get('essential'      ,False) : cmd_line.append('--essential'      )
	if kwds.get('incremental'    ,False) : cmd_line.append('--incremental'    )
	if kwds.get('no_uniquify'    ,False) : cmd_line.append('--no-uniquify'    )
	if kwds.get('no_warning'     ,False) : cmd_line.append('--no-warning'     )
	if kwds.get('phony'          ,False) : cmd_line.append('--phony'          )
	if kwds.get('ignore'         ,False) : cmd_line.append('--ignore'         )
	if kwds.get('no_allow'       ,False) : cmd_line.append('--no-allow'       )
	if kwds.get('source_ok'      ,False) : cmd_line.append('--source-ok'      )
	if kwds.get('follow_symlinks',False) : cmd_line.append('--follow-symlinks')
	if kwds.get('write'          ,False) : cmd_line.append('--write'          )
	cmd_line += args
	_run(cmd_line)

def check_deps(verbose=False) :
	if verbose : _run((_bin('lcheck_deps'),'--verbose'))
	else       : _run((_bin('lcheck_deps'),           ))

def decode(file,ctx,code         ) : return _run((_bin('ldecode'),'-f',file,'-x',ctx,'-c',code        )          )
def encode(file,ctx,val,min_len=1) : return _run((_bin('lencode'),'-f',file,'-x',ctx,'-l',str(min_len)),input=val)[:-1] # suppress terminating newline

def get_autodep(      ) : return 'LMAKE_AUTODEP_ENV' in _os.environ
def set_autodep(enable) : raise NotImplemented

if 'LMAKE_AUTODEP_ENV' in _os.environ :
	ade           = _os.environ['LMAKE_AUTODEP_ENV'].split(':') # format : server:port:fast_host:fast_report_pipe:options:tmp_dir_s:repo_root_s:sub_repo_s:src_dirs_s:views
	top_repo_root =  ade[6][1:-2]                               # suppress " at start and /" at the end
	repo_root     = (ade[6][1:-1]+ade[7][1:-1])[:-1]            # .
else :
	top_repo_root = repo_root = _os.getcwd()

autodeps = ()
if "$HAS_LD_AUDIT" : autodeps += ('ld_audit'  ,                              ) # $HAS_LD_AUDIT is substituted at build time
if True            : autodeps += ('ld_preload','ld_preload_jemalloc','ptrace')

backends = ('local','sge','slurm')
