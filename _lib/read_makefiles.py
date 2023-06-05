# This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import os.path as osp
import sys
import re

lmake_s_dir = osp.dirname(osp.dirname(__file__))

save_path = list(sys.path)

sys.implementation.cache_tag = None
sys.dont_write_bytecode      = True
sys.path[0:0]                = [lmake_s_dir+'/lib','.']                        # lmake_s_dir is provided in global dict by _bin/read_makefiles
sys.reading_makefiles        = True                                            # signal we are reading makefiles to define the proper primitives

import lmake                                                                   # import before user code to be sure user did not play with sys.path
import Lmakefile
pdict = lmake.pdict

sys.path = [lmake_s_dir+'/_lib']+save_path                                     # restore sys.path in case user played with it
import serialize

# helper constants
# AntiRule's only need a subset of plain rule attributes as there is no execution
StdAntiAttrs = {
	'job_name'     : str
,	'name'         : str
,	'post_targets' : dict
,	'prio'         : float
,	'targets'      : dict
,	'stems'        : dict
}
StdExecAttrs = {
	'allow_stderr' : bool
,	'autodep'      : str
,	'auto_mkdir'   : bool
,	'backend'      : str
,	'chroot'       : str
,	'cmd'          : None
,	'deps'         : dict
,	'environ'      : dict
,	'ete'          : float
,	'force'        : bool
,	'ignore_stat'  : bool
,	'keep_tmp'     : bool
,	'kill_sigs'    : tuple
,	'n_tokens'     : int
,	'python'       : tuple
,	'resources'    : dict
,	'shell'        : tuple
,	'start_delay'  : float
,	'stderr_len'   : int
,	'timeout'      : float
,	'job_tokens'   : str
}
Keywords     = {'dep','deps','resource','resources','stems','target','targets','job_tokens'}
StdAttrs     = { **StdAntiAttrs , **StdExecAttrs }
SimpleStemRe = re.compile(r'{\w+}')
SimpleDepRe  = re.compile(r'^([^{}]|{{|}})*({\w+}([^{}]|{{|}})*)*$')           # this means stems in {} are simple identifiers, e.g. 'foo{a}bar but not 'foo{a+b}bar'

def update_dict(acc,new) :
	for k,v in new.items() :
		acc.pop(k,None)                                                        # ensure entry is put at the end of dict order
		if v is not None : acc[k] = v                                          # None is used to suppress entries
def update_list(acc,new) :
	if not isinstance(new,(list,tuple,set)) : new = (new,)
	acc += new
def update_set(acc,new) :
	if not isinstance(new,(list,tuple,set)) : new = (new,)
	for k in new :
		if isinstance(k,str) and len(k) and k[0]=='-' : acc.discard(k[1:])
		else                                          : acc.add    (k    )
def update(acc,new) :
	if   isinstance(acc,dict) : update_dict(acc,new)
	elif isinstance(acc,list) : update_list(acc,new)
	elif isinstance(acc,set ) : update_set (acc,new)
	else                      : raise TypeError(f'cannot combine {acc.__class__.__name__} and {new.__class__.__name__}')
def top(new) :
	if   isinstance(new,dict        ) : acc = {}
	elif isinstance(new,(list,tuple)) : acc = []
	elif isinstance(new,set         ) : acc = set()
	else                              : raise TypeError(f'cannot combine {new.__class__.__name__}')
	update(acc,new)
	return acc

def qualify_key(kind,key,seen) :
	if not isinstance(key,str) : raise TypeError(f'{kind} key {key} is a str'                   )
	if not key.isidentifier()  : raise TypeError(f'{kind} key {key} is not an identifier'       )
	if key in Keywords         : raise TypeError(f'{kind} key {key} is a reserved keyword'      )
	if key in seen             : raise TypeError(f'{kind} key {key} already seen as {seen[key]}')
	seen[key] = kind

def mk_snake(txt) :
	res = []
	start_of_word = True ;
	for c in txt :
		if   not c.isupper() : res.append(    c        )
		elif start_of_word   : res.append(    c.lower())
		else                 : res.append('_'+c.lower())
		start_of_word = not c.isalnum()
	return ''.join(res)

def fmt_target(target) :
	if       isinstance(target,str         ) : flags = ()
	elif not isinstance(target,(tuple,list)) : raise TypeError(f'bad format for target {k} of type {target.__class__.__name__}')
	elif not target                          : raise TypeError(f'cannot find target {k} in empty entry')
	elif not isinstance(target[0],str)       : raise TypeError(f'bad format for target {k} of type {target[0].__class__.__name__}')
	else                                     : target,flags = target[0],tuple(mk_snake(f) for f in target[1:] if f)
	return (target,*flags)

def no_match(target) :
	return '-match' in target[1:]

def qualify(rule_rep) :
	seen = {}
	for k in rule_rep.stems       .keys() : qualify_key('stem'       ,k,seen)
	for k in rule_rep.targets     .keys() : qualify_key('target'     ,k,seen)
	for k in rule_rep.post_targets.keys() : qualify_key('post_target',k,seen)
	if not rule_rep.__anti__ :
		for k in rule_rep.deps     .keys() : qualify_key('dep'     ,k,seen)
		for k in rule_rep.resources.keys() : qualify_key('resource',k,seen)

def handle_inheritance(rule) :
	# acquire rule properties by fusion of all info from base classes
	combine = set()
	dct     = pdict()
	# special case for cmd : it may be a function or a str, and base classes may want to provide 2 versions.
	# in that case, the solution is to attach a shell attribute to the cmd function to contain the shell version
	is_shell = isinstance(getattr(rule,'cmd',None),str)
	try :
		for i,r in enumerate(reversed(rule.__mro__)) :
			d = r.__dict__
			if 'combine' in d :
				for k in d['combine'] :
					if k in dct and k not in combine : dct[k] = top(dct[k]) # if an existing value becomes combined, it must be uniquified as it may be modified by further combine's
				update(combine,d['combine'])                                # process combine first so we use the freshest value
			for k,v in d.items() :
				if k.startswith('__') and k.endswith('__')                      : continue # do not process standard python attributes
				if k=='combine'                                                 : continue
				if k=='cmd' and callable(v) and is_shell and hasattr(v,'shell') : v = v.shell
				if k in combine :
					if k in dct : update(dct[k],v)
					else        : dct[k] = top(v)                              # make a fresh copy as it may be modified by further combine's
				else :
					dct[k] = v
	except Exception as e :
		e.base  = r
		e.field = k
		raise
	# reformat dct and split into rule_rep for the standard part and attrs for the rest
	rule_rep = pdict()
	attrs    = pdict()
	for k,v in dct.items() :
		if k in StdAttrs :
			if v is None : continue                                            # None is not transported
			if StdAttrs.get(k) :
				try    : v = StdAttrs[k](v)
				except : raise TypeError(f'bad format for {k} : cannot be converted to {StdAttrs[k].__name__}')
			rule_rep[k] = v
		else :
			attrs[k] = v
	rule_rep.name     = rule.__dict__.get('name',rule.__name__)                # name is not inherited as it must be different for each rule and defaults to class name
	rule_rep.__anti__ = rule.__anti__
	qualify(rule_rep)
	return rule_rep,attrs

def handle_cwd(rule,rule_rep) :
	try :
		if 'cwd' not in rule_rep :
			rule_rep.cwd = lmake.search_root_dir(sys.modules[rule.__module__].__file__)
	except :
		raise RuntimeError('cannot determine cwd')

def find_job_name(rule,rule_rep) :
	assert 'job_name' not in rule_rep
	if '<stdout>' in rule_rep.targets : return '<stdout>'                      # if we have a stdout, this is an excellent job_name
	for r in rule.__mro__ :                                                    # find the first clean target of the most specific class that has one
		for k,t in r.__dict__.get('targets',{}).items() :
			if not no_match(rule_rep.targets[k]) : return k                    # no_match targets are not good names : they may be ambiguous and they are not the focus of the user
	for k,t in rule_rep.targets.items() :                                      # find anything, a priori a post_target
		if not no_match(t) : return k
	assert False,f'cannot find adequate target to name jobs of {rule.__name__}' # we know we have clean targets, we should have found a job_name

# /!\ : this function is also implemented in rule.cc:_parse_py, both must stay in sync
def add_stems(stems,target) :
	state = 'Literal'
	key   = ''
	depth = 0
	for c in target :
		with_re = False
		if state=='Literal' :
			if   c=='{' : state = 'SeenStart'
			elif c=='}' : state = 'SeenStop'
			continue
		if state=='SeenStop' :
			if c!='}' : break
			state = 'Literal'
			continue
		if state=='SeenStart' :
			if c=='{' :
				state = 'Literal'
				continue
			state = 'Key'
		if state=='Key' :
			if c!='}' :
				if c==':' : state  = 'Re'
				else      : key   += c
				continue
		if state=='Re' :
			if not( c=='}' and depth==0 ) :
				if   c=='{' : depth += 1
				elif c=='}' : depth -= 1
				continue
		key = key.strip()
		if key.endswith('*') : key = key[:-1]
		if key :
			if not key.isidentifier() : raise ValueError(f'key {key} must be an identifier in {target}')
			stems.add(key)
		key   = ''
		state = 'Literal'
	if state!='Literal'  :
		if state=='SeenStop' : raise ValueError(f'spurious }} in {target}')
		else                 : raise ValueError(f'spurious {{ in {target}')
def find_stems(rule_rep) :
	stems = set()
	add_stems(stems,rule_rep.job_name)
	for t in rule_rep.targets.values() :
		add_stems(stems,t[0])
	return stems

def handle_targets(rule_rep,attrs) :
	if 'target' in attrs and 'post_target' in attrs : raise ValueError('cannot specify both target and post_target')
	if   'target'      in attrs                     : rule_rep.targets     ['<stdout>'] = attrs.pop('target'     )
	elif 'post_target' in attrs                     : rule_rep.post_targets['<stdout>'] = attrs.pop('post_target')
	bad_keys = set(rule_rep.targets) & set(rule_rep.post_targets)
	if bad_keys : raise ValueError(f'{bad_keys} are defined both as target and post_target')
	#
	rule_rep.targets = {
		**{ k:fmt_target(t) for k,t in               rule_rep.targets     .items()   }
	,	**{ k:fmt_target(t) for k,t in reversed(list(rule_rep.post_targets.items())) }
	}
	rule_rep.pop('post_targets')
	#
	return not all(no_match(t) for t in rule_rep.targets.values())             # if there is no way to match this rule, must be a base class

def dep_code(rule_rep,kind,key,dep) :
	if SimpleDepRe.match(dep) and all( k[1:-1] in rule_rep.stems for k in SimpleStemRe.findall(dep) ) :
		return dep,None                                                        # this can be interpreted by the engine without resorting to f-string interpretation
	else :
		if   "'"   not in dep and '\n' not in dep : sep = "'"
		elif '"'   not in dep and '\n' not in dep : sep = '"'
		elif "'''" not in dep                     : sep = "'''"
		elif '"""' not in dep                     : sep = '"""'
		else                                      : raise ValueError(f'{kind}{" " if kind else ""}{key} is too complicated an f-string with both \'\'\' and """ in it')
		dep = 'fr'+sep+dep+sep
		return dep,compile(dep,rule_rep.name+'.'+(key or '<stdin>'),'eval') # this is a real f-string

def mk_deps(rule_rep,kind,serialize_ctx,code=None) :
	deps = rule_rep[kind]
	res = pdict(dct={})
	if code : codes = [code]
	else    : codes = []
	for k,d in deps.items() :
		d,code = dep_code(rule_rep,kind,k,d)
		if code : codes.append(code)
		res.dct[k] = (d,bool(code))
	if codes :
		res.prelude , ctx = serialize.get_code_ctx(
			*codes
		,	ctx        = serialize_ctx[0]
		,	no_imports = serialize_ctx[1]
		)
		res.ctx = tuple(ctx)
	return res

def handle_deps(rule_rep,attrs,serialize_ctx) :
	# do some reformatting linked to execution
	#
	if 'dep'      in attrs : rule_rep.deps     ['<stdin>' ] = attrs.pop('dep'     )
	if 'resource' in attrs : rule_rep.resources['resource'] = attrs.pop('resource')
	#
	for k,s in rule_rep.deps.items() :
		if not isinstance(s,str) : raise TypeError(f'bad format for dep {k} of type {s.__class__.__name__}')
	#
	# handle deps & resources
	#
	rule_rep.deps = mk_deps(rule_rep,'deps',serialize_ctx)
	#
	per_job = serialize_ctx[0][0]
	# during processing, job_tokens & resources are handled after deps, these can be added to the evaluation context
	per_job.update({ 'deps' , *( k for k in rule_rep.deps.dct.keys() if k.isidentifier() ) }) # special cases are not accessible from f-string's
	#
	job_tokens,tokens_code = dep_code(rule_rep,'','job_tokens',rule_rep.job_tokens)
	rule_rep.job_tokens    = (job_tokens,bool(tokens_code))
	rule_rep.resources     = { k:str(v) for k,v in rule_rep.resources.items() }
	rule_rep.resources     = mk_deps(rule_rep,'resources',serialize_ctx,tokens_code) # resources context must be able to interpret job_tokens as well
	#
	# for cmd execution, it may be necessary to access job_tokens & resources as well
	per_job.update({ 'job_tokens'                                                                    })
	per_job.update({ 'resources' , *( k for k in rule_rep.resources.dct.keys() if k.isidentifier() ) }) # special cases are not accessible from f-string's

def handle_cmd(rule_rep,serialize_ctx) :
	rule_rep.cmd_ctx = set()
	multi            = len(rule_rep.cmd)>1
	if all( callable(c) for c in rule_rep.cmd ) :
		rule_rep.is_python   = True
		rule_rep.interpreter = rule_rep.pop('python')
		if multi :
			cmd     = []
			cmd_idx = 0
			for c in rule_rep.cmd :
				cmd_idx += 1
				while any( any(y==f'cmd{cmd_idx}' for y in x) for x in serialize_ctx[0] ) : cmd_idx += 1 # protect against user having defined names such as cmd1, cmd2, ...
				# create a copy of c with its name modified (dont modify in place as this would be visible for other rules inheriting from the same parent)
				cmd.append(c.__class__( c.__code__ , c.__globals__ , f'cmd{cmd_idx}' , c.__defaults__ , c.__closure__ ))
			rule_rep.cmd = cmd
		rule_rep.script , rule_rep.cmd_ctx = serialize.get_src(
			*rule_rep.cmd
		,	ctx        = serialize_ctx[0]
		,	no_imports = serialize_ctx[1]
		)
		if multi :
			rule_rep.script += 'def cmd() :\n'
			for i,c in enumerate(rule_rep.cmd) :
				x = '' if c.__code__.co_argcount==0 else 'None' if i==0 else 'x'
				if i<len(rule_rep.cmd)-1 : rule_rep.script += f'\tx =    {c.__name__}({x})\n'
				else                     : rule_rep.script += f'\treturn {c.__name__}({x})\n'
	elif all( isinstance(c,str) for c in rule_rep.cmd ) :
		rule_rep.is_python   = False
		rule_rep.interpreter = rule_rep.pop('shell')
		rule_rep.script      = '\n'.join(rule_rep.cmd)
		per_job = serialize_ctx[0][0]
		for v in per_job :
			if re.search(f'\\b{v}\\b',rule_rep.script) :                       # if {v} is mentioned in script as a word, put it in the context
				rule_rep.cmd_ctx.add(v)
	elif all( callable(c) or isinstance(c,str) for c in rule_rep.cmd ) :
		raise TypeError('cannot mix Python & shell along the inheritance hierarchy')
	else :
		raise TypeError('bad cmd type')
	del rule_rep.cmd
	rule_rep.cmd_ctx = tuple(rule_rep.cmd_ctx)

def handle_env(rule_rep) :
	rule_rep.env = {}
	for k,v in rule_rep.pop('environ').items() :
		if       isinstance(v,str)                             : v = (v,'cmd')
		if not ( isinstance(v   ,(list,tuple)) and len(v)==2 ) : raise TypeError(f'cannot understand environment variable {k} : {v}')
		if not   isinstance(v[1],str         )                 : raise TypeError(f'bad type for environment variable {k} flag : {v}')
		v1 = mk_snake(v[1])
		if v1 not in ('none','resource','cmd') : raise ValueError(f"bad flag for environment variable {k} : {v[1]} not in ('none','resource','cmd')")
		if v1=='resource'                      : v1 = 'rsrc'
		rule_rep.env[k] = (v[0],v1)

def fmt_rule(rule) :
	if rule.__dict__.get('virtual',False) : return                             # with an explicit marker, this is definitely a base class
	#
	rule_rep,attrs = handle_inheritance(rule)
	#
	handle_targets(rule_rep,attrs)
	if all(no_match(t) for t in rule_rep.targets.values()) :                                                         # if there is no way to match this rule, must be a base class
		if not rule.__dict__.get('virtual',True) : raise ValueError('no matching target while virtual forced False')
		return
	#
	if 'job_name' not in rule_rep : rule_rep.job_name = rule_rep.targets[find_job_name(rule,rule_rep)][0]
	#
	# handle cases with no execution
	if rule.__anti__ :
		return pdict({ k:v for k,v in rule_rep.items() if k in StdAntiAttrs })
	if not getattr(rule_rep,'cmd',None) :                                                                # Rule must have a cmd, or it is a base class
		if not rule.__dict__.get('virtual',True) : raise ValueError('no cmd while virtual forced False')
		return
	#
	# prepare serialization
	per_job = {                                                                # this is a set as there are no values as of now : they will be provided at job execution time
		'stems'   , *find_stems(rule_rep)
	,	'targets' , *( k for k in rule_rep.targets.keys() if k.isidentifier() ) # special cases are not accessible from f-string's
	}
	serialize_ctx = ( ( per_job , attrs , sys.modules[rule.__module__].__dict__  ) , {rule.__module__} )
	#
	# handle execution related stuff
	handle_deps(rule_rep,attrs,serialize_ctx)
	handle_cmd (rule_rep,      serialize_ctx)
	handle_env (rule_rep                    )
	rule_rep.kill_sigs = tuple(int(s) for s in rule_rep.kill_sigs)
	#
	return rule_rep

def fmt_rule_chk(rule) :
	try :
		return fmt_rule(rule)
	except Exception as e :
		if hasattr(rule,'name') : name = f'({rule.name})'
		else                    : name = ''
		print(f'while processing {rule.__name__}{name} :',file=sys.stderr)
		if hasattr(e,'field')                  : print(f'\tfor field {e.field}'      ,file=sys.stderr)
		if hasattr(e,'base' ) and e.base!=rule : print(f'\tin base {e.base.__name__}',file=sys.stderr)
		print(f'\t{e.__class__.__name__} : {e}',file=sys.stderr)
		sys.exit(2)

if hasattr(lmake,'sources') : srcs = lmake.sources
else                        : srcs = lmake.auto_sources()

print(repr({
	'local_admin_dir'  : lmake.local_admin_dir
,	'remote_admin_dir' : lmake.remote_admin_dir
,	'config'           : lmake.config
,	'srcs'             : srcs
,	'rules' : [
		rule_rep
		for rule in lmake.rules
		for rule_rep in (fmt_rule_chk(rule),)
		if rule_rep
	]
}),file=open(sys.argv[1],'w'))
