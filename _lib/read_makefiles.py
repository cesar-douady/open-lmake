# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys
sys.implementation.cache_tag = None # dont read pyc files as our rudimentary dependency mechanism will not handle them properly
sys.dont_write_bytecode      = True # and dont generate them

import os
import os.path as osp

lmake_private_lib = osp.dirname(__file__ )
lmake_root        = osp.dirname(lmake_private_lib)
lmake_lib         = lmake_root+'/lib'

assert sys.path[0]==lmake_private_lib # normal python behavior : put script dir as first entry

sys.path[0:0] = [lmake_lib]

if len(sys.argv)!=5 :
	print('usage : python read_makefiles.py <out_file> <environ_file> /[config/][rules/][sources/][top/] sub_repos_s',file=sys.stderr)
	sys.exit(1)

out_file     =      sys.argv[1]
environ_file =      sys.argv[2]
actions      =      sys.argv[3]
sub_repos_s  = eval(sys.argv[4])
is_top       = '/top/' in actions
actions      = actions.replace('/top/','/')
cwd          = os.getcwd()

if is_top : os.environ['TOP_REPO_ROOT'] = cwd
if True   : os.environ['REPO_ROOT'    ] = cwd

import lmake
from   lmake import _maybe_lcl
import fmt_rule

lmake.user_environ = eval(open(environ_file).read()) # make original user env available while reading config
pdict              = lmake.pdict

# suppress access to _lib (not  for user usage)
# add access to repo      (only for user usage)
sys.path = [ lmake_lib , *sys.path[2:] , '.' ]
import Lmakefile
# if sys.path has been manipulated, record it for dynamic attribute execution
# but only extern imports are allowed, so filter out local dirs
sys_path = tuple(d for d in sys.path if not _maybe_lcl(d))

config = pdict()
if '/config/' in actions :
	if callable(getattr(Lmakefile,'config',None)) :           # /!\ dont use try/except to ensure errors inside Lmakefile.config() are correctly caught
		Lmakefile.config()
	else :
		try :
			import Lmakefile.config
		except ImportError as e :
			if e.name!='Lmakefile.config' : raise
	config = lmake.config
	if 'backends' in config and 'sge' in config['backends'] : # XXX> : suppress when backward compatibility can be broken
		sge = config['backends']['sge']
		for old,new in (('bin_dir','bin'),('root_dir','root')) :
			if old in sge and new not in sge :
				sge[new] = sge[old]
				del sge[old]
	if not isinstance(config,pdict) : config = pdict.mk_deep(config)
	if is_top :
		if lmake.manifest : actions += 'sources/'
		if lmake._rules   : actions += 'rules/'
		for be in config.get('backends',{}).values() :
			if 'interface' not in be : continue
			import serialize
			expr = serialize.get_expr(
				be['interface']
			,	ctx            = (config.__dict__,)
			,	call_callables = True
			)
			be['interface'] = expr.ctx+'interface = '+expr.expr

sources = []
if '/sources/' in actions :
	if callable(getattr(Lmakefile,'sources',None)) : # /!\ dont use try/except to ensure errors inside Lmakefile.sources() are correctly caught
		Lmakefile.sources()
	else :
		try :
			import Lmakefile.sources
		except ImportError as e :
			if e.name!='Lmakefile.sources' : raise
	if not lmake.manifest :
		from lmake import sources
		lmake.manifest = sources.auto_sources()
	sources = list(lmake.manifest)

rules = []
if '/rules/' in actions :
	if callable(getattr(Lmakefile,'rules',None)) :                                                     # /!\ dont use try/except to ensure errors inside Lmakefile.rules() are correctly caught
		Lmakefile.rules()
	else :
		try :
			import Lmakefile.rules
		except ImportError as e :
			if e.name!='Lmakefile.rules' : raise
	fmt_rule.no_imports |= { r.__module__ for r in lmake._rules if fmt_rule.lcl_mod_file(r.__module__) } # transport by value all local modules that contain at least one rule
	for r in lmake._rules :
		r2 = fmt_rule.fmt_rule(r)
		if r2 : rules.append(r2)

#
# manage sub-repos
#

def max_link_support(*link_supports) :
	for ls in ('full','Full','file','File','none','None',None) :
		if ls in link_supports : return ls
	raise ValueError(f'unexpected link_support values : {link_support}')

def merge_config( config , sub_config , sub_dir_s ) :
	config.link_support = max_link_support( config.link_support , sub_config.link_support )
	if sub_config.sub_repos :
		config.sub_repos += tuple(sub_dir_s+sr for sr in sub_config.sub_repos )

def merge_sources( sources , sub_sources , sub_dir_s ) :
	sources[:] = [ s for s in sources if not s.startswith(sub_dir_s) ] # in sub-repo, sources are provided by sub-repo, not by top-level
	for s in sub_sources :
		if   not s.endswith  ('/'  ) : sources.append(sub_dir_s+s)     # source file
		elif     s.startswith('/'  ) : sources.append(          s)     # absolute source dir
		elif not s.startswith('../') : sources.append(sub_dir_s+s)     # local source dir
		else :                                                         # relative external source dir : walks up sub_dir_s
			sd_s = sub_dir_s
			while sd_s and s.startswith('../') : sd_s , s = sd_s[0:sd_s.rfind('/',0,-1)+1] , s[3:]
			# if local to the top level dir, relative external sources of the sub-repo are deemed produced by the top-level (or other sub-repo's) rules, thus they are no more external sources
			if s.startswith('../') : sources.append(sd_s+s)

def merge_rules( sub_rules , sub_dir_s ) :
	global rules
	for r in sub_rules.rules : r.sub_repo_s = sub_dir_s+getattr(r,'sub_repo_s','')
	rules += sub_rules['rules']

if sub_repos_s==... : sub_sub_repos_s,sub_repos_s = ...,(d+'/' for d in config.sub_repos) # recurse if not provided explicitly
else                : sub_sub_repos_s             = ()

if sub_repos_s : import subprocess as sp
for sub_repo_s in sub_repos_s :
	if not isinstance(sub_repo_s,str)                        : raise TypeError (f'in {cwd}, sub-repo ({sub_repo_s}) must be a str')
	if any(w in '/'+sub_repo_s for w in ('//','/./','/../')) : raise ValueError(f'in {cwd}, sub-repo ({sub_repo_s}) must be local and canonical')
	os.chdir(sub_repo_s[:-1])
	rc = sp.run(( sys.executable , sys.argv[0] , osp.join(cwd,out_file) , osp.join(cwd,environ_file) , actions , str(sub_sub_repos_s) )).returncode
	if rc : sys.exit(rc)
	os.chdir(cwd)
	sub_infos = pdict.mk_deep(eval(open(out_file).read()))
	if '/config/'  in actions : merge_config ( config  , sub_infos.config  , sub_repo_s )
	if '/sources/' in actions : merge_sources( sources , sub_infos.sources , sub_repo_s )
	if '/rules/'   in actions : merge_rules  (           sub_infos.rules   , sub_repo_s )

sources = sorted(set(sources)) # suppress duplicates if any

# generate output
# could be a mere print, but it is easier to debug with a prettier output
# XXX! : write a true pretty printer

lvl_stack = []
def sep(l) :
	assert l<=len(lvl_stack)
	indent = l==len(lvl_stack)
	if indent : lvl_stack.append(0)
	lvl_stack[l+1:]  = []
	lvl_stack[l]    += 1
	return '\t'*l + (',','')[indent] + '\t'
def tuple_end(l) : return '\t'*(l+1) + ('',',')[ len(lvl_stack)>l+1 and lvl_stack[l+1]==1 ] # /!\ must add a comma at end of singletons
def dict_end (l) : return '\t'*(l+1)

with open(out_file,'w') as out :
	print('{',file=out)
	#
	if '/config/' in actions :
		kl = max((len(repr(k)) for k in config.keys()),default=0)
		if True                   : print(f"{sep(0)}'config' : {{"     ,file=out)
		for k,v in config.items() : print(f'{sep(1)}{k!r:{kl}} : {v!r}',file=out)
		if True                   : print(f'{dict_end(0)}}}'           ,file=out)
	#
	if rules or '/rules/' in actions :
		print(f"{sep(0)}'rules' : {{"             ,file=out)
		print(f"{sep(1)}'sys_path' : {sys_path!r}",file=out) # for dynamic attributes execution
		print(f"{sep(1)}'rules' : ("              ,file=out)
		for r in rules :
			kl = max((len(repr(k)) for k in r.keys()),default=0)
			if True              : print(f'{sep(2)}{{'                ,file=out)
			for k,v in r.items() : print(f'{sep(3)}{k!r:{kl}} : {v!r}',file=out)
			if True              : print(f'{dict_end(2)}}}'           ,file=out)
		print(f'{tuple_end(1)})',file=out)
		print(f'{dict_end(0)}}}',file=out)
	#
	if sources or '/sources/' in actions :
		if True            : print(f"{sep(0)}'sources' : (",file=out)
		for src in sources : print(f'{sep(1)}{src!r}'      ,file=out)
		if True            : print(f'{tuple_end(0)})'      ,file=out)
	#
	print('}',file=out)
