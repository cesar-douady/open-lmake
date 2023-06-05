# This file is part of the lmake distribution (https://wgit.doliam.net/cdy/lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import builtins
import dis
import inspect
import pickle
import re
import sys
import types

__all__ = ('get_src','get_code_ctx')                                           # everything else is private

comment_re = re.compile(r'^\s*(#.*)?$')

def get_src(*args,no_imports=(),ctx=()) :
	'''
		get a source text that reproduce args :
		- args must be composed of named objects such as functions or classes or dict mapping names to values
		- no_imports is a list of modules or module names that must not be imported in the resulting source
		- ctx is a list of dict or set to get indirect values from. If found in a set, no value is generated
		the return value is (source,names) where :
			- source is the source text that reproduces args
			- names is the list of names found in sets in ctx
	'''
	s = Serialize(no_imports,ctx)
	for a in args :
		if isinstance(a,dict) :
			for k,v in a.items() : s.val_src(k,v)
		else :
			s.val_src(None,a)
	return s.get_src() , s.in_sets

def get_code_ctx(*args,no_imports=(),ctx=()) :
	'''
		get a source text that provides the necessary context to evaluate args :
		- args must be composed of code objects
		- no_imports is a list of modules or module names that must not be imported in the resulting source
		- ctx is a list of dict or set to get indirect values from. If found in a set, no value is generated
		the return value is (source,names) where :
			- source is the source text that provides the necessary context to evaluate args
			- names is the list of names found in sets in ctx
	'''
	s = Serialize(no_imports,ctx)
	for a in args :
		if not isinstance(a,get_code_ctx.__code__.__class__) : raise TypeError(f'args must be code, not {a.__class__.__name__}')
		for glb_var in s.get_glbs(a) : s.gather_ctx(glb_var)
	return s.get_src() , s.in_sets

end_liness = {}
srcs       = {}
def analyze(file_name) :
	if file_name in end_liness : return
	srcs      [file_name] = lines          = open(file_name).read().splitlines()
	end_liness[file_name] = file_end_lines = {}
	for start_lineno in range(len(lines)) :
		start_line = lines[start_lineno]
		def_pos    = start_line.find('def')
		if def_pos==-1 : continue                                              # not function def here
		prefix = start_line[:def_pos]
		if prefix and not prefix.isspace() : continue                          # if def is not at start of line : not a function def either
		if start_lineno>0 :
			prev_line = lines[start_lineno-1]
			if prev_line and not prev_line.isspace() and prev_line.startswith(prefix) and prev_line[def_pos]=='@' :
				file_end_lines[start_lineno] = None                            # cannot handle decorators in serializer as we generally cannot reproduce the object
		candidate = None
		for lineno in range(start_lineno+1,len(lines)) :                       # XXX : handle misaligned (), [] & {} and multi-lines '''string''' & """string"""
			line = lines[lineno]
			if comment_re.match(line) :
				if not candidate : candidate = lineno                          # manage to exclude comment lines at end of funcs
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
	def __init__(self,no_imports,ctx) :
		self.seen    = set()
		self.src_lst = []
		self.in_sets = set()
		self.ctx     = list(ctx)
		if isinstance(no_imports,str) : self.by_values =    {no_imports}
		else                          : self.by_values = set(no_imports)

	@staticmethod
	def has_repr(val) :
		cls = val.__class__
		if val in (None,...)                   : return True
		if cls in (bool,int,float,complex,str) : return True
		if cls in (tuple,list,set            ) : return all( Serialize.has_repr(v)                           for v   in val         )
		if cls is  dict                        : return all( Serialize.has_repr(k) and Serialize.has_repr(v) for k,v in val.items() )
		return False

	def get_src(self) :
		if len(self.src_lst) and len(self.src_lst[-1]) : self.src_lst.append('') # ensure there is \n at the end
		return '\n'.join(self.src_lst)

	have_name = {
		'LOAD_GLOBAL' , 'STORE_GLOBAL' , 'DELETE_GLOBAL'
	,	'LOAD_NAME'   , 'STORE_NAME'   , 'DELETE_NAME'
	}
	@staticmethod
	def get_glbs(code) :
		'find func global'
		# for global references, we need to inspect code as code.co_names contains much more
		def gather_codes(code) :                                               # gather all code objects as there may be function defs within a function
			if code in codes : return
			codes[code] = None
			for c in code.co_consts :
				if isinstance(c,types.CodeType) : gather_codes(c)
		codes = {}                                                             # use dict to retain order so order is stable
		gather_codes(code)
		glb_names = {}                                                         # use dict to retain order so order is stable
		for c in codes :
			for i in dis.Bytecode(c) :
				if i.opname in Serialize.have_name : glb_names[i.argval] = None
		return glb_names

	def val_src(self,name,val) :
		if not name :
			try                   : name = val.__name__
			except AttributeError : pass
		if name :
			if name in self.seen : return
			self.seen.add(name)
		if isinstance(val,types.ModuleType) :
			if name==val.__name__ : self.src_lst.append(f'import {val.__name__}'          )
			else                  : self.src_lst.append(f'import {val.__name__} as {name}')
		elif hasattr(val,'__module__') and hasattr(val,'__qualname__') and val.__module__ not in self.by_values :
			if '.' in val.__qualname__ :
				# use {name} to temporarily hold the module as it is guaranteed to be an available name
				self.src_lst.append(f'import {val.__module__} as {name} ; {name} = {name}.{val.__qualname__}')
			else :
				if name==val.__qualname__ : self.src_lst.append(f'from {val.__module__} import {val.__qualname__}'          )
				else                      : self.src_lst.append(f'from {val.__module__} import {val.__qualname__} as {name}')
		elif isinstance(val,types.FunctionType) :
			self.func_src(name,val)
		elif name :
			if self.has_repr(val) :
				if name : self.src_lst.append(f'{name} = {val!r}')
			elif val.__class__.__module__ not in self.by_values :
				# by default, use the broadest serializer available : pickle
				# inconvenient is that the resulting source is everything but readable
				# protocol 0 is the less unreadable, though, so use it
				# use {name} to temporarily hold pickle.loads as it is guaranteed to be an available name
				val_str = pickle.dumps(val,protocol=0).decode()
				self.src_lst.append(f'from pickle import loads as {name} ; {name} = {name}({val_str!r}.encode())')
			else :
				raise ValueError(f'dont know how to serialize {name} = {val}')

	def gather_ctx(self,name) :
		for d in self.ctx :
			if name not in d : continue
			try    : val = d[name]
			except : self.in_sets.add(name)
			else   : self.val_src(name,val)
			return
		# name may be a builtins or it does not exist, in both case, do nothing and we'll have a NameError exception at runtime if name is accessed and it is not a builtin

	def get_first_line(self,name,func,first_line) :
		#
		# if this does not work, the we have to mimic inspect.Signature.__str__ and replace calls to repr by smarter self.get_src calls
		if not self.has_repr(func.__defaults__) : raise ValueError('defaults for func {func.__qualname__} are too difficult to analyze')
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
		file_name = code.co_filename
		analyze(file_name)
		file_src       = srcs      [file_name]
		file_end_lines = end_liness[file_name]
		first_line_no  = code.co_firstlineno-1
		end_line_no    = file_end_lines.get(first_line_no)
		assert end_line_no,f'{file_name}:{first_line_no+1} : cannot find def {name}'
		#
		if func.__globals__ not in self.ctx : self.ctx.append(func.__globals__)
		for glb_var in self.get_glbs(code) :
			self.gather_ctx(glb_var)
		#
		self.src_lst.append( self.get_first_line( name , func , file_src[first_line_no] ) ) # first line
		self.src_lst.extend( file_src[ first_line_no+1 : end_line_no ]                    ) # other lines
