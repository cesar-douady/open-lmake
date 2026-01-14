# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import builtins
import dis
import inspect
import os.path as osp
import pickle
import re
import sys
import types

from lmake.utils import pdict

class f_str(str) :                                                                                          # used as a marker to generate an f-string as source
	def __repr__(self) :
		if not self                                                : return  "fr''"
		if "'"   not in self and '\n' not in self                  : return  "fr'"  +self     +"'"
		if '"'   not in self and '\n' not in self                  : return  'fr"'  +self     +'"'
		if "'''" not in self and self[-1]!="'"                     : return  "fr'''"+self     +"'''"
		if '"""' not in self and self[-1]!='"'                     : return  'fr"""'+self     +'"""'
		if "'''" not in self and self[-1]=="'"                     : return "(fr'''"+self[:-1]+"'''+\"'\")" # put last quote outside f-string as \ protection is forbidden within fr-strings
		if '"""' not in self and self[-1]=='"'                     : return '(fr"""'+self[:-1]+'""""\'"\')' # .
		if (sys.version_info.major,sys.version_info.minor)>=(3.13) : return  'f'+str.__repr__(self)         # until python3.13, \'s are forbidden within {}
		raise SyntaxError(f'string quotes are too complex : {self}')

class based_dict :                # used to add entries to a dict provided as a callable
	def __init__(self,base,inc) :
		assert callable(base),f'useless use of {self.__class__.__name__} with non-callable base {base}'
		self.base = base
		self.inc  = inc
	def __call__(self,*args,**kwds) :
		return { **base(*args,**kwds) , **inc }

__all__ = ('get_src','f_str','based_dict') # everything else is private

comment_re = re.compile(r'^\s*(#.*)?$')

_Code = (lambda:None).__code__.__class__

def get_src(*args,no_imports=None,ctx=(),force=False,root=None) :
	'''
		get a source text that reproduce args :
		- args must be composed of named objects such as functions or classes or dicts mapping names to values
		- no_imports is a set of module names that must not be imported in the resulting source or a regexpr of module filenames
		- ctx is a list of dict or set to get indirect values from. If found in a set, no value is generated
		- if force is true, args are guaranteed to be imported by value (i.e. they are not imported). Dependencies can be imported, though
		- if root is provided, source filename debug info are reported relative to this directory
		The return value is a pdict where :
			- src        : the source text that reproduces args
			- names      : the dict of accessed names, value is False if free (found in a set context), True if it has a value and None if no value found
			- modules    : the modules mentioned in import statements
			- dbg_info   : a dict mapping generated function names to (module,qualname,file,firstlineno)
			- may_import : a tuple (if present) of keys among ('static','dyn') showing when imports can be done : when src is executed (static) or defined functions are called (dyn)
	'''
	s = Serialize(no_imports,ctx,root)
	for a in args :
		if isinstance(a,dict) :
			for k,v in a.items() : s.val_src(k,v,force)
		else :
			s.val_src(None,a,force=force)
	return s.get_src()

def get_expr(expr,*,no_imports=None,ctx=(),force=False,call_callables=False) :
	'''
		get an expression text that reproduce expr :
		- expr can be any object
		- no_imports is a set of module names that must not be imported in the resulting source or a regexpr of module filenames
		- ctx is a list of dict or set to get indirect values from. If found in a set, no value is generated
		- if force is true, args are guaranteed to be inlined (i.e. they are not imported). Dependencies can be imported, though
		The return value is a pdict where :
			- expr       : the source text that reproduces expr as an expression when executed in the provided context
			- glbs       : a source text that reproduces the the necessary context to evaluate args
			- names      : the dict of accessed names, value is False if free (found in a set context), True if it has a value and None if no value found
			- modules    : the modules mentioned in import statements of ctx
			- dbg_info   : a dict mapping generated function names to (module,qualname,file,firstlineno)
			- may_import : a tuple (if present) of keys among ('static','dyn') showing when imports can be done : when src is executed (static) or defined functions are called (dyn)
	'''
	s        = Serialize(no_imports,ctx)
	expr     = s.expr_src(expr,force=force,call_callables=call_callables)
	res      = s.get_src()
	res.glbs = res.pop('src')
	res.expr = expr
	return res

end_liness = {}
srcs       = {}
def _analyze(file_name) :
	if file_name in end_liness : return
	srcs      [file_name] = lines          = open(file_name).read().splitlines()
	end_liness[file_name] = file_end_lines = {}
	for start_lineno in range(len(lines)) :
		start_line = lines[start_lineno]
		def_pos    = start_line.find('def')
		if def_pos==-1 : continue                                                                                   # not function def here
		prefix = start_line[:def_pos]
		if prefix and not prefix.isspace() : continue                                                               # if def is not at start of line : not a function def either
		if start_lineno>0 :
			prev_line = lines[start_lineno-1]
			if prev_line and not prev_line.isspace() and prev_line.startswith(prefix) and prev_line[def_pos]=='@' :
				file_end_lines[start_lineno] = None                                                                 # cannot handle decorators in serializer as we generally cannot reproduce the object
		candidate = None
		for lineno in range(start_lineno+1,len(lines)) :                                                            # XXX! : handle misaligned (), [] & {} and multi-lines '''string''' & """string"""
			line = lines[lineno]
			if comment_re.match(line) :
				if not candidate : candidate = lineno                                                               # manage to exclude comment lines at end of funcs
				continue
			if line.startswith(prefix) and line[def_pos].isspace() :
				candidate = None
				continue
			end_lineno = candidate or lineno
			break
		else :
			end_lineno = candidate or len(lines)
		file_end_lines[start_lineno] = end_lineno

class Serialize :

	def __init__(self,no_imports,ctx,root=None) :
		self.seen       = {}
		self.origin     = {}
		self.modules    = {}
		self.src        = []
		self.ctx        = tuple(ctx)
		self.dbg_info   = {}
		self.may_import = set()
		if   not root           : self.root_s = root
		elif root.endswith('/') : self.root_s = root
		else                    : self.root_s = root+'/'
		if   not        no_imports               : self.no_imports_proc = lambda m : False
		elif isinstance(no_imports,(list,tuple)) : self.no_imports_proc = set(no_imports).__contains__
		elif isinstance(no_imports, set        ) : self.no_imports_proc = no_imports.__contains__
		elif callable  (no_imports             ) : self.no_imports_proc = no_imports
		elif isinstance(no_imports, str        ) :
			no_imports_re = re.compile(no_imports)
			def no_imports_proc(mod_name) :
				try    : return no_imports_re.fullmatch(sys.modules[mod_name].__file__)
				except : return False
			self.no_imports_proc = no_imports_proc
		else :
			raise TypeError(f'cannot understand {no_imports}')

	@staticmethod
	def has_repr(val,avoid=None) :
		if avoid is None : avoid = set()          # used to detect loops : loops have no repr (i.e. the repr output does not represent to object)
		cls = val.__class__
		if val in (None,...)                         : return True
		if cls in (bool,int,float,complex,str,bytes) : return True
		val_id = id(val)
		if val_id in avoid : raise RuntimeError() # avoid loops
		avoid.add(val_id)
		try :
			if   cls in (tuple,list,set) : return all( Serialize.has_repr(v,avoid)                                 for v   in val         )
			elif cls is dict             : return all( Serialize.has_repr(k,avoid) and Serialize.has_repr(v,avoid) for k,v in val.items() )
			else                         : return False
		except RuntimeError :
			if not avoid : return False
			else         : raise
		finally :
			avoid.discard(val_id)

	def get_src(self) :
		if len(self.src) and self.src[-1] : self.src.append('')                                                                           # ensure there is \n at the end
		modules = ''
		for name,(mod,var) in self.modules.items() :
			self.may_import.add('static')
			if var :
				if   '.' in var : modules += f"from {mod} import {var.split('.',1)[0]} as {name} ; {name} = {name}.{var.split('.',1)[1]}" # use name as intermediate var as it is avail for sure
				elif var==name  : modules += f'from {mod} import {var}'
				else            : modules += f'from {mod} import {var} as {name}'
			else :
				if name==mod    : modules += f'import {mod}'
				else            : modules += f'import {mod} as {name}'
			modules += '\n'
		res = pdict(
			src      = modules + '\n'.join(self.src)
		,	names    = {      k:v[1] for k,v   in self.seen.items()     }
		,	modules  = tuple( mod    for mod,_ in self.modules.values() )
		,	dbg_info = self.dbg_info
		)
		if self.may_import : res.may_import = tuple(self.may_import)
		return res

	HaveGlbLoads = { 'LOAD_GLOBAL' , 'LOAD_NAME'   }
	Imports      = { 'IMPORT_FROM' , 'IMPORT_NAME' }
	def get_glbs(self,code) :
		'recursively find func globals'
		# for global references, we need to inspect code as code.co_names contains much more
		def gather_codes(code) :                                  # gather all code objects as there may be function defs within a function
			if code in codes : return
			codes[code] = None
			for c in code.co_consts :
				if isinstance(c,types.CodeType) : gather_codes(c)
		codes     = {}                                            # use dict rather than set to retain order so order is stable
		glb_loads = {}                                            # .
		gather_codes(code)
		for c in codes :
			for i in dis.Bytecode(c) :
				if i.opname in self.HaveGlbLoads : glb_loads[i.argval] = None
				if i.opname in self.Imports      : self.may_import.add('dyn')
		return glb_loads

	def val_src(self,name,val,*,force=False) :
		if not name :
			try                   : name = val.__name__
			except AttributeError : pass
		if name :
			if name in self.seen and self.seen[name][1]!=None :
				if (val,True)==self.seen[name] : return
				else                           : raise f'name conflict : {name} is both {val} and {self.seen[name][0]}'
			self.seen[name] = ( val , True )
		if isinstance(val,types.ModuleType) :
			self.modules[name] = (val.__name__,None)
		elif hasattr(val,'__module__') and hasattr(val,'__qualname__') and not self.no_imports_proc(val.__module__) and not force :
			self.modules[name] = (val.__module__,val.__qualname__)
		elif isinstance(val,types.FunctionType) :
			self.func_src(name,val)
		elif name :
			self.src.append(f'{name} = {self.expr_src(val,force=force)}')

	def expr_src(self,val,*,force=False,call_callables=False) :
		if isinstance(val,types.ModuleType) :
			self.modules[val.__name__] = (val.__name__,None)
			return val.__name__
		#
		sfx = ''
		if call_callables and callable(val) :
			inspect.signature(val).bind()                                                  # check val can be called with no argument
			sfx = '()'                                                                     # call val if possible and required
		has_name = hasattr(val,'__module__') and hasattr(val,'__qualname__') and not force
		if has_name :
			can_import = not self.no_imports_proc(val.__module__)
			if can_import :
				self.modules[val.__module__] = (val.__module__,None)
				return f'{val.__module__}.{val.__qualname__}{sfx}'
		if isinstance(val,types.FunctionType) :
			self.val_src(val.__name__,val)
			return f'{val.__name__}{sfx}'
		if self.has_repr(val) :
			return repr(val)
		#
		kwds = { 'force':force , 'call_callables':call_callables }
		#
		if isinstance(val,(tuple,list,set,dict)) :
			cls = val.__class__
			if cls in (tuple,list,set,dict) :
				pfx = ''
				sfx = ''
			else :                                                                         # val is an instance of a derived class
				self.modules[cls.__module__] = (cls.__module__,None)
				pfx = f'{cls.__module__}.{cls.__qualname__}('
				sfx = ')'
			if isinstance(val,tuple) : return f"{pfx}( { ' , '.join(   self.expr_src(x,**kwds)                             for x   in val        )} {',' if len(val)==1 else ''}){sfx}"
			if isinstance(val,list ) : return f"{pfx}[ { ' , '.join(   self.expr_src(x,**kwds)                             for x   in val        )} ]{sfx}"
			if isinstance(val,set  ) : return f"{pfx}{{ {' , '.join(   self.expr_src(x,**kwds)                             for x   in val        )} }}{sfx}" if len(val) else "{pfx}set(){sfx}"
			if isinstance(val,dict ) : return f"{pfx}{{ {' , '.join(f'{self.expr_src(k,**kwds)}:{self.expr_src(v,**kwds)}' for k,v in val.items())} }}{sfx}"
		#
		if isinstance(val,f_str) :
			fs = repr(val)
			try                     : cfs = compile(fs,'','eval')
			except SyntaxError as e : raise SyntaxError(f'{e} : {fs}')
			for glb_var in self.get_glbs(cfs) : self.gather_ctx(glb_var)
			return fs
		#
		if isinstance(val,based_dict) :
			return f"{{ **{self.expr_src(val.base,**kwds)}{''.join(f' , {self.expr_src(k,**kwds)}:{self.expr_src(v,**kwds)}' for k,v in val.inc.items())} }}"
		#
		# if we have a name, pickle will generate an import statement
		if not (has_name and not can_import) and not self.no_imports_proc(val.__class__.__module__) :
			# by default, use the broadest serializer available : pickle
			# inconvenient is that the resulting source is everything but readable
			# protocol 0 is the least unreadable, though, so use it
			val_str = pickle.dumps(val,protocol=0).decode()
			self.modules['pickle'] = ('pickle',None)
			return f'pickle.loads({val_str!r}.encode()){sfx}'
		#
		raise ValueError(f'as of this version, dont know how to serialize {val} (consider defining this value within a function)')

	BuiltinExecs = { 'eval' , 'exec' }
	def gather_ctx(self,name,func_glbs=None) :
		if func_glbs!=None : ctx = ( *self.ctx , func_glbs )
		else               : ctx =    self.ctx
		for d in ctx :
			if name not in d : continue
			try    : val             = d[name]
			except : self.seen[name] = ( None , False )           # d is in a set, no associated value
			else   : self.val_src(name,val)
			return
		self.seen[name] = ( None , None )                         # name is not found (let go, there will be a runtime error if it is accessed)
		if name in self.BuiltinExecs : self.may_import.add('dyn') # we may execute arbitrary code which may import

	def get_first_line(self,name,func,first_line) :
		#
		if not self.has_repr(func.__defaults__) :                                                   # we have to mimic inspect.Signature.__str__ and replace calls to repr by smarter self.get_src calls
			raise ValueError(f'defaults for func {func.__qualname__} are too difficult to analyze')
		#
		# if this does not work, then we have to call the tokenizer to find the correct : that splits signature from core
		if   first_line[-1]==':'                                : core = ''                                  # if line ends with :, it is a multi-line func and signature is the entire first line
		elif first_line.count(':')==1                           : core = first_line[first_line.find(':')+1:] # if there is a single :, there is not choice
		elif not func.__defaults__ and not func.__annotations__ : core = first_line[first_line.find(':')+1:] # if not default nor annotations, the first : is necessarily correct
		else                                                    : raise ValueError('core for func {func.__qualname__} is too difficult to analyze')
		#
		return f'def {name}{inspect.signature(func)} :{core}'

	def func_src(self,name,func) :
		code      = func.__code__
		module    = func.__module__
		file_name = osp.abspath(code.co_filename)
		_analyze(file_name)
		file_src       = srcs      [file_name]
		file_end_lines = end_liness[file_name]
		first_line_no1 = code.co_firstlineno                                             # first line is 1
		first_line_no0 = first_line_no1-1                                                # first line is 0
		end_line_no    = file_end_lines.get(first_line_no0)
		if first_line_no0>0 and file_src[first_line_no0-1].strip().startswith('@') : raise ValueError(f'cannot handle decorated {name}')
		assert end_line_no,f'{file_name}:{first_line_no1} : cannot find def {name}'
		#
		for glb_var in self.get_glbs(code) : self.gather_ctx( glb_var , func.__globals__ )
		#
		self.src.append( self.get_first_line( name , func , file_src[first_line_no0] ) ) # first line
		self.src.extend( file_src[ first_line_no0+1 : end_line_no ]                    ) # other lines
		#
		if self.root_s and file_name.startswith(self.root_s) : file_name = file_name[len(self.root_s):]
		self.dbg_info[name] = (func.__module__,func.__qualname__,file_name,first_line_no1)
