# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
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

class f_str(str) :                                                                    # used as a marker to generate an f-string as source
	def __repr__(self) :
		if   not self                               : res = "f''"
		elif "'"   not in self and '\n' not in self : res = "f'"  +self+"'"
		elif '"'   not in self and '\n' not in self : res = 'f"'  +self+'"'
		elif "'''" not in self and self[-1]!="'"    : res = "f'''"+self+"'''"
		elif '"""' not in self and self[-1]!='"'    : res = 'f"""'+self+'"""'
		elif "'''" not in self and self[-1]=="'"    : res = "f'''"+self[:-1]+"\\''''" # this \ is certainly outside {}, hence f-string is still valid
		elif '"""' not in self and self[-1]=='"'    : res = 'f"""'+self[:-1]+'\\""""' # .
		else                                        : res = 'f'+str.repr(self)        # hope that repr will not insert \ within {}
		return res

class based_dict :                # used to add entries to a dict provided as a callable
	def __init__(self,base,inc) :
		assert callable(base),f'useless use of {self.__class__.__name__} with non-callable base {base}'
		self.base = base
		self.inc  = inc
	def __call__(self,*args,**kwds) :
		return { **base(*args,**kwds) , **inc }

__all__ = ('get_src','get_code_ctx','f_str','based_dict') # everything else is private

comment_re = re.compile(r'^\s*(#.*)?$')

_Code = (lambda:None).__code__.__class__

def get_src(*args,no_imports=None,ctx=(),force=False,root=None) :
	'''
		get a source text that reproduce args :
		- args must be composed of named objects such as functions or classes or dicts mapping names to values
		- no_imports is a set of module names that must not be imported in the resulting source or a regexpr of module file names
		- ctx is a list of dict or set to get indirect values from. If found in a set, no value is generated
		- if force is true, args are guaranteed to be imported by value (i.e. they are not imported). Dependencies can be imported, though
		- if root is provided, source filename debug info are reported relative to this directory
		The return value is (source,names) where :
			- source is the source text that reproduces args
			- names is the set of names found in sets in ctx
			- debug_info contains a dict mapping generated function names to (module,qualname,file,firstlineno)
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
		- no_imports is a set of module names that must not be imported in the resulting source or a regexpr of module file names
		- ctx is a list of dict or set to get indirect values from. If found in a set, no value is generated
		- if force is true, args are guaranteed to be imported by value (i.e. they are not imported). Dependencies can be imported, though
		The return value is (source,ctx,names) where :
			- source is the source text that reproduces expr as an expression
			- ctx is as ource text that reproduces the environment in which to evaluate source
			- names is the list of names found in sets in ctx
	'''
	s = Serialize(no_imports,ctx)
	src = s.expr_src(expr,force=force,call_callables=call_callables)
	return (src,*s.get_src())

def get_code_ctx(*args,no_imports=None,ctx=()) :
	'''
		get a source text that provides the necessary context to evaluate args :
		- args must be composed of code objects
		- no_imports is a set of module names that must not be imported in the resulting source or a regexpr of module file names
		- ctx is a list of dict or set to get indirect values from. If found in a set, no value is generated
		the return value is (source,names) where :
			- source is the source text that provides the necessary context to evaluate args
			- names is the list of names found in sets in ctx
	'''
	s = Serialize(no_imports,ctx)
	for a in args :
		if not isinstance(a,types.CodeType) : raise TypeError(f'args must be code, not {a.__class__.__name__}')
		for glb_var in s.get_glbs(a) : s.gather_ctx(glb_var)
	return s.get_src()

end_liness = {}
srcs       = {}
def _analyze(filename) :
	if filename in end_liness : return
	srcs      [filename] = lines          = open(filename).read().splitlines()
	end_liness[filename] = file_end_lines = {}
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
	InSet = object()                              # a marker to mean that we have no value as name was found in a set (versus in a dict) in the context list
	def __init__(self,no_imports,ctx,root=None) :
		self.seen       = {}
		self.modules    = {}
		self.src        = []
		self.in_sets    = set()
		self.ctx        = list(ctx)
		self.root       = root
		self.debug_info = {}
		if not no_imports :
			self.no_imports_proc = lambda m : False
		elif isinstance(no_imports,str) :
			no_imports_re = re.compile(no_imports)
			def no_imports_proc(mod_name) :
				try    : return no_imports_re.fullmatch(sys.modules[mod_name].__file__)
				except : return False
			self.no_imports_proc = no_imports_proc
		elif isinstance(no_imports,set) :
			self.no_imports_proc = no_imports.__contains__
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
		if len(self.src) and len(self.src[-1]) : self.src.append('')                                               # ensure there is \n at the end
		modules = ''
		for n,(f,s) in self.modules.items() :
			if f is not None : pfx = f'from {f} '
			else             : pfx = ''
			if   n==s     : modules += f'{pfx}import {s}\n'
			elif '.' in s : modules += f"{pfx}import {s.split('.',1)[0]} as {n} ; {n} = {n}.{s.split('.',1)[1]}\n" # use {n} as temporary as it is guaranteed to be an available name
			else          : modules += f'{pfx}import {s} as {n}\n'
		return (
				modules
			+	'\n'.join(self.src)
		,	{k for k,v in self.seen.items() if v is self.InSet}
		,	self.debug_info
		)

	have_name = {
		'LOAD_GLOBAL','STORE_GLOBAL','DELETE_GLOBAL'
	,	'LOAD_NAME'  ,'STORE_NAME'  ,'DELETE_NAME'
	}
	@staticmethod
	def get_glbs(code) :
		'recursively find func globals'
		# for global references, we need to inspect code as code.co_names contains much more
		def gather_codes(code) :                                  # gather all code objects as there may be function defs within a function
			if code in codes : return
			codes[code] = None
			for c in code.co_consts :
				if isinstance(c,types.CodeType) : gather_codes(c)
		codes = {}                                                # use dict to retain order so order is stable
		gather_codes(code)
		glb_names = {}                                            # use dict to retain order so order is stable
		for c in codes :
			for i in dis.Bytecode(c) :
				if i.opname in Serialize.have_name : glb_names[i.argval] = None
		return glb_names

	def val_src(self,name,val,*,force=False) :
		if not name :
			try                   : name = val.__name__
			except AttributeError : pass
		if name :
			if name in self.seen :
				if val==self.seen[name] : return
				else                    : raise f'name conflict : {name} is both {val} and {self.seen[name]}'
			self.seen[name] = val
		if isinstance(val,types.ModuleType) :
			self.modules[name] = (None,val.__name__)
		elif hasattr(val,'__module__') and hasattr(val,'__qualname__') and not self.no_imports_proc(val.__module__) and not force :
			self.modules[name] = (val.__module__,val.__qualname__)
		elif isinstance(val,types.FunctionType) :
			self.func_src(name,val)
		elif name :
			self.src.append(f'{name} = {self.expr_src(val,force=force)}')

	def expr_src(self,val,*,force=False,call_callables=False) :
		if isinstance(val,types.ModuleType) :
			self.modules[val.__name__] = (None,val.__name__)
			return val.__name__
		#
		sfx = ''
		if call_callables and callable(val) :
			inspect.signature(val).bind()                                                                                          # check val can be called with no argument
			sfx = '()'                                                                                                             # call val if possible and required
		if hasattr(val,'__module__') and hasattr(val,'__qualname__') and not self.no_imports_proc(val.__module__ ) and not force :
			self.modules[val.__module__] = (None,val.__module__)
			return f'{val.__module__}.{val.__qualname__}{sfx}'
		if isinstance(val,types.FunctionType) :
			self.func_src(val.__name__,val)
			return f'{val.__name__}{sfx}'
		if self.has_repr(val) : return repr(val)
		#
		kwds = { 'force':force , 'call_callables':call_callables }
		#
		if isinstance(val,(tuple,list,set,dict)) :
			cls = val.__class__
			if cls in (tuple,list,set,dict) :
				pfx = ''
				sfx = ''
			else :                                                                                                                 # val is an instance of a derived class
				self.modules[cls.__module__] = (None,cls.__module__)
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
		if  not self.no_imports_proc(val.__class__.__module__) :
			# by default, use the broadest serializer available : pickle
			# inconvenient is that the resulting source is everything but readable
			# protocol 0 is the least unreadable, though, so use it
			val_str = pickle.dumps(val,protocol=0).decode()
			self.modules['pickle'] = (None,'pickle')
			return f'pickle.loads({val_str!r}.encode()){sfx}'
		#
		raise ValueError(f'dont know how to serialize {val}')

	def gather_ctx(self,name) :
		for d in self.ctx :
			if name not in d : continue
			try    : val = d[name]
			except : self.seen[name] = self.InSet
			else   : self.val_src(name,val)
			return
		# name may be a builtins or it does not exist, in both case, do nothing and we'll have a NameError exception at runtime if name is accessed and it is not a builtin

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
		code     = func.__code__
		module   = func.__module__
		filename = osp.abspath(code.co_filename)
		_analyze(filename)
		file_src       = srcs      [filename]
		file_end_lines = end_liness[filename]
		first_line_no1 = code.co_firstlineno                                                            # first line is 1
		first_line_no0 = first_line_no1-1                                                               # first line is 0
		end_line_no    = file_end_lines.get(first_line_no0)
		if first_line_no0>0 and file_src[first_line_no0-1].strip().startswith('@') : raise ValueError(f'cannot handle decorated {name}')
		assert end_line_no,f'{filename}:{first_line_no1} : cannot find def {name}'
		#
		if func.__globals__ not in self.ctx : self.ctx.append(func.__globals__)
		for glb_var in self.get_glbs(code) :
			self.gather_ctx(glb_var)
		#
		if self.root : filename = osp.relpath(filename,self.root)
		if True      : self.src.append( self.get_first_line( name , func , file_src[first_line_no0] ) ) # first line
		if True      : self.src.extend( file_src[ first_line_no0+1 : end_line_no ]                    ) # other lines
		#
		self.debug_info[name] = (func.__module__,func.__qualname__,filename,first_line_no1)
