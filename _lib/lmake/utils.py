# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

class pdict(dict) :
	'This class is a dict whose items can also be accessed as attributes.'
	'This greatly improves readability of configurations'
	@staticmethod
	def mk_deep(x) :
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

def multi_strip(txt) :
	'in addition to stripping its input, this function also suppresses the common blank prefix of all lines'
	ls = txt.split('\n')
	while ls and ( not ls[ 0] or ls[ 0].isspace() ) : ls = ls[1:  ]
	while ls and ( not ls[-1] or ls[-1].isspace() ) : ls = ls[ :-1]
	if not ls : return ''
	l0 = ls[0]
	while l0[0].isspace() and all(not l or l[0]==l0[0] for l in ls) :
		ls = [ l[1:] for l in ls ]
		l0 = ls[0]
	return ''.join(l+'\n' for l in ls)

def find_cc_ld_library_path(cc) :
	import subprocess as sp
	import os.path    as osp
	return sp.run( (osp.dirname(osp.dirname(osp.dirname(__file__)))+'/bin/find_cc_ld_library_path',cc) , stdout=sp.PIPE , check=True , universal_newlines=True ).stdout.strip()
