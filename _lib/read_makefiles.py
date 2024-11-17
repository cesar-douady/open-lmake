# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys
sys.implementation.cache_tag = None # dont read pyc files as our rudimentary dependency mechanism will not handle them properly
sys.dont_write_bytecode      = True # and dont generate them

import os
import os.path as osp

lmake_lib_dir = osp.dirname(__file__     )
lmake_dir     = osp.dirname(lmake_lib_dir)

assert sys.path[0]==lmake_lib_dir # normal python behavior : put script dir as first entry

sys.path[0:0] = [lmake_dir+'/lib']

if len(sys.argv)!=4 :
	print('usage : python read_makefiles.py <out_file> [config|rules|srcs] sub_repos',file=sys.stderr)
	sys.exit(1)

out_file  =      sys.argv[1]
action    =      sys.argv[2]
sub_repos = eval(sys.argv[3])

import lmake     # import before user code to be sure user did not play with sys.path
import serialize

from fmt_rule import fmt_rule

lmake.user_environ = eval(open('LMAKE/user_environ').read()) # make original user env available while reading config
pdict              = lmake.pdict

assert sys.path[1]== lmake_lib_dir
sys.path[1:2] = []                 # suppress access to _lib : not for user usage

import importlib

def chdir(dir) :
	os.chdir(dir)

def max_link_support(*link_supports) :
	for ls in ('full','Full','file','File','none','None',None) :
		if ls in link_supports : return ls
	raise ValueError(f'unexpected link_support values : {link_support}')

def merge_config( config , sub_config , sub_dir_s ) :
	config.link_support  = max_link_support( config.link_support , sub_config.link_support )
	if sub_config.sub_repos :
		if not all(sr for sr in sub_config.sub_repos) : raise ValueError(f'sub_repos cannot be empty in {sub_dir_s[:-1]}')
		config.sub_repos += tuple( sub_dir_s+sr for sr in sub_config.sub_repos )

def merge_manifest( manifest , sub_manifest , sub_dir_s ) :
	manifest[:] = [ s for s in manifest if not s.startswith(sub_dir_s) ] # in sub-repo, sources are provided by sub-repo, not by top-level
	for s in sub_manifest :
		if   not s.endswith  ('/'  ) : manifest.append(sub_dir_s+s)      # source file
		elif     s.startswith('/'  ) : manifest.append(          s)      # absolute source dir
		elif not s.startswith('../') : manifest.append(sub_dir_s+s)      # local source dir
		else :                                                           # relative external source dir : walks up sub_dir_s
			sd_s = sub_dir_s
			while sd_s and s.startswith('../') : sd_s , s = sd_s[0:sd_s.rfind('/',0,-1)+1] , s[3:]
			# if local to the top level dir, relative external sources of the sub-repo are deemed produced by the top-level (or other sub-repo's) rules, thus they are no more external sources
			if s.startswith('../') : manifest.append(sd_s+s)

def merge_rules( rules , sub_rules , sub_dir_s ) :
	sub_dir = sub_dir_s[:-1]
	for r in sub_rules :
		cwd = getattr(r,'cwd','')
		if cwd : r.cwd = sub_dir_s+cwd
		else   : r.cwd = sub_dir
	rules += sub_rules

def read_makefiles(dir_s,actions,sub_repos) :
	lmake.reset()
	sub_actions = actions
	if dir_s :
		cwd = os.getcwd()
		os.chdir(dir_s[:-1])
		del sys.modules['Lmakefile']                                   # ensure sub-Lmakefile is imported from disk
		importlib.invalidate_caches()                                  # .
	sys.path[0:0] = [ os.getcwd() ]                                    # cannot use '.' because of buggy importlib.invalidate_caches() before python3.12
	import Lmakefile
	sys.path[0:1] = []
	for a in actions :
		if a=='config' and not dir_s :                                 # at top level, extend actions if we are not a package
			if lmake.manifest : sub_actions += ('sources',)
			if lmake._rules   : sub_actions += ('rules'  ,)
		try :
			getattr(Lmakefile,a)()
		except :
			try    : importlib.import_module('.'+a,'Lmakefile')
			except : pass
		if a=='sources' and not lmake.manifest :
			from lmake import sources
			lmake.manifest = sources.auto_sources()
	config   = lmake.config
	manifest = list(lmake.manifest)
	rules    = lmake._rules
	if sub_repos==... : sub_repos,sub_sub_repos = config.sub_repos,... # recurse if not provided explicitely
	else              :           sub_sub_repos =                  ()
	for sub_dir_s in sub_repos :
		if not isinstance(sub_dir_s,str) : raise TypeError (f'sub-lmakefile must be a str, not {sub_dir_s}')
		if not sub_dir_s                 : raise ValueError('empty sub-lmakefile'                          )
		if sub_dir_s[-1]!='/'            : sub_dir_s += '/'
		sub_config , sub_manifest , sub_rules = read_makefiles( sub_dir_s , sub_actions , sub_sub_repos )
		if 'config'  in sub_actions : merge_config  ( config   , sub_config   , sub_dir_s )
		if 'sources' in sub_actions : merge_manifest( manifest , sub_manifest , sub_dir_s )
		if 'rules'   in sub_actions : merge_rules   ( rules    , sub_rules    , sub_dir_s )
	if dir_s :
		os.chdir(cwd)
	return ( config , manifest , rules )

config , manifest , rules = read_makefiles('',(action,),sub_repos)
manifest = sorted(set(manifest))                                   # suppress duplicates if any (e.g. because of relative external sources of sub-repo's)

def handle_config(config) :
	for be in config.get('backends',{}).values() :
		if 'interface' not in be : continue
		code,ctx,names,dbg = serialize.get_expr(
			be['interface']
		,	ctx            = (config.__dict__,)
		,	call_callables = True
		)
		be['interface'] = ctx+'interface = '+code

# generate output
# could be a mere print, but it is easier to debug with a prettier output
# XXX : write a true pretty printer

def error(txt='') :
	print(txt,file=sys.stderr)
	exit(2)

if action=='config' :
	handle_config(lmake.config)

lvl_stack = []
def sep(l) :
	assert l<=len(lvl_stack)
	indent = l==len(lvl_stack)
	if indent : lvl_stack.append(0)
	lvl_stack[l+1:]  = []
	lvl_stack[l]    += 1
	return '\t'*l + (',','')[indent] + '\t'
def tuple_end(l) :                                                   # /!\ must add a comma at end of singletons
	return '\t'*l + ('',',')[ len(lvl_stack)>l and lvl_stack[l]==1 ]

with open(out_file,'w') as out :
	print('{',file=out)
	#
	if action=='config' :
		print(f"{sep(0)}'config' : {{",file=out) ;
		kl  = max((len(repr(k)) for k in config.keys()),default=0)
		for k,v in config.items() :
			print(f'{sep(1)}{k!r:{kl}} : {v!r}',file=out)
		print('\t}',file=out)
	#
	if rules or action=='rules' :
		import fmt_rule
		fmt_rule.no_imports = { r.__module__ for r in rules } # transport by value all modules that contain a rule
		print(f"{sep(0)}'rules' : (",file=out)
		for r in rules :
			rule = fmt_rule.fmt_rule(r)
			if not rule : continue
			print(f'{sep(1)}{{',file=out)
			kl = max((len(repr(k)) for k in rule.keys()),default=0)
			for k,v in rule.items() :
				if v!=None : print(f'{sep(2)}{k!r:{kl}} : {v!r}',file=out)
			print('\t\t}',file=out)
		print(f'{tuple_end(1)})',file=out)
	#
	if manifest or action=='sources' :
		print(f"{sep(0)}'manifest' : (",file=out)
		for src in manifest :
			print(f'{sep(1)}{src!r}',file=out)
		print(f'{tuple_end(1)})',file=out)
	#
	print('}',file=out)
