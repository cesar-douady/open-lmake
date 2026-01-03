# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

# This is a very simple proof of concept to show how C++20 modules can be supported with no naming convention between file names and module names.
# The principle is to build a mapping logical-name -> physical-name in the form of symbolic links in the top level map_dir directory.
# This way, it can be used during compilation with suitable mapping

# It is solely validated on the inputs derived from : https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p1689r5.html.
# The only difference is that _m has been added to module names to show that they is no relation between physical names and logical names.

# In a real world flow one could/should:
# - Generate the mapping logical-name -> physical-name in a hiearchical way to avoid reconstructing the world each time a file is added/removed
#   (although this is pretty fast).
# - Use a conformant dep-scanning tool when one is available/identified.
# - Implement a true compilation flow.

import json
import os
import subprocess as sp
import sys

import lmake

from lmake.rules import PyRule

gxx = 'g++'

# generate mapping logical_name -> physical_name in the form of dir map_dir containing symbolic links of the form :
# map_dir/logical_name.gcm -> physical_name.gcm
class GenGcms(PyRule) :
	targets = { 'GCM' : r'map_dir/{File*:.*}.gcm' }
	deps    = { 'LST' :  'LMAKE/manifest'         }
	def cmd() :
		#
		# gather list of all source file
		#
		modules = [ m.rsplit('.',1)[0] for m in open(LST) if m.endswith('.mpp\n') ]
		#
		# generate mapping
		#
		for phys_name in modules :
			try                      : log_name = open(f'{phys_name}.log_name').read().strip()
			except FileNotFoundError : pass
			else                     : os.symlink(f'../{phys_name}.gcm',GCM(log_name)) # note that we do not need the target to be up-to-date (not even existing) to create the link

# mimic a dep-scanning tool based on gcc -M to extract module imports/exports
# replace by a conformant tool when one is identified
class GenDeps(PyRule) :
	targets = {
		'DEPS' :   r'{File:.*}.deps'
	,	'LOG'  : (  '{File   }.log_name' , 'optional' ) # only contains the export info (if any), so that full map is only reconstructed when export module changes
	}                                                   # contains the deps as specified in https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p1689r5.html
	deps    = { 'SRC'  :  '{File   }.mpp'  }
	def cmd() :
		#
		# gather deps from gcc -M
		#
		tmp_deps_file = f"{os.environ['TMPDIR']}/deps"
		sp.run( (gxx,'-E','-MD',f'-MF{tmp_deps_file}','-fmodules-ts','-std=c++20','-xc++',SRC) , stdout=sp.DEVNULL , check=True )
		x     = open(tmp_deps_file).read()
		print('raw gcc -M output:')
		print(x)
		lines = x.splitlines()
		#
		# generate a conformant description (https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p1689r5.html)
		#
		deps = {}
		for i,l in enumerate(lines) :
			if 'provides' not in deps :
				if '.c++m:' in l :
					deps['provides'] = [ {'logical-name':l.split('.c++m:',1)[0]} ]
			if 'imports' not in deps :
				if l.startswith('CXX_IMPORTS +=') :
					deps['imports'] = []
					for j in range(i,len(lines)) :
						for w in lines[j].split() :
							if w.endswith('.c++m') :
								deps['imports'].append({'logical-name':w[:-5]})
						if w!='\\' : break
		json.dump(deps,fp=open(DEPS,'w'))
		if 'provides' in deps :
			print(deps['provides'][0]['logical-name'],file=open(LOG,'w'))

class Compile(PyRule) :
	targets = {
		'OBJ' :   r'{File:.+}.o'
	,	'GCM' : (  '{File   }.gcm' , 'optional' )
	}
	deps = {
		'SRC'  : '{File}.mpp'
	,	'DEPS' : '{File}.deps'
	}
	def cmd() :
		#
		# load deps
		#
		deps = json.load(open(DEPS))
		#
		# compute map file for gcc
		#
		map_file = f"{os.environ['TMPDIR']}/map"
		with open(map_file,'w') as fd :
			if 'imports' in deps :
				gcm_names = []
				for e in deps['imports'] :
					log_name = e['logical-name']
					gcm_name = f'map_dir/{log_name}.gcm'
					print( log_name , gcm_name , file=fd )
					gcm_names.append(gcm_name)
				lmake.depend( gcm_names , follow_symlinks=True , read=True )                                 # for perf only : avoid serial dep discovery by creating deps all at once
			if 'provides' in deps :
				print( deps['provides'][0]['logical-name'] , GCM , file=fd )
		print('map file:')
		sys.stdout.write(open(map_file).read())
		print()
		#
		# call gcc
		#
		lmake.check_deps()                                                                                   # for perf only : ensure known deps are available before potentially long processing
		cmd_line = (gxx,'-c','-xc++','-std=c++20','-fmodules-ts',f'-fmodule-mapper={map_file}','-o',OBJ,SRC)
		print('cmd line:')
		print(' '.join(cmd_line))
		sp.run( cmd_line , check=True )
