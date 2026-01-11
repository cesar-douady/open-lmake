# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

'this module contains classes and functions that are of general use, not directly linked to lmake'

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
