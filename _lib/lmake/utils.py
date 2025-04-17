# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
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
		except KeyError : pass
		raise AttributeError(attr)
	def __setattr__(self,attr,val) :
		try             : self[attr] = val ; return
		except KeyError : pass
		raise AttributeError(attr)
	def __delattr__(self,attr) :
		try             : del self[attr] ; return
		except KeyError : pass
		raise AttributeError(attr)

def multi_strip(txt) :
	r"""
		multi_strip(txt) looks like txt.strip(), but in addition, the common blank prefix on each line is also suppressed.
		This allows to easily define multi-line text that have no reason to be indented in an indented context while keeping a nice looking code.
		Usage :
		gen_c = multi_strip(r'''
			int main() {                           // this is the first line, not indented
				printf("this is nice looking\n") ; // this is indented once
			}                                      // this is the last line (no ending newline), not indented
		''')
	"""
	ls = txt.split('\n')
	while ls and ( not ls[ 0] or ls[ 0].isspace() ) : ls = ls[1:  ]
	while ls and ( not ls[-1] or ls[-1].isspace() ) : ls = ls[ :-1]
	if not ls : return ''
	l0 = ls[0]
	while l0[0].isspace() and all(not l or l[0]==l0[0] for l in ls) :
		ls = [ l[1:] for l in ls ]
		l0 = ls[0]
	return ''.join(l+'\n' for l in ls)

def indent(txt,pfx='\t') :
	'''indent txt by adding pfx in front of each line not composed entirely of spaces'''
	lines = txt.split('\n')
	for i,line in enumerate(lines) :
		if line and not line.isspace() : lines[i] = pfx+line
	return '\n'.join(lines)
