# This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import os.path as osp
import sys
import re

lmake_s_dir = osp.dirname(osp.dirname(__file__))

sys.implementation.cache_tag = None
sys.dont_write_bytecode      = True
save_path                    = list(sys.path)
sys.path                     = [lmake_s_dir+'/_lib',lmake_s_dir+'/lib',*save_path]

sys.reading_makefiles        = True                                            # signal we are reading makefiles to define the proper primitives
import lmake                                                                   # import before user code to be sure user did not play with sys.path
import serialize
pdict = lmake.pdict

sys.path = [lmake_s_dir+'/lib','.',*save_path]
import Lmakefile

sys.path = save_path                                                           # restore sys.path in case user played with it

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
	'allow_stderr'      : bool
,	'autodep'           : str
,	'auto_mkdir'        : bool
,	'backend'           : str
,	'chroot'            : str
,	'cache'             : str
,	'cmd'               : None
,	'deps'              : dict
,	'environ_cmd'       : dict
,	'environ_resources' : dict
,	'environ_ancillary' : dict
,	'ete'               : float
,	'force'             : bool
,	'ignore_stat'       : bool
,	'keep_tmp'          : bool
,	'kill_sigs'         : tuple
,	'n_tokens'          : int
,	'python'            : tuple
,	'resources'         : dict
,	'shell'             : tuple
,	'start_delay'       : float
,	'stderr_len'        : int
,	'timeout'           : float
,	'job_tokens'        : str
}
Keywords     = {'dep','deps','resources','stems','target','targets'}
StdAttrs     = { **StdAntiAttrs , **StdExecAttrs }
SimpleStemRe = re.compile(r'{\w+}')
SimpleDepRe  = re.compile(r'^([^{}]|{{|}})*({\w+}([^{}]|{{|}})*)*$')           # this means stems in {} are simple identifiers, e.g. 'foo{a}bar but not 'foo{a+b}bar'
SimpleStrRe  = re.compile(r'^([^{}]|{{|}})*$')                                 # this means string can be interpreted

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
	if   callable  (acc     ) : acc = top(new)
	elif isinstance(acc,dict) : update_dict(acc,new)
	elif isinstance(acc,list) : update_list(acc,new)
	elif isinstance(acc,set ) : update_set (acc,new)
	else                      : raise TypeError(f'cannot combine {acc.__class__.__name__} and {new.__class__.__name__}')
def top(new) :
	if   callable  (new             ) : return new
	elif isinstance(new,dict        ) : acc = {}
	elif isinstance(new,(list,tuple)) : acc = []
	elif isinstance(new,set         ) : acc = set()
	else                              : raise TypeError(f'cannot combine {new.__class__.__name__}')
	update(acc,new)
	return acc

def qualify_key(kind,key,seen) :
	if not isinstance(key,str) : raise TypeError(f'{kind} key {key} is not a str'               )
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

def fmt_entry(kind,entry) :
	if       isinstance(entry,str         ) : flags = ()
	elif not isinstance(entry,(tuple,list)) : raise TypeError(f'bad format for {kind} {k} of type {entry.__class__.__name__}')
	elif not entry                          : raise TypeError(f'cannot find {kind} {k} in empty entry')
	elif not isinstance(entry[0],str)       : raise TypeError(f'bad format for {kind} {k} of type {entry[0].__class__.__name__}')
	else                                    : entry,flags = entry[0],tuple(mk_snake(f) for f in entry[1:] if f)
	return (entry,*flags)

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
					if k in dct and k not in combine : dct[k] = top(dct[k])    # if an existing value becomes combined, it must be uniquified as it may be modified by further combine's
				update(combine,d['combine'])                                   # process combine first so we use the freshest value
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
			if StdAttrs.get(k) and not callable(v) :
				try    : v = StdAttrs[k](v)
				except : raise TypeError(f'bad format for {k} : cannot be converted to {StdAttrs[k].__name__}')
			rule_rep[k] = v
		else :
			attrs[k] = v
	rule_rep.name     = rule.__dict__.get('name',rule.__name__)                # name is not inherited as it must be different for each rule and defaults to class name
	rule_rep.__anti__ = rule.__anti__
	qualify(rule_rep)
	return rule_rep,attrs

def find_job_name(rule,rule_rep) :
	assert 'job_name' not in rule_rep
	if '<stdout>' in rule_rep.targets : return '<stdout>'                      # if we have a stdout, this is an excellent job_name
	for r in rule.__mro__ :                                                    # find the first clean target of the most specific class that has one
		for k in r.__dict__.get('targets',{}).keys() :
			if not no_match(rule_rep.targets[k]) : return k                    # no_match targets are not good names : they may be ambiguous and they are not the focus of the user
	for k,t in rule_rep.targets.items() :                                      # find anything, a priori a post_target
		if not no_match(t) : return k
	assert False,f'cannot find adequate target to name jobs of {rule.__name__}' # we know we have clean targets, we should have found a job_name

def find_static_stems(rule_rep) :
	stems = set()
	state = 'Literal'
	key   = ''
	depth = 0
	for c in rule_rep.job_name :
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
		if state=='SeenStop' : raise ValueError(f'spurious }}')
		else                 : raise ValueError(f'spurious {{')
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

class NewHandle :
	def __init__(self,rule_rep,glbs,no_import) :
		self.rule_rep      = rule_rep
		self.glbs          = glbs
		self.no_imports    = {no_import}

	def prepare_jobs(self,rule) :
		if 'job_name' not in self.rule_rep : self.rule_rep.job_name = self.rule_rep.targets[find_job_name(rule,self.rule_rep)][0]
		self.stems   = find_static_stems(self.rule_rep)
		self.per_job = {
			'stems'   , *self.stems
		,	'targets' , *( k for k in self.rule_rep.targets.keys() if k.isidentifier() )
		}

	def handle_interpreter(self) :
		if all( callable(c) for c in self.rule_rep.cmd ) :
			self.rule_rep.is_python   = True
			self.rule_rep.interpreter = self.rule_rep.pop('python')
		elif all( isinstance(c,str) for c in self.rule_rep.cmd ) :
			self.rule_rep.is_python   = False
			self.rule_rep.interpreter = self.rule_rep.pop('shell')
		elif all( callable(c) or isinstance(c,str) for c in self.rule_rep.cmd ) :
			raise TypeError('cannot mix Python & shell along the inheritance hierarchy')
		else :
			raise TypeError('bad cmd type')
	def _init(self) :
		self.static_val = {}
		self.code       = []

	@staticmethod
	def _mk_call(c) :
		return c.__name__+()

	def _handle_dict(self,key,rep_key=None) :
		if not rep_key                  : rep_key = key
		if rep_key not in self.rule_rep : return
		dct = self.rule_rep[rep_key]
		if callable(dct) :
			self.code += ' , ',repr(key),':',self._mk_call(dct)
			return
		sv = {}
		c  = []
		for k,v in dct.items() :
			if callable(v)             : c     += ' , ',repr(k),':',self._mk_call(v)
			elif not isinstance(v,str) : sv[k]  = v
			elif SimpleStrRe.match(v)  : sv[k]  = static_fstring(v)
			else                       : c     += ' , ',repr(k),':f',repr(v)
		if sv : self.static_val[key]  = sv
		if c  : self.code            += ( ' , ',repr(key),':{ ',*c[1:],' }' )

	def _handle_str(self,key,rep_key=None) :
		if not rep_key                  : rep_key = key
		if rep_key not in self.rule_rep : return
		s = self.rule_rep[rep_key]
		if   callable(s)          : self.code            += ' , ',repr(key),':',self._mk_call(s)
		elif SimpleStrRe.match(s) : self.static_val[key]  = static_fstring(s)
		else                      : self.code            += ' , ',repr(key),':f',repr(s)

	def _handle_any(self,key,rep_key=None) :
		if not rep_key                  : rep_key = key
		if rep_key not in self.rule_rep : return
		x = self.rule_rep[rep_key]
		if callable(x) : self.code            += ' , ',repr(key),':',self._mk_call(x)
		else           : self.static_val[key]  = x

	def _finalize(self) :
		static_val = self.static_val
		code       = self.code
		del self.static_val
		del self.code
		if not code : return (static_val,)
		if isinstance(code,list) : code  = ''.join(('{ ',*code[1:],' }'))
		ctx,names = serialize.get_code_ctx( compile(code,self.rule_rep.name,'eval') , ctx=(self.per_job,*self.glbs) , no_imports=self.no_imports )
		return ( static_val , ctx , code , tuple(names) )

	def handle_targets(self,attrs) :
		self.rule_rep.targets = {
			**{ k:fmt_entry('target',t) for k,t in               self.rule_rep.targets     .items()   }
		,	**{ k:fmt_entry('target',t) for k,t in reversed(list(self.rule_rep.post_targets.items())) }
		}
		self.rule_rep.pop('post_targets')

	def handle_match(self) :
		self._init()
		if callable(self.rule_rep.deps) :
			self.static_val = None                                             # for deps, a None static value means all keys are allowed
			self.code       = self._mk_call(self.rule_rep.deps)
		else :
			dynamic_deps = {}
			for k,d in self.rule_rep.deps.items() :
				if callable(d) :
					dynamic_deps[k] = d
					continue
				if isinstance(d,str) : d = (d,)
				if any(callable(x) for x in d) :
					dynamic_deps[k] = d
					continue
				is_static = SimpleDepRe.match(d[0]) and all( k[1:-1] in self.stems for k in SimpleStemRe.findall(d[0]) )
				self.static_val[k] = d if is_static else None                                                            # for deps, the static value must contain all dep keys
				if not is_static : dynamic_deps[k] = d
			for k,v in dynamic_deps.items() :
				self.code += ' , ' , repr(k) , ' : '
				if callable(v) :
					self.code.append(self._mk_call(v))
				else :
					c = []
					for x in v :
						if   callable  (x    ) : c += ',',self._mk_call(x)
						elif isinstance(x,str) : c += ',','f',repr(x)
						else                   : raise TypeError(f'dep {k} contains {x} which is not a str nor callable')
					if len(v)==1 : self.code +=        c[1:]
					else         : self.code += ( '(',*c[1:],')' )
		self.rule_rep.x_match = self._finalize()
		# once deps are evaluated, they are available for others
		self.per_job.add('deps')
		if not callable(self.rule_rep.deps) :
			self.per_job.update({ k for k in self.rule_rep.deps.keys() if k.isidentifier() }) # special cases are not accessible from f-string's

	def handle_match_cmd(self) :
		self._init()
		self._handle_any('force')
		self.rule_rep.x_match_cmd = self._finalize()

	def handle_match_none(self) :
		self._init()
		self._handle_any('job_tokens')
		self.rule_rep.x_match_none = self._finalize()

	def handle_cache_none(self) :
		self._init()
		self._handle_any('key','cache')
		self.rule_rep.x_cache_none = self._finalize()

	def handle_submit_rsrcs(self) :
		self._init()
		self._handle_str ('backend'            )
		self._handle_dict('rsrcs'  ,'resources')
		self.rule_rep.x_submit_rsrcs = self._finalize()
		self.per_job.add('resources')
		if not callable(self.rule_rep.resources) :
			self.per_job.update(set(self.rule_rep.resources.keys()))

	def handle_start_cmd(self) :
		self._init()
		self._handle_any ('auto_mkdir'               )
		self._handle_any ('ignore_stat'              )
		self._handle_str ('autodep'                  )
		self._handle_str ('chroot'                   )
		self._handle_any ('interpreter'              )
		self._handle_dict('env'        ,'environ_cmd')
		self.rule_rep.x_start_cmd = self._finalize()

	def handle_start_rsrcs(self) :
		self._init()
		self._handle_dict('env'    ,'environ_resources')
		self._handle_any ('timeout'                    )
		self.rule_rep.x_start_rsrcs = self._finalize()

	def handle_start_none(self) :
		self._init()
		self._handle_any ('keep_tmp'                       )
		self._handle_any ('stderr_len'                     )
		self._handle_any ('start_delay'                    )
		self._handle_any ('kill_sigs'                      )
		self._handle_dict('env'        ,'environ_ancillary')
		self.rule_rep.x_start_none = self._finalize()

	def handle_end_cmd(self) :
		self._init()
		self._handle_any('allow_stderr')
		self.rule_rep.x_end_cmd = self._finalize()

	def handle_end_none(self) :
		self._init()
		self._handle_any('stderr_len')
		self.rule_rep.x_end_none = self._finalize()

def old_handle_deps(rule_rep,serialize_ctx,stems) :
	#
	def dep_code(rule_rep,kind,key,dep) :
		if SimpleDepRe.match(dep) and all( k[1:-1] in stems for k in SimpleStemRe.findall(dep) ) :
			return dep,None                                                                        # this can be interpreted by the engine without resorting to f-string interpretation
		else :
			if   "'"   not in dep and '\n' not in dep : sep = "'"
			elif '"'   not in dep and '\n' not in dep : sep = '"'
			elif "'''" not in dep                     : sep = "'''"
			elif '"""' not in dep                     : sep = '"""'
			else                                      : raise ValueError(f'{kind}{" " if kind else ""}{key} is too complicated an f-string with both \'\'\' and """ in it')
			dep = 'fr'+sep+dep+sep
			return dep,compile(dep,rule_rep.name+'.'+(key or '<stdin>'),'eval') # this is a real f-string
	#
	def mk_deps(rule_rep,kind,serialize_ctx,code=None) :
		deps = rule_rep[kind]
		res  = pdict(dct={})
		if code : codes = [code]
		else    : codes = []
		for k,d in deps.items() :
			d      = fmt_entry('dep',d)
			d0,code = dep_code(rule_rep,kind,k,d[0])
			d       = d0,*d[1:]
			if code : codes.append(code)
			res.dct[k] = (d[0],bool(code),*d[1:])
		if codes :
			res.prelude , ctx = serialize.get_code_ctx(
				*codes
			,	ctx        = serialize_ctx[0]
			,	no_imports = serialize_ctx[1]
			)
			res.ctx = tuple(ctx)
		return res
	#
	for k,s in rule_rep.deps.items() :
		if not isinstance(s,str) : raise TypeError(f'bad format for dep {k} of type {s.__class__.__name__}')
	#
	# handle deps & resources
	#
	rule_rep.deps = mk_deps(rule_rep,'deps',serialize_ctx)
	#
	job_tokens,tokens_code = dep_code(rule_rep,'','job_tokens',rule_rep.job_tokens)
	rule_rep.job_tokens    = (job_tokens,bool(tokens_code))
	rule_rep.resources     = { k:str(v) for k,v in rule_rep.resources.items() }
	rule_rep.resources     = mk_deps(rule_rep,'resources',serialize_ctx,tokens_code) # resources context must be able to interpret job_tokens as well
	per_job = serialize_ctx[0][0]
	per_job.add('deps')
	per_job.update({ k for k in rule_rep.deps.dct.keys() if k.isidentifier() }) # special cases are not accessible from f-string's

def handle_cmd(rule_rep,serialize_ctx) :
	cmd_ctx = set()
	if rule_rep.is_python :
		multi = len(rule_rep.cmd)>1
		if multi :
			cmd_lst = []
			cmd_idx = 0
			for c in rule_rep.cmd :
				cmd_idx += 1
				while any( any(y==f'cmd{cmd_idx}' for y in x) for x in serialize_ctx[0] ) : cmd_idx += 1 # protect against user having defined names such as cmd1, cmd2, ...
				# create a copy of c with its name modified (dont modify in place as this would be visible for other rules inheriting from the same parent)
				cmd_lst.append(c.__class__( c.__code__ , c.__globals__ , f'cmd{cmd_idx}' , c.__defaults__ , c.__closure__ ))
			rule_rep.cmd = cmd_lst
		cmd , cmd_ctx = serialize.get_src(
			*rule_rep.cmd
		,	ctx        = serialize_ctx[0]
		,	no_imports = serialize_ctx[1]
		,	force      = True
		)
		if multi :
			cmd += 'def cmd() :\n'
			for i,c in enumerate(rule_rep.cmd) :
				x = '' if c.__code__.co_argcount==0 else 'None' if i==0 else 'x'
				if i<len(rule_rep.cmd)-1 : cmd += f'\tx =    {c.__name__}({x})\n'
				else                     : cmd += f'\treturn {c.__name__}({x})\n'
	else :
		cmd = '\n'.join(rule_rep.cmd)
		per_job = serialize_ctx[0][0]
		for v in per_job :
			if re.search(f'\\b{v}\\b',cmd) :                                   # if {v} is mentioned in cmd as a word, put it in the context
				cmd_ctx.add(v)
	rule_rep.cmd_ctx = tuple(cmd_ctx)
	rule_rep.cmd     = cmd

def old_handle_env(rule_rep) :
	rule_rep.env = {}
	for k,v in rule_rep.pop('environ_cmd'      ).items() : rule_rep.env[k] = (v,'cmd'  )
	for k,v in rule_rep.pop('environ_resources').items() : rule_rep.env[k] = (v,'rsrcs')
	for k,v in rule_rep.pop('environ_ancillary').items() : rule_rep.env[k] = (v,'none' )
	if hasattr(rule_rep,'environ') :  raise TypeError('please use environ_cmd/rsrcs/ancillary')

def fmt_rule(rule) :
	if rule.__dict__.get('virtual',False) : return                             # with an explicit marker, this is definitely a base class
	#
	rule_rep,attrs = handle_inheritance(rule)
	#
	if 'target' in attrs and 'post_target' in attrs : raise ValueError('cannot specify both target and post_target')
	if   'target'      in attrs                     : rule_rep.targets     ['<stdout>'] = attrs.pop('target'     )
	elif 'post_target' in attrs                     : rule_rep.post_targets['<stdout>'] = attrs.pop('post_target')
	bad_keys = set(rule_rep.targets) & set(rule_rep.post_targets)
	if bad_keys : raise ValueError(f'{bad_keys} are defined both as target and post_target')
	#
	h = NewHandle( rule_rep , (attrs,sys.modules[rule.__module__].__dict__) , rule.__module__ )
	#
	h.handle_targets(attrs)
	if all(no_match(t) for t in rule_rep.targets.values()) :                                                         # if there is no way to match this rule, must be a base class
		if not rule.__dict__.get('virtual',True) : raise ValueError('no matching target while virtual forced False')
		return
	#
	h.prepare_jobs(rule)
	#
	# handle cases with no execution
	if rule.__anti__ :
		return pdict({ k:v for k,v in rule_rep.items() if k in StdAntiAttrs },__anti__=True)
	if not getattr(rule_rep,'cmd',None) :                                                                # Rule must have a cmd, or it is a base class
		if not rule.__dict__.get('virtual',True) : raise ValueError('no cmd while virtual forced False')
		return
	#
	# prepare serialization
	# handle execution related stuff
	if 'dep' in attrs                   : rule_rep.deps['<stdin>'] = attrs.pop('dep')
	if 'cwd' not in rule_rep            : rule_rep.cwd             = lmake.search_sub_root_dir(sys.modules[rule.__module__].__file__)
	if not callable(rule_rep.kill_sigs) : rule_rep.kill_sigs       = [int(x) for x in rule_rep.kill_sigs]
	h.handle_interpreter()
	# new
	h.handle_match       ()
	h.handle_match_cmd   ()
	h.handle_match_none  ()
	h.handle_cache_none  ()
	h.handle_submit_rsrcs()
	h.handle_start_cmd   ()
	h.handle_start_rsrcs ()
	h.handle_start_none  ()
	h.handle_end_cmd     ()
	h.handle_end_none    ()
	# old
	static_stems = find_static_stems(rule_rep)
	per_job = {                                                                # this is a set as there are no values as of now : they will be provided at job execution time
		'stems'   , *static_stems
	,	'targets' , *( k for k in rule_rep.targets.keys() if k.isidentifier() ) # special cases are not accessible from f-string's
	}
	serialize_ctx = ( ( per_job , attrs , sys.modules[rule.__module__].__dict__  ) , {rule.__module__} )
	#
	old_handle_deps(rule_rep,serialize_ctx,static_stems)
	handle_cmd     (rule_rep,serialize_ctx             )
	old_handle_env (rule_rep                           )
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
		for rule_rep in (fmt_rule(rule),)
		if rule_rep
	]
}),file=open(sys.argv[1],'w'))
