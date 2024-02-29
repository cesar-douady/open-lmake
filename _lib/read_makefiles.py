# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys

from importlib import import_module
from os        import getcwd
from os.path   import abspath,dirname,relpath
import re

lmake_dir  = dirname(dirname(__file__))
root_dir   = getcwd()
in_repo_re = fr'({re.escape(root_dir)}/|[^/]).*'

sys.implementation.cache_tag = None                                               # dont read pyc files as our rudimentary dependency mechanism will not handle them properly
sys.dont_write_bytecode      = True                                               # and dont generate them
sys.path                     = [lmake_dir+'/lib',lmake_dir+'/_lib','.',*sys.path] # ensure we have safe entries in front so as to be immune to uncontrolled user settings

import lmake     # import before user code to be sure user did not play with sys.path
import serialize
pdict = lmake.pdict

sys.path = [sys.path[0],*sys.path[2:]] # suppress access to _lib

if len(sys.argv)!=4 :
	print('usage : python read_makefiles.py <out_file> [ config | rules | srcs ] <module>',file=sys.stderr)
	sys.exit(1)

out_file =               sys.argv[1]
action   =               sys.argv[2]
module   = import_module(sys.argv[3])

# helper constants
StdAttrs = {
	#                       type   dynamic
	'job_name'          : ( str   , False )
,	'name'              : ( str   , False )
,	'prio'              : ( float , False )
,	'stems'             : ( dict  , False )
,	'targets'           : ( dict  , False )
,	'allow_stderr'      : ( bool  , True  )
,	'autodep'           : ( str   , True  )
,	'auto_mkdir'        : ( bool  , True  )
,	'backend'           : ( str   , True  )
,	'chroot'            : ( str   , True  )
,	'cache'             : ( str   , True  )
,	'cmd'               : ( str   , True  )                    # when it is a str, such str may be dynamic, i.e. it may be a full f-string
,	'cwd'               : ( str   , True  )
,	'dep_flags'         : ( dict  , True  )
,	'deps'              : ( dict  , True  )
,	'environ_cmd'       : ( dict  , True  )
,	'environ_resources' : ( dict  , True  )
,	'environ_ancillary' : ( dict  , True  )
,	'ete'               : ( float , False )
,	'force'             : ( bool  , False )
,	'ignore_stat'       : ( bool  , True  )
,	'job_tokens'        : ( int   , True  )
,	'keep_tmp'          : ( bool  , True  )
,	'kill_sigs'         : ( tuple , True  )
,	'max_stderr_len'    : ( int   , True  )
,	'n_retries'         : ( int   , True  )
,	'n_tokens'          : ( int   , False )
,	'order'             : ( list  , False )
,	'python'            : ( tuple , False )
,	'resources'         : ( dict  , True  )
,	'shell'             : ( tuple , False )
,	'start_delay'       : ( float , True  )
,	'target_flags'      : ( dict  , True  )
,	'timeout'           : ( float , True  )
,	'tmp'               : ( str   , True  )
,	'use_script'        : ( bool  , True  )
}
Keywords     = {'dep','deps','resources','stems','target','targets'}
DictAttrs    = { k for k,v in StdAttrs.items() if v[0]==dict }
SimpleStemRe = re.compile(r'{\w+}|{{|}}')                      # include {{ and }} to prevent them from being recognized as stem, as in '{{foo}}'
SimpleFstrRe = re.compile(r'^([^{}]|{{|}}|{\w+})*$')           # this means stems in {} are simple identifiers, e.g. 'foo{a}bar but not 'foo{a+b}bar'
SimpleStrRe  = re.compile(r'^([^{}]|{{|}})*$'      )           # this means string has no variable parts

def update_dct(acc,new,paths=None,prefix=None) :
	sav = acc.copy()
	acc.clear()
	for k,v in new.items() :
		if v is None :
			sav.pop(k,None)
			continue                                                                                # None is used to suppress entries
		if paths :
			pk = prefix+'.'+k
			if pk in paths and sav.get(k) is not None :
				sep    = paths[pk]
				acc[k] = re.sub(fr'(?<={sep})\.\.\.(?={sep})',sav[k],sep+v+sep)[len(sep):-len(sep)] # add seps before and after, then remove them because look-behind must be of fixed length
				continue
		acc[k] = v
	for k,v in sav.items() : acc.setdefault(k,v)
def update_set(acc,new) :
	if not isinstance(new,(list,tuple,set)) : new = (new,)
	for k in new :
		if isinstance(k,str) and k and k[0]=='-' : acc.discard(k[1:])
		else                                     : acc.add    (k    )
def update_lst(acc,new) :
	if not isinstance(new,(list,tuple,set)) : new = (new,)
	acc[0:0] = new
def update(acc,new,paths=None,prefix=None) :
	if   callable  (acc     ) : acc = top(new)
	elif callable  (new     ) : acc = top(new)
	elif isinstance(acc,dict) : update_dct(acc,new,paths,prefix)
	elif isinstance(acc,set ) : update_set(acc,new             )
	elif isinstance(acc,list) : update_lst(acc,new             )
	else                      : raise TypeError(f'cannot combine {acc.__class__.__name__} and {new.__class__.__name__}')
	return acc
def top(new) :
	if   callable  (new     ) : return new
	elif isinstance(new,dict) : acc = {}
	elif isinstance(new,set ) : acc = set()
	elif isinstance(new,list) : acc = []
	else                      : raise TypeError(f'cannot combine {new.__class__.__name__}')
	return update(acc,new)

def _qualify_key(kind,key,strong,is_python,seen) :
	if not isinstance(key,str) : raise TypeError (f'{kind} key {key} is not a str'               )
	if key in seen             : raise ValueError(f'{kind} key {key} already seen as {seen[key]}')
	seen[key] = kind
	if strong :
		if not key.isidentifier() : raise ValueError(f'{kind} key {key} is not an identifier')
		if key in Keywords        : raise ValueError(f'{kind} key {key} is a lmake keyword'  )
		if is_python :
			try                : exec(f'{key}=None',{})
			except SyntaxError : raise ValueError(f'{kind} key {key} is a python keyword')
def qualify(attrs,is_python) :
	seen = {}
	for k in attrs.stems       .keys() : _qualify_key('stem'        ,k,True,is_python,seen)
	for k in attrs.targets     .keys() : _qualify_key('target'      ,k,True,is_python,seen)
	if attrs.__special__ : return
	for k in attrs.target_flags.keys() : _qualify_key('target_flags',k,True,is_python,seen)
	for k in attrs.dep_flags   .keys() : _qualify_key('dep_flags'   ,k,True,is_python,seen)
	for key in ('dep','resource') :
		dct = attrs[key+'s']
		if callable(dct) : continue
		for k in dct.keys() : _qualify_key(key,k,key=='dep',is_python,seen)

def handle_inheritance(rule) :
	# acquire rule properties by fusion of all info from base classes
	combine = set()
	paths   = {}
	dct     = pdict(cmd=[])                                                 # cmd is handled specially
	# special case for cmd : it may be a function or a str, and base classes may want to provide 2 versions.
	# in that case, the solution is to attach a shell attribute to the cmd function to contain the shell version
	is_python = callable(getattr(rule,'cmd',None))                          # first determine if final objective is python or shell by searching the closest cmd definition
	try :
		for i,r in enumerate(reversed(rule.__mro__)) :
			d = r.__dict__
			if 'combine' in d :
				for k in d['combine'] :
					if k in dct and k not in combine : dct[k] = top(dct[k]) # if an existing value becomes combined, it must be uniquified as it may be modified by further combine's
				combine = update(combine,d['combine'])                      # process combine first so we use the freshest value
			if 'paths' in d : paths = update(paths,d['paths'])
			for k,v in d.items() :
				if k.startswith('__') and k.endswith('__') : continue       # do not process standard python attributes
				if k=='combine'                            : continue
				if k=='cmd' :                                               # special case cmd that has a very special behavior to provide base classes adapted to both python & shell cmd's
					if is_python :
						if not callable(v) : raise TypeError(f'{r.__name__}.cmd is not callable for python rule {rule.__name__}')
					else :
						if callable(v) and hasattr(v,'shell') : v = v.shell
						if not isinstance(v,str)              : raise TypeError(f'{r.__name__}.cmd is not a str for shell rule {rule.__name__}')
					dct[k].append(v)
				elif k in combine :
					if k in dct : dct[k] = update(dct[k],v,paths,k)
					else        : dct[k] = top   (       v        )         # make a fresh copy as it may be modified by further combine's
				else :
					dct[k] = v
	except Exception as e :
		e.base  = r
		e.field = k
		raise
	# reformat dct
	attrs = pdict()
	for k,v in dct.items() :
		if k in StdAttrs and k!='cmd' :                                     # special case cmd
			if v is None : continue                                         # None is not transported
			typ,dyn = StdAttrs[k]
			if typ and not ( dyn and callable(v) ) :
				try :
					if typ in (tuple,list) and not isinstance(v,(tuple,list)) : v = typ((v,))
					else                                                      : v = typ( v  )
				except :
					raise TypeError(f'bad format for {k} : cannot be converted to {typ.__name__}')
		attrs[k] = v
	attrs.name        = rule.__dict__.get('name',rule.__name__)             # name is not inherited as it must be different for each rule and defaults to class name
	attrs.__special__ = rule.__special__
	attrs.is_python   = is_python
	qualify(attrs,is_python)
	return attrs

def find_static_stems(job_name) :
	stems = set()
	state = 'Literal'
	key   = ''
	depth = 0
	for c in job_name :
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
		if key and not key.endswith('*') :
			if not key.isidentifier() : raise ValueError(f'key {key} must be an identifier')
			stems.add(key)
		key   = ''
		state = 'Literal'
	if state!='Literal'  :
		if state=='SeenStop' : raise ValueError(f'spurious }} in job_name {job_name}')
		else                 : raise ValueError(f'spurious {{ in job_name {job_name}')
	return stems

def static_fstring(s) :
	'suppress double { and } assuming no variable part'
	prev_c = '*'
	res    = ''
	for c in s :
		if prev_c in '{}' :
			assert c==prev_c
			prev_c = '*'
			continue
		prev_c  = c
		res    += c
	assert prev_c not in '{}'
	return res

def avoid_ctx(name,ctxs) :
	for i in range(sum(len(ks) for ks in ctxs)) :
		n = name + (f'_{i}' if i else '')
		if not any(n in ks for ks in ctxs) : return n
	assert False,f'cannot find suffix to make {name} an available name'

class Handle :
	def __init__(self,rule) :
		attrs         = handle_inheritance(rule)
		module        = sys.modules[rule.__module__]
		self.rule     = rule
		self.attrs    = attrs
		self.glbs     = (attrs,module.__dict__)
		self.rule_rep = pdict( { k:attrs[k] for k in ('name','stems') } )
		if 'prio' in attrs : self.rule_rep.prio = attrs.prio

	def _init(self) :
		self.static_val  = pdict()
		self.dynamic_val = pdict()

	def _is_simple_fstr(self,fstr) :
		return SimpleFstrRe.match(fstr) and all( k in ('{{','}}') or k[1:-1] in self.per_job for k in SimpleStemRe.findall(fstr) )

	def _fstring(self,x,mk_fstring=True,for_deps=False) :
		if callable(x) : return True,x
		if isinstance(x,(tuple,list,set)) :
			res_id  = False
			res_val = []
			first = True
			for c in x :
				id,v = self._fstring(c,mk_fstring,first and for_deps) # only transmit for_deps to first item of dep when it is a tuple
				first   = False
				res_id |= id
				res_val.append(v)
			return res_id,tuple(res_val)
		if isinstance(x,dict) :
			res_id  = False
			res_dct = {}
			for key,val in x.items() :
				id_k,k = self._fstring(key,False     )
				id_v,v = self._fstring(val,mk_fstring)
				res_id |= id_k or id_v
				res_dct[k] = v
			return res_id,res_dct
		if not mk_fstring  or not isinstance(x,str) :
			return False,x
		if for_deps :
			if self._is_simple_fstr(x) :
				return False,x
		else :
			if SimpleStrRe.match(x)  :
				return False,static_fstring(x)                        # v has no variable parts, can be interpreted statically as an f-string
		return True,serialize.f_str(x)                                # x is made an f-string

	def _handle_val(self,key,rep_key=None,for_deps=False) :
		if not rep_key               : rep_key = key
		if rep_key not in self.attrs : return
		val = self.attrs[rep_key]
		if rep_key in DictAttrs :
			if callable(val) :
				self.dynamic_val[key] = val
				return
			sv = {}
			dv = {}
			for k,v in val.items() :
				id_k,k = self._fstring(k,False                  )
				id_v,v = self._fstring(v      ,for_deps=for_deps)
				if   id_k or id_v : dv[k],sv[k] = self._fstring(v)[1],None # static_val must have an entry for each dynamic one, simple dep stems are only interpreted by engine if static
				else              : sv[k]       = v
			if sv : self.static_val [key] = sv
			if dv : self.dynamic_val[key] = dv
		else :
			id,v = self._fstring(val)
			if id : self.dynamic_val[key] = v
			else  : self.static_val [key] = v

	def _finalize(self) :
		static_val  = self.static_val
		dynamic_val = self.dynamic_val
		del self.static_val
		del self.dynamic_val
		if not dynamic_val : return (static_val,)
		code,ctx,names,dbg = serialize.get_expr(
			dynamic_val
		,	ctx            = ( self.per_job , self.aggregate_per_job , *self.glbs )
		,	no_imports     = in_repo_re                                             # non-static deps are forbidden, transport all local modules by value
		,	call_callables = True
		)
		return ( static_val , tuple(names) , ctx , code )

	def handle_matches(self) :
		if 'target' in self.attrs : self.attrs.targets['<stdout>'] = self.attrs.pop('target')
		#
		def fmt(k,kind,val) :
			if   isinstance(val,str         ) : return (val   ,kind        )
			elif isinstance(val,(list,tuple)) : return (val[0],kind,val[1:])
			raise TypeError(f'bad {kind} {k} : {val}')
		#
		d = {
			**{ k:fmt(k,'target'      ,t) for k,t in self.attrs.targets     .items() }
		,	**{ k:fmt(k,'target_flags',t) for k,t in self.attrs.target_flags.items() }
		,	**{ k:fmt(k,'dep_flags'   ,t) for k,t in self.attrs.dep_flags   .items() }
		}
		if self.attrs.order : # reorder d
			d2 = {}
			for k in self.attrs.order : d2[k] = d[k]
			for k,v in d.items()      : d2[k] = v
			d = d2
		self.rule_rep.matches = d

	def handle_job_name(self) :
		matches = self.rule_rep.matches
		if   'job_name' in self.attrs : self.rule_rep.job_name = self.attrs.job_name    # if job name is specified, use it
		elif '<stdout>' in matches    : self.rule_rep.job_name = matches['<stdout>'][0] # if we have a stdout, use it
		else :                                                                          # use first target, the most specific one
			for t in matches.values() :
				if t[1]=='target' :
					self.rule_rep.job_name = t[0]
					break
			else : assert False,f'cannot find a suitable job_name for {self.rule_rep.name}'

	def prepare_jobs(self) :
		self.static_stems = find_static_stems(self.rule_rep.job_name)
		self.aggregate_per_job = {'stems','target','targets'}
		self.per_job = {
			*self.static_stems
		,	*( k for k in self.rule_rep.matches.keys() if k.isidentifier() )
		}
		#
		if True                     : self.rule_rep.interpreter = self.attrs.python if self.attrs.is_python else self.attrs.shell
		if 'cwd'      in self.attrs : self.rule_rep.cwd         =      self.attrs.cwd
		if 'force'    in self.attrs : self.rule_rep.force       = bool(self.attrs.force   )
		if 'n_tokens' in self.attrs : self.rule_rep.n_tokens    =      self.attrs.n_tokens

	def handle_create_none(self) :
		self._init()
		self._handle_val('job_tokens')
		self.rule_rep.create_none_attrs = self._finalize()

	def handle_cache_none(self) :
		self._init()
		self._handle_val('key','cache')
		self.rule_rep.cache_none_attrs = self._finalize()

	def handle_deps(self) :
		if 'dep' in self.attrs : self.attrs.deps['<stdin>'] = self.attrs.pop('dep')
		self._init()
		self._handle_val('deps',for_deps=True)
		if 'deps' in self.dynamic_val : self.dynamic_val = self.dynamic_val['deps']
		if 'deps' in self.static_val  : self.static_val  = self.static_val ['deps']
		if callable(self.dynamic_val) :
			assert not self.static_val                                                                                  # there must be no static val when deps are full dynamic
			self.static_val  = None                                                                                     # tell engine deps are full dynamic (i.e. static val does not have the dep keys)
		self.rule_rep.deps_attrs = self._finalize()
		# once deps are evaluated, they are available for others
		self.aggregate_per_job.add('deps')
		if self.rule_rep.deps_attrs[0] : self.per_job.update({ k for k in self.attrs.deps.keys() if k.isidentifier() }) # special cases are not accessible from f-string's

	def handle_submit_rsrcs(self) :
		self._init()
		self._handle_val('backend'            )
		self._handle_val('rsrcs'  ,'resources')
		self.rule_rep.submit_rsrcs_attrs = self._finalize()
		self.aggregate_per_job.add('resources')
		rsrcs = self.rule_rep.submit_rsrcs_attrs[0].get('rsrcs',{})
		if not callable(rsrcs) : self.per_job.update(set(rsrcs.keys()))

	def handle_submit_none(self) :
		self._init()
		self._handle_val ('n_retries')
		self.rule_rep.submit_none_attrs = self._finalize()

	def handle_start_cmd(self) :
		self._init()
		self._handle_val('auto_mkdir'               )
		self._handle_val('env'        ,'environ_cmd')
		self._handle_val('ignore_stat'              )
		self._handle_val('chroot'                   )
		self._handle_val('interpreter'              )
		self._handle_val('tmp'                      )
		self._handle_val('use_script'               )
		self.rule_rep.start_cmd_attrs = self._finalize()

	def handle_start_rsrcs(self) :
		self._init()
		self._handle_val('autodep'                    )
		self._handle_val('env'    ,'environ_resources')
		self._handle_val('timeout'                    )
		self.rule_rep.start_rsrcs_attrs = self._finalize()

	def handle_start_none(self) :
		if not callable(self.attrs.kill_sigs) : self.attrs.kill_sigs = [int(x) for x in self.attrs.kill_sigs]
		self._init()
		self._handle_val('keep_tmp'                       )
		self._handle_val('start_delay'                    )
		self._handle_val('kill_sigs'                      )
		self._handle_val('n_retries'                      )
		self._handle_val('env'        ,'environ_ancillary')
		self.rule_rep.start_none_attrs = self._finalize()

	def handle_end_cmd(self) :
		self._init()
		self._handle_val('allow_stderr')
		self.rule_rep.end_cmd_attrs = self._finalize()

	def handle_end_none(self) :
		self._init()
		self._handle_val('max_stderr_len')
		self.rule_rep.end_none_attrs = self._finalize()

	def handle_cmd(self) :
		self.rule_rep.is_python = self.attrs.is_python
		if self.attrs.is_python :
			cmd_ctx       = set()
			serialize_ctx = (self.per_job,self.aggregate_per_job,*self.glbs)
			cmd           = self.attrs.cmd
			multi         = len(cmd)>1
			if multi :
				cmd_lst = []
				cmd_idx = 0
				for ci,c in enumerate(cmd) :
					# create a copy of c with its name modified (dont modify in place as this would be visible for other rules inheriting from the same parent)
					cc                 = c.__class__( c.__code__ , c.__globals__ , avoid_ctx(f'cmd{ci}',serialize_ctx) , c.__defaults__ , c.__closure__ )
					cc.__annotations__ = c.__annotations__
					cc.__kwdefaults__  = c.__kwdefaults__
					cc.__module__      = c.__module__
					cc.__qualname__    = c.__qualname__
					cmd_lst.append(cc)
				cmd = cmd_lst
			decorator = avoid_ctx('lmake_func',serialize_ctx)
			cmd , cmd_ctx , dbg_info = serialize.get_src(
				*cmd
			,	ctx        = serialize_ctx
			,	no_imports = rule_modules
			,	force      = True
			,	decorator  = decorator
			,	root_dir   = root_dir
			)
			if multi :
				cmd += 'def cmd() : \n'
				x = avoid_ctx('x',serialize_ctx) # find a non-conflicting name
				for i,c in enumerate(cmd_lst) :
					a = '' if c.__code__.co_argcount==0 else 'None' if i==0 else x
					if   i==len(self.attrs.cmd)-1          : cmd += f'\treturn {c.__name__}({a})\n'
					elif cmd_lst[i+1].__code__.co_argcount : cmd += f'\t{a} = { c.__name__}({a})\n'
					else                                   : cmd += f'\t{       c.__name__}({a})\n'
			self.rule_rep.cmd      = ( {'cmd':cmd,'decorator':decorator} , tuple(cmd_ctx) )
			self.rule_rep.dbg_info = dbg_info
		else :
			self.attrs.cmd = cmd = '\n'.join(self.attrs.cmd)
			self._init()
			self._handle_val('cmd',for_deps=True)
			if 'cmd' in self.dynamic_val : self.dynamic_val = self.dynamic_val['cmd']
			self.rule_rep.cmd = self._finalize()

def fmt_rule(rule) :
	if rule.__dict__.get('virtual',False) : return                                                                   # with an explicit marker, this is definitely a base class
	#
	h = Handle(rule)
	#
	h.handle_matches()
	if all(m[1]!='target' for m in h.rule_rep.matches.values()) :                                                    # if there is no way to match this rule, must be a base class
		if not rule.__dict__.get('virtual',True) : raise ValueError('no matching target while virtual forced False')
		return
	h.handle_job_name()
	#
	# handle cases with no execution
	if rule.__special__ :
		h.rule_rep.__special__ = rule.__special__
		return h.rule_rep
	# plain case
	if not getattr(h.attrs,'cmd',None) :                                                                             # Rule must have a cmd, or it is a base class
		if not rule.__dict__.get('virtual',True) : raise ValueError('no cmd while virtual forced False')
		return
	#
	h.prepare_jobs()
	#
	h.handle_create_none ()
	h.handle_cache_none  ()
	h.handle_deps        ()
	h.handle_submit_rsrcs()
	h.handle_submit_none ()
	h.handle_start_cmd   ()
	h.handle_start_rsrcs ()
	h.handle_start_none  ()
	h.handle_end_cmd     ()
	h.handle_end_none    ()
	h.handle_cmd         ()
	return h.rule_rep

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

def handle_config(config) :
	if 'backends' not in config : return
	bes = config['backends']
	for be in bes.values() :
		if 'interface' not in be : continue
		code,ctx,names,dbg = serialize.get_expr(
			be['interface']
		,	ctx            = (module.__dict__,)
		,	call_callables = True
		)
		be['interface'] = ctx+'interface = '+code

rule_modules = { r.__module__ for r in lmake._rules }

handle_config(lmake.config)

rules = [ fmt_rule_chk(r) for r in lmake._rules      ]
rules = [              r  for r in rules        if r ]

# generate output
# could be a mere print, but it is easier to debug with a prettier output
# XXX : write a true pretty printer

def error(txt='') :
	print(txt,file=sys.stderr)
	exit(2)

if not lmake.manifest and 'sources_module' not in lmake.config : lmake.config.sources_module = 'lmake.auto_sources'

gen_config = action=='config'
gen_rules  = action=='rules'
gen_srcs   = action=='srcs'
if gen_config :
	if   not lmake.config.get('rules_module')   : gen_rules = True
	elif rules                                  : error('cannot set lmake.config.rules_module and define rules'   )
	if   not lmake.config.get('sources_module') : gen_srcs  = True
	elif lmake.manifest                         : error('cannot set lmake.config.sources_module and lmake.sources')

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

with open(sys.argv[1],'w') as out :
	print('{',file=out)
	#
	if gen_config :
		print(f"{sep(0)}'config' : {{",file=out) ;
		kl  = max((len(repr(k)) for k in lmake.config.keys()),default=0)
		for k,v in lmake.config.items() :
			print(f'{sep(1)}{k!r:{kl}} : {v!r}',file=out)
		print('\t}',file=out)
	#
	if gen_rules :
		print(f"{sep(0)}'rules' : (",file=out)
		for rule in rules :
			print(f'{sep(1)}{{',file=out)
			kl   = max((len(repr(k)) for k in rule.keys()),default=0)
			for k,v in rule.items() :
				print(f'{sep(2)}{k!r:{kl}} : {v!r}',file=out)
			print('\t\t}',file=out)
		print(f'{tuple_end(1)})',file=out)
	#
	if gen_srcs :
		print(f"{sep(0)}'manifest' : (",file=out)
		for src in lmake.manifest :
			print(f'{sep(1)}{src!r}',file=out)
		print(f'{tuple_end(1)})',file=out)
	#
	print('}',file=out)
