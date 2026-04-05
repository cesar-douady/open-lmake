# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

# XXX> : as long as python2 is supported, this file must be python2 compatible

'this module contains classes and functions that are of general use, not directly linked to lmake'

import os
import os.path as osp

__all__ = ( 'pdict' , 'config_dict' )

class pdict(dict) :
	'''
		This class is a dict whose items can also be accessed as attributes.
		This greatly improves readability of configurations.
		Usage :
		d = pdict(a=1,b=2)
		d                  --> {'a':1,'b':2}
		d['a']             --> 2
		d.a                --> 2
		d.c = 3
		d.c                --> 3
	'''
	@staticmethod
	def mk_deep(x) :
		'''
			This is a factory function that deeply transform a dict into a pdict.
			Deeply means that values are recursively tranformed and that list/tuple are also traversed.
			Usage :
			d = { 'a':1 , 'b':({'x':2},{'y':3}) }
			dd = pdict.mk_deep(d)
			dd.b[0].x                             --> 2 # dd.b[0] is a pdict, like dd
		'''
		if isinstance(x,dict ) : return pdict({ k : pdict.mk_deep(v) for k,v in x.items() })
		if isinstance(x,tuple) : return tuple(      pdict.mk_deep(v) for   v in x         )
		if isinstance(x,list ) : return [           pdict.mk_deep(v) for   v in x         ]
		return x
	def __getattr__(self,attr) :
		try             : return self[attr]
		except KeyError : raise AttributeError(attr)
	def __setattr__(self,attr,val) :
		try             : self[attr] = val
		except KeyError : raise AttributeError(attr)
	def __delattr__(self,attr) :
		try             : del self[attr]
		except KeyError : raise AttributeError(attr)

class config_dict :
	'''
		The purpose of this object is to behave as a dict except that each field/sub-field is stored in a separate file.
		This allows to access it and only be rerun if accessed field is modified.
		Arguments :
		- base        : the base name of the config on disk
		- dir_ext     : the extension used to create the dir storing the field hierarchy
		- val_ext     : the extension used to record values
		- create      : if provided, the disk tree is created from the passed object
		- create_from : if provided, the disk tree is create from reading passed file name assuming it contains 'config = ...'
		- list_ok     : if passed as a false value, list/tuple are considered terminal rather than expanded as individual per item files
	'''
	__dir_ext__     = 'config_dir'
	__sub_dir_ext__ = 'sub_dir'
	__val_ext__     = 'val.py'
	#
	def __str__ (self) : return self.__class__.__qualname__+'('+     self.__base__ +                           ')'
	def __repr__(self) : return self.__class__.__qualname__+'('+repr(self.__base__)+','+repr(self.__dir_ext__)+')'
	#
	def __init__(self,base,dir_ext=None,val_ext=None,**kwds) :
		self.__base__    = base
		self.__dir_ext__ = dir_ext or self.__dir_ext__
		self.__val_ext__ = val_ext or self.__val_ext__
		if 'create_from' in kwds :
			fn  = kwds['create_from']
			dct = {}
			exec( open(fn).read() , None , dct )
			if 'config' not in dct : raise NameError('config not found in '+fn)
			kwds['create'] = dct['config']
		if 'create' not in kwds : return
		list_ok = kwds.get('list_ok',True)
		#
		if '/' in self.__base__ : os.makedirs( osp.dirname(self.__base__) , exist_ok=True )
		cfg = kwds['create']
		cls = cfg.__class__
		if list_ok and isinstance(cfg,(list,tuple)) : cfg = { i:v for i,v in enumerate(cfg) }
		if isinstance(cfg,dict) :
			open(self.__base__+'.'+self.__val_ext__,'w').write( 'cls = '+cls.__qualname__+'\nmanifest = '+repr(tuple(cfg.keys()))+'\n' )
			for key,val in cfg.items() :
				self.__class__( self.__base__+'.'+self.__dir_ext__+'/'+str(key) , self.__sub_dir_ext__ , create=val , list_ok=list_ok )
		else :
			open(self.__base__+'.'+self.__val_ext__,'w').write( 'val = '+repr(cfg)+'\n' )
	#
	def __getitem__(self,key ) : return self.__class__(self.__base__+'.'+self.__dir_ext__+'/'+str(key),self.__sub_dir_ext__,self.__val_ext__)
	def __getattr__(self,attr) : return self[attr]
	#
	def __call__(self) :
		val_file = self.__base__+'.'+self.__val_ext__
		dct      = {}
		exec( open(val_file).read() , None , dct )
		#
		if 'val' in dct :
			return dct['val']
		#
		if 'manifest' in dct :
			if 'cls' not in dct : raise NameError('cls not found in '+val_file)
			cls      = dct['cls'     ]
			manifest = dct['manifest']
			if   issubclass(cls, dict       ) : vals = cls()
			elif issubclass(cls,(list,tuple)) : vals = [None]*len(manifest)
			else                              : raise TypeError('unexpected class '+cls.__qualname__+' in '+val_file)
			for key in manifest :
				vals[key] = self[key]()
			if issubclass(cls,(list,tuple)) : vals = cls(vals)
			return vals
		raise NameError('unrecognized format for '+val_file)
	#
	def __iter__(self) :
		val_file = self.__base__+'.'+self.__val_ext__
		dct      = {}
		exec( open(val_file).read() , None , dct )
		if 'manifest' not in dct : raise ValueError(str(self)+' is not iterable')
		return iter(dct['manifest'])
