# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys
sys.implementation.cache_tag = None # dont read pyc files as our rudimentary dependency mechanism will not handle them properly
sys.dont_write_bytecode      = True # and dont generate them

import os
import os.path as osp

lmake_dir = osp.dirname(osp.dirname(__file__))

sys.path = [lmake_dir+'/lib',lmake_dir+'/_lib','.',*sys.path] # ensure we have safe entries in front so as to be immune to uncontrolled user settings

if len(sys.argv)!=3 :
	print('usage : python read_makefiles.py <out_file> [ config | rules | srcs ]',file=sys.stderr)
	sys.exit(1)

out_file                            = sys.argv[1]
action = os.environ['LMAKE_ACTION'] = sys.argv[2]

import lmake     # import before user code to be sure user did not play with sys.path
import serialize

from fmt_rule import fmt_rule

lmake.user_environ = eval(open('LMAKE/user_environ').read()) # make original user env available while reading config
pdict              = lmake.pdict

sys.path[1:2] = [] # suppress access to _lib

def has_submodule(mod,sub) :
	if not mod.__package__ : return False # if not a package, we dont even need to import importlib.util
	from importlib.util import find_spec
	return find_spec(f'{mod.__name__}.{sub}')

gen = {action}

import Lmakefile
if action=='config' :
	config = Lmakefile                              # as long as we do not import a specific module, config is defined here
	if callable(getattr(Lmakefile,'config',None)) :
		Lmakefile.config()
	elif has_submodule(Lmakefile,'config') :
		from Lmakefile import config
	if lmake._rules   : gen.add('rules'  )
	if lmake.manifest : gen.add('sources')
elif action in ('rules','sources') :
	if callable(getattr(Lmakefile,action,None)) :
		getattr(Lmakefile,action)()
	elif has_submodule(Lmakefile,action) :
		from importlib import import_module
		import_module(f'Lmakefile.{action}')
	elif action=='sources' :
		import lmake.auto_sources
else :
	raise RuntimeError(f'unexpected action {action}')

def handle_config(config) :
	config.has_split_rules = 'rules'   not in gen
	config.has_split_srcs  = 'sources' not in gen
	if 'backends' not in config : return
	bes = config['backends']
	for be in bes.values() :
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

if 'config' in gen :
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
	if 'config' in gen :
		print(f"{sep(0)}'config' : {{",file=out) ;
		kl  = max((len(repr(k)) for k in lmake.config.keys()),default=0)
		for k,v in lmake.config.items() :
			print(f'{sep(1)}{k!r:{kl}} : {v!r}',file=out)
		print('\t}',file=out)
	#
	if 'rules' in gen :
		import fmt_rule
		fmt_rule.no_imports = { r.__module__ for r in lmake._rules } # transport by value all modules that contain a rule
		print(f"{sep(0)}'rules' : (",file=out)
		for r in lmake._rules :
			rule = fmt_rule.fmt_rule(r)
			if not rule : continue
			print(f'{sep(1)}{{',file=out)
			kl = max((len(repr(k)) for k in rule.keys()),default=0)
			for k,v in rule.items() :
				print(f'{sep(2)}{k!r:{kl}} : {v!r}',file=out)
			print('\t\t}',file=out)
		print(f'{tuple_end(1)})',file=out)
	#
	if 'sources' in gen :
		print(f"{sep(0)}'manifest' : (",file=out)
		for src in lmake.manifest :
			print(f'{sep(1)}{src!r}',file=out)
		print(f'{tuple_end(1)})',file=out)
	#
	print('}',file=out)
