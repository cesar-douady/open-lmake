# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import os
import sys

import lmake

import serialize
import fmt_rule

cwd = os.getcwd()

def fmt_callable( func , res='' ) :
	if not callable(func) : raise TypeError('not callable')
	expr = serialize.get_expr(
		func
	,	ctx            = sys.modules[func.__module__].__dict__
	,	no_imports     = fmt_rule.lcl_mod_file                 # cannot afford local imports, so serialize in place all of them
	,	call_callables = True
	)
	if res : res_eq = res+' = '
	else   : res_eq = ''
	return (
		expr.glbs+res_eq+expr.expr+'\n'
	+	"print(end='',flush=True)\n"
	)

def stringify(x) :
	if   x in (None,...)                    : return x
	elif isinstance(x,(bool,float,int,str)) : return x
	elif isinstance(x,list )                : return list (  (             stringify(v) for   v in x        ))
	elif isinstance(x,set  )                : return set  (  (stringify(k)              for k   in x        ))
	elif isinstance(x,tuple)                : return tuple(  (             stringify(v) for   v in x        ))
	elif isinstance(x,dict )                : return dict (**{stringify(k):stringify(v) for k,v in x.items()})
	else                                    : return str(x)


StdAttrs = {
	'disk_date_precision' : float
,	'file_sync'           : str
,	'heartbeat'           : float
,	'heartbeat_tick'      : float
,	'link_support'        : str
,	'local_admin_dir'     : str
,	'max_dep_depth'       : int
,	'max_error_lines'     : int
,	'network_delay'       : float
,	'nice'                : int
,	'path_max'            : int
,	'sub_repos'           : tuple
,	'req_start_proc'      : fmt_callable
,	'req_end_proc'        : fmt_callable
,	'server_start_proc'   : fmt_callable
,	'server_end_proc'     : fmt_callable
,	'system_tag_proc'     : lambda f:fmt_callable(f,'system_tag')
,	'backends'            : stringify
,	'caches'              : stringify
,	'codecs'              : stringify
,	'collect'             : stringify
,	'colors'              : stringify
,	'console'             : stringify
,	'debug'               : stringify
,	'trace'               : stringify
}

def fmt_config( config , is_top ) :
	if 'system_tag_proc' not in config and 'system_tag' in config :                                                           # XXX> : suppress when compatibility with 26.02 is no more necessary
		print(f'lmake.config.system_tag is deprecated',file=sys.stderr)
		print(f'use lmake.config.system_tag_proc'     ,file=sys.stderr)
		config.system_tag_proc = config.system_tag
	for k,v in config.items() :
		if k not in StdAttrs : raise KeyError("unexpected key ",k)
		config[k] = StdAttrs[k](v)
	if is_top :
		git = '/usr/bin/git'                                                                                                  # substitued at build time
		if 'caches' in config :
			for cache in config.caches.values() :
				if 'repo_key' in cache : continue
				key = cwd
				try    : key += ' '+sp.check_output((git,'rev-parse','--verify','-q','HEAD'),universal_newlines=True).strip()
				except : pass                                                                                                 # if not under git, ignore
				cache['repo_key'] = key
	config.extra_manifest = lmake.extra_manifest
	return config
