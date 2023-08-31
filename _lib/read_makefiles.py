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
f_str = serialize.f_str

sys.path = [lmake_s_dir+'/lib','.',*save_path]
import Lmakefile

sys.path = save_path                                                           # restore sys.path in case user played with it

# helper constants
# AntiRule's only need a subset of plain rule attributes as there is no execution
StdAntiAttrs = {
	#                  type    dynamic
	'job_name'     : ( str   , False   )
,	'name'         : ( str   , False   )
,	'post_targets' : ( dict  , False   )
,	'prio'         : ( float , False   )
,	'targets'      : ( dict  , False   )
,	'stems'        : ( dict  , False   )
}
StdExecAttrs = {
	#                       type    dynamic
	'allow_stderr'      : ( bool  , True    )
,	'autodep'           : ( str   , True    )
,	'auto_mkdir'        : ( bool  , True    )
,	'backend'           : ( str   , True    )
,	'chroot'            : ( str   , True    )
,	'cache'             : ( str   , True    )
,	'cmd'               : ( None  , False   )
,	'cwd'               : ( str   , False   )
,	'deps'              : ( dict  , True    )
,	'environ_cmd'       : ( dict  , True    )
,	'environ_resources' : ( dict  , True    )
,	'environ_ancillary' : ( dict  , True    )
,	'ete'               : ( float , False   )
,	'force'             : ( bool  , False   )
,	'ignore_stat'       : ( bool  , True    )
,	'keep_tmp'          : ( bool  , True    )
,	'kill_sigs'         : ( tuple , True    )
,	'n_tokens'          : ( int   , False   )
,	'python'            : ( tuple , False   )
,	'resources'         : ( dict  , True    )
,	'shell'             : ( tuple , False   )
,	'start_delay'       : ( float , True    )
,	'stderr_len'        : ( int   , True    )
,	'timeout'           : ( float , True    )
,	'job_tokens'        : ( str   , True    )
}
Keywords     = {'dep','deps','resources','stems','target','targets'}
StdAttrs     = { **StdAntiAttrs , **StdExecAttrs }
SimpleStemRe = re.compile(r'{\w+}')
SimpleDepRe  = re.compile(r'^([^{}]|{{|}})*({\w+}([^{}]|{{|}})*)*$')           # this means stems in {} are simple identifiers, e.g. 'foo{a}bar but not 'foo{a+b}bar'
SimpleStrRe  = re.compile(r'^([^{}]|{{|}})*$')                                 # this means string can be interpreted

def update_dct(acc,new) :
	for k,v in new.items() :
		acc.pop(k,None)                                                        # ensure entry is put at the end of dict order
		if v is not None : acc[k] = v                                          # None is used to suppress entries
def update_set(acc,new) :
	if not isinstance(new,(list,tuple,set)) : new = (new,)
	for k in new :
		if isinstance(k,str) and k and k[0]=='-' : acc.discard(k[1:])
		else                                     : acc.add    (k    )
def update_lst(acc,new) :
	if not isinstance(new,(list,tuple,set)) : new = (new,)
	acc += new
def update(acc,new) :
	if   callable  (acc     ) : acc = top(new)
	elif callable  (new     ) : acc = top(new)
	elif isinstance(acc,dict) : update_dct(acc,new)
	elif isinstance(acc,set ) : update_set(acc,new)
	elif isinstance(acc,list) : update_lst(acc,new)
	else                      : raise TypeError(f'cannot combine {acc.__class__.__name__} and {new.__class__.__name__}')
	return acc
def top(new) :
	if   callable  (new     ) : return new
	elif isinstance(new,dict) : acc = {}
	elif isinstance(new,set ) : acc = set()
	elif isinstance(new,list) : acc = []
	else                      : raise TypeError(f'cannot combine {new.__class__.__name__}')
	return update(acc,new)

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

def no_match(target) :
	return '-match' in target[1:]

def qualify(attrs) :
	seen = {}
	for k in attrs.stems       .keys() : qualify_key('stem'       ,k,seen)
	for k in attrs.targets     .keys() : qualify_key('target'     ,k,seen)
	for k in attrs.post_targets.keys() : qualify_key('post_target',k,seen)
	if not attrs.__anti__ :
		for key in ('dep','resource') :
			dct = attrs[key+'s']
			if callable(dct) : continue
			for k in dct.keys() : qualify_key(key,k,seen)

def handle_inheritance(rule) :
	# acquire rule properties by fusion of all info from base classes
	combine = set()
	dct     = pdict(cmd=[])                                                    # cmd is handled specially
	# special case for cmd : it may be a function or a str, and base classes may want to provide 2 versions.
	# in that case, the solution is to attach a shell attribute to the cmd function to contain the shell version
	is_shell = isinstance(getattr(rule,'cmd',None),str)
	try :
		for i,r in enumerate(reversed(rule.__mro__)) :
			d = r.__dict__
			if 'combine' in d :
				for k in d['combine'] :
					if k in dct and k not in combine : dct[k] = top(dct[k])    # if an existing value becomes combined, it must be uniquified as it may be modified by further combine's
				combine = update(combine,d['combine'])                         # process combine first so we use the freshest value
			for k,v in d.items() :
				if k.startswith('__') and k.endswith('__')                      : continue # do not process standard python attributes
				if k=='combine'                                                 : continue
				if k=='cmd' :
					if is_shell and callable(v) :
						if not hasattr(v,'shell') : raise TypeError(f'{r.__name__}.cmd is callable and has no shell attribute while a shell cmd is needed')
						v = v.shell
					dct[k].append(v)
				elif k in combine :
					if k in dct : dct[k] = update(dct[k],v)
					else        : dct[k] = top(v)                              # make a fresh copy as it may be modified by further combine's
				else :
					dct[k] = v
	except Exception as e :
		e.base  = r
		e.field = k
		raise
	# reformat dct
	attrs = pdict()
	for k,v in dct.items() :
		if k in StdAttrs :
			if v is None : continue                                            # None is not transported
			typ,dyn = StdAttrs[k]
			if typ and not ( dyn and callable(v) ) :
				try    : v = typ(v)
				except : raise TypeError(f'bad format for {k} : cannot be converted to {typ.__name__}')
		attrs[k] = v
	attrs.name     = rule.__dict__.get('name',rule.__name__)                # name is not inherited as it must be different for each rule and defaults to class name
	attrs.__anti__ = rule.__anti__
	qualify(attrs)
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

class Handle :
	def __init__(self,attrs,rule) :
		module_name     = rule.__module__
		module          = sys.modules[module_name]
		self.rule       = rule
		self.attrs      = attrs
		self.glbs       = (attrs,module.__dict__)
		self.no_imports = {module_name}
		self.rule_rep   = pdict( name=attrs.name , prio=attrs.prio , stems=attrs.stems )
		try    : self.local_root = lmake.search_sub_root_dir(module.__file__)
		except : self.local_root = ''                                          # rules defined outside repo (typically standard base rules) are deemed to apply to the whole base

	def _handle_dict(self,key,rep_key=None) :
		if not rep_key               : rep_key = key
		if rep_key not in self.attrs : return
		dct = self.attrs[rep_key]
		if callable(dct) :
			self.dynamic_val[key] = dct
			return
		sv = {}
		dv = {}
		for k,v in dct.items() :
			if   callable(v)           : dv[k] = v
			elif not isinstance(v,str) : sv[k] = v
			elif SimpleStrRe.match(v)  : sv[k] = static_fstring(v)             # v has no variable parts, make it a static value
			else                       : dv[k] = f_str(v)                      # mark v so it is generated as an f-string
		for k in dv.keys() : sv[k] = None                                      # ensure key is in static val so that it is seen from cmd
		if sv : self.static_val [key] = sv
		if dv : self.dynamic_val[key] = dv

	def _handle_val(self,typ,key,rep_key=None) :
		if not rep_key               : rep_key = key
		if rep_key not in self.attrs : return
		x = self.attrs[rep_key]
		if callable(x) :
			self.dynamic_val[key] = x
		else :
			if typ : x = typ(x)                                                # convert x if asked to do so
			if typ==str :
				if SimpleStrRe.match(x) : self.static_val [key] = static_fstring(x) # x has no variable parts, make it a static value
				else                    : self.dynamic_val[key] = f_str         (x) # mark x so it is generated as an f-string
			else :
				self.static_val [key] = x

	def _init(self) :
		self.static_val  = pdict()
		self.dynamic_val = pdict()

	def _finalize(self) :
		static_val  = self.static_val
		dynamic_val = self.dynamic_val
		del self.static_val
		del self.dynamic_val
		if not dynamic_val : return (static_val,)
		code,ctx,names = serialize.get_expr( dynamic_val , ctx=(self.per_job,*self.glbs) , no_imports=self.no_imports , call_callables=True )
		return ( static_val , ctx , code , tuple(names) )

	def handle_targets(self) :
		def fmt(key,entry) :
			if       isinstance(entry,str         ) : flags = ()
			elif not isinstance(entry,(tuple,list)) : raise TypeError(f'bad format for target {key} of type {entry.__class__.__name__}')
			elif not entry                          : raise TypeError(f'cannot find target {key} in empty entry')
			elif not isinstance(entry[0],str)       : raise TypeError(f'bad format for target {key} of type {entry[0].__class__.__name__}')
			else                                    : entry,flags = entry[0],tuple(mk_snake(f) for f in entry[1:] if f)
			return (entry,*flags)
		if   'target' in self.attrs and 'post_target' in self.attrs : raise ValueError('cannot specify both target and post_target')
		if   'target'      in self.attrs                            : self.attrs.targets     ['<stdout>'] = self.attrs.pop('target'     )
		elif 'post_target' in self.attrs                            : self.attrs.post_targets['<stdout>'] = self.attrs.pop('post_target')
		bad_keys = set(self.attrs.targets) & set(self.attrs.post_targets)
		if bad_keys : raise ValueError(f'{bad_keys} are defined both as target and post_target')
		#
		self.rule_rep.targets = {
			**{ k:fmt(k,t) for k,t in               self.attrs.targets     .items()   }
		,	**{ k:fmt(k,t) for k,t in reversed(list(self.attrs.post_targets.items())) }
		}

	def handle_job_name(self) :
		def find_job_name() :
			if '<stdout>' in targets : return '<stdout>'                       # if we have a stdout, this is an excellent job_name
			for r in self.rule.__mro__ :                                       # find the first clean target of the most specific class that has one
				for k in r.__dict__.get('targets',{}).keys() :
					if not no_match(targets[k]) : return k                     # no_match targets are not good names : they may be ambiguous and they are not the focus of the user
			for k,t in targets.items() :                                       # find anything, a priori a post_target
				if not no_match(t) : return k
			assert False,f'cannot find adequate target to name jobs of {rule.__name__}' # we know we have clean targets, we should have found a job_name
		#
		targets = self.rule_rep.targets
		if 'job_name' in self.attrs : self.rule_rep.job_name = self.attrs.job_name
		else                        : self.rule_rep.job_name = targets[find_job_name()][0]

	def prepare_jobs(self) :
		self.static_stems = find_static_stems(self.rule_rep.job_name)
		self.per_job = {
			'stems'   , *self.static_stems
		,	'targets' , *( k for k in self.rule_rep.targets.keys() if k.isidentifier() )
		}
		if 'cwd'   in self.attrs : self.rule_rep.cwd      = self.attrs.cwd
		else                     : self.rule_rep.cwd      = self.local_root
		if 'force' in self.attrs : self.rule_rep.force    = bool(self.attrs.force)
		if True                  : self.rule_rep.n_tokens = self.attrs.n_tokens

	def handle_interpreter(self) :
		if all( callable(c) for c in self.attrs.cmd ) :
			self.rule_rep.is_python   = True
			self.attrs   .interpreter = self.attrs.python
		elif all( isinstance(c,str) for c in self.attrs.cmd ) :
			self.rule_rep.is_python   = False
			self.attrs   .interpreter = self.attrs.shell
		elif all( callable(c) or isinstance(c,str) for c in self.attrs.cmd ) :
			raise TypeError('cannot mix Python & shell along the inheritance hierarchy')
		else :
			raise TypeError('bad cmd type')

	def handle_create_none(self) :
		self._init()
		self._handle_val(int,'job_tokens')
		self.rule_rep.create_none_attrs = self._finalize()

	def handle_cache_none(self) :
		self._init()
		self._handle_val(str,'key','cache')
		self.rule_rep.cache_none_attrs = self._finalize()

	def handle_create_match(self) :
		if 'dep' in self.attrs : self.attrs.deps['<stdin>'] = self.attrs.dep
		self._init()
		if callable(self.attrs.deps) :
			self.static_val  = None                                                   # for deps, a None static value means all keys are allowed
			self.dynamic_val = self.attrs.deps
		else :
			for k,d in self.attrs.deps.items() :
				self.static_val[k] = None                                      # in all cases, we need a static entry, even for dynamic values, so we have an idx at compilations time
				if isinstance(d,str) : d = (d,)
				if callable(d) :
					self.dynamic_val[k] = d
				elif not any(callable(x) for x in d) and SimpleDepRe.match(d[0]) and all( k[1:-1] in self.static_stems for k in SimpleStemRe.findall(d[0]) ) :
					self.static_val[k] = d
				else :
					l = []
					for x in d :
						if   callable  (x    ) : l.append(      x )
						elif isinstance(x,str) : l.append(f_str(x))
						else                   : raise TypeError(f'dep {k} contains {x} which is not a str nor callable')
					self.dynamic_val[k] = l
		self.rule_rep.create_match_attrs = self._finalize()
		# once deps are evaluated, they are available for others
		self.per_job.add('deps')
		if self.rule_rep.create_match_attrs[0] : self.per_job.update({ k for k in self.attrs.deps.keys() if k.isidentifier() }) # special cases are not accessible from f-string's

	def handle_submit_rsrcs(self) :
		self._init()
		self._handle_val (str,'backend'            )
		self._handle_dict('rsrcs'  ,'resources')
		self.rule_rep.submit_rsrcs_attrs = self._finalize()
		self.per_job.add('resources')
		rsrcs = self.rule_rep.submit_rsrcs_attrs[0].get('rsrcs',{})
		if not callable(rsrcs) : self.per_job.update(set(rsrcs.keys()))

	def handle_start_cmd(self) :
		self._init()
		self._handle_val (bool ,'auto_mkdir'                )
		self._handle_val (bool ,'ignore_stat'               )
		self._handle_val (str  ,'autodep'                   )
		self._handle_val (str  ,'chroot'                    )
		self._handle_val (tuple,'interpreter'               )
		self._handle_val (str  ,'local_mrkr' ,'local_marker')
		self._handle_dict(      'env'        ,'environ_cmd' )
		self.rule_rep.start_cmd_attrs = self._finalize()

	def handle_start_rsrcs(self) :
		self._init()
		self._handle_dict(      'env'    ,'environ_resources')
		self._handle_val (float,'timeout'                    )
		self.rule_rep.start_rsrcs_attrs = self._finalize()

	def handle_start_none(self) :
		if not callable(self.attrs.kill_sigs) : self.attrs.kill_sigs = [int(x) for x in self.attrs.kill_sigs]
		self._init()
		self._handle_val (bool ,'keep_tmp'                       )
		self._handle_val (float,'start_delay'                    )
		self._handle_val (tuple,'kill_sigs'                      )
		self._handle_dict(      'env'        ,'environ_ancillary')
		self.rule_rep.start_none_attrs = self._finalize()

	def handle_end_cmd(self) :
		self._init()
		self._handle_val(bool,'allow_stderr')
		self.rule_rep.end_cmd_attrs = self._finalize()

	def handle_end_none(self) :
		self._init()
		self._handle_val(int,'stderr_len')
		self.rule_rep.end_none_attrs = self._finalize()

	def handle_cmd(self) :
		if self.rule_rep.is_python :
			cmd_ctx       = set()
			serialize_ctx = (self.per_job,*self.glbs)
			cmd           = self.attrs.cmd
			multi         = len(cmd)>1
			if multi :
				cmd_lst = []
				cmd_idx = 0
				for c in cmd :
					cmd_idx += 1
					while any( any(y==f'cmd{cmd_idx}' for y in x) for x in serialize_ctx ) : cmd_idx += 1 # protect against user having defined names such as cmd1, cmd2, ...
					# create a copy of c with its name modified (dont modify in place as this would be visible for other rules inheriting from the same parent)
					cmd_lst.append(c.__class__( c.__code__ , c.__globals__ , f'cmd{cmd_idx}' , c.__defaults__ , c.__closure__ ))
				cmd = cmd_lst
			cmd , cmd_ctx = serialize.get_src(
				*cmd
			,	ctx        = serialize_ctx
			,	no_imports = self.no_imports
			,	force      = True
			)
			if multi :
				cmd += 'def cmd() :\n'
				for i,c in enumerate(cmd_lst) :
					x = '' if c.__code__.co_argcount==0 else 'None' if i==0 else 'x'
					if i<len(self.attrs.cmd)-1 : cmd += f'\tx =    {c.__name__}({x})\n'
					else                       : cmd += f'\treturn {c.__name__}({x})\n'
			self.rule_rep.cmd = ( {'cmd':cmd,'is_python':True} , '' , '' , tuple(cmd_ctx) )
		else :
			self.attrs.cmd = '\n'.join(self.attrs.cmd)
			self._init()
			self._handle_val(str ,'cmd')
			self.static_val.is_python = False
			self.rule_rep.cmd = self._finalize()

def fmt_rule(rule) :
	if rule.__dict__.get('virtual',False) : return                             # with an explicit marker, this is definitely a base class
	#
	h = Handle( handle_inheritance(rule) , rule )
	#
	h.handle_targets()
	if all(no_match(t) for t in h.rule_rep.targets.values()) :                                                       # if there is no way to match this rule, must be a base class
		if not rule.__dict__.get('virtual',True) : raise ValueError('no matching target while virtual forced False')
		return
	h.handle_job_name()
	#
	# handle cases with no execution
	if rule.__anti__ :
		return pdict({ k:v for k,v in h.rule_rep.items() if k in StdAntiAttrs },__anti__=True)
	if not getattr(h.attrs,'cmd',None) :                                                                 # Rule must have a cmd, or it is a base class
		if not rule.__dict__.get('virtual',True) : raise ValueError('no cmd while virtual forced False')
		return
	#
	h.prepare_jobs()
	#
	h.handle_interpreter ()
	h.handle_create_none ()
	h.handle_cache_none  ()
	h.handle_create_match()
	h.handle_submit_rsrcs()
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

if hasattr(lmake,'sources') : srcs = lmake.sources
else                        : srcs = lmake.auto_sources()

print(repr({
	'source_dirs'      : tuple(lmake.source_dirs)
,	'local_admin_dir'  : lmake.local_admin_dir
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
