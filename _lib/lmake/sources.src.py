# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import os.path    as _osp
import subprocess as _sp

from clmake import root_dir                                                    # if not in an lmake repo, root_dir is not set to current dir

def manifest_sources(manifest='Manifest',**kwds) :
	'''
		read manifest, filtering out comments :
		- comments start with #
		- they must be separated from file with spaces
		- files must start and end with non-space and cannot start with #
		- note that files can contain spaces (except first and last character) and # as long as they are not preceded by spaces
		kwds are ignored which simplifies the usage of auto_sources
	'''
	import re
	line_re = re.compile(r'\s*(?P<file>[^#\s](.*\S)?)?\s+(#.*)?\n?')
	try                      : stream = open(manifest)
	except FileNotFoundError : raise NotImplementedError(f'cannot find {manifest}')
	srcs = [ f for f in ( line_re.fullmatch(l).group('file') for l in stream ) if f ]
	if 'Lmakefile.py' not in srcs : raise NotImplementedError(f'cannot find Lmakefile.py in git files')
	return srcs

_git = '$GIT'                                                                # value is substitued at installation configuration
def git_sources( recurse=True , ignore_missing_submodules=False , **kwds ) :
	'''
		gather git controled files.
		recurse to sub-modules    if recurse                   is True
		ignore missing submodules if ignore_missing_submodules is True
		kwds are ignored which simplifies the usage of auto_sources
	'''
	def run( cmd , dir=None ) :
		# old versions of git (e.g. 1.8) require an open stdin (although is is not used)
		return _sp.run( cmd , cwd=dir , check=True , stdin=_sp.DEVNULL , stdout=_sp.PIPE , stderr=_sp.DEVNULL , universal_newlines=True ).stdout.splitlines()
	#
	# compute directories
	#
	git_base  = root_dir
	git_path_s = ''
	while git_base!='/' and not _osp.exists(_osp.join(git_base,'.git')) :
		git_base   = _osp.dirname(git_base)
		git_path_s = '../' + git_path_s
	if git_base =='/' : raise NotImplementedError('not in a git repository')
	git_base_s = _osp.join(git_base ,'')
	root_dir_s = _osp.join(root_dir ,'')
	assert root_dir_s.startswith(git_base_s),f'found git dir {git_base} is not a prefix of root dir {root_dir}'
	repo_dir_s = root_dir_s[len(git_base_s):]
	#
	# compute file lists
	#
	if recurse :
		# compute submodules
		# old versions of git (e.g. 1.8) do not support submodule command when not launched from top nor $displaypath
		submodules = run( (_git,'submodule','--quiet','foreach','--recursive','echo $toplevel/$sm_path') , git_base ) # less file accesses than git submodule status
		submodules = [ sm[len(root_dir_s):] for sm in submodules if _osp.join(sm,'').startswith(root_dir_s) ]
		try :
			srcs = run((_git,'ls-files','--recurse-submodules'))
			for sm in submodules :
				sm_admin = _osp.join(sm,'.git')
				if   _osp.isfile(sm_admin)         : srcs.append(sm_admin)
				elif not ignore_missing_submodules : raise FileNotFoundError(f'cannot find {sm_admin}')
		except _sp.CalledProcessError :
			srcs = run((_git,'ls-files'))                                  # old versions of git ls-files (e.g. 1.8) do not support the --recurse-submodules option
			srcs_set = set(srcs)
			for sm in submodules :                                         # proceed top-down so that srcs_set includes its sub-modules
				srcs_set.remove(sm)
				try :
					sub_srcs = run( (_git,'ls-files') , root_dir_s+sm  )
					sm_s     = _osp.join(sm,'')
					srcs_set.update( sm_s+f for f in sub_srcs )
					srcs_set.add   ( sm_s+'.git'              )
				except _sp.CalledProcessError :
					if not ignore_missing_submodules : raise
			srcs = list(srcs_set)
			srcs.sort()                                                    # avoid random order
	else :
		srcs = run((_git,'ls-files'))
	#
	#  update source_dirs
	#
	dot_git =  _osp.join(git_base,'.git')
	if _osp.isdir(dot_git) :
		git_dir_s = git_path_s + '.git/'
	else : 
		srcs.append(git_path_s + '.git')
		git_dir_s = open(dot_git).read().replace('gitdir:','').strip() + '/'
		common_dir = _osp.join(git_dir_s,'commondir')
		if _osp.exists(common_dir):
			cd = open(common_dir).read().strip()
			srcs.append(_osp.normpath(_osp.join(git_dir_s,cd)) + '/')
	srcs.append(git_dir_s)
	return srcs

def auto_sources(**kwds) :
	'''
		auto-determine used source control
		kwds can be passed to underlying source control
	'''
	for ctl in ( manifest_sources , git_sources ) :
		try                        : return ctl(**kwds)
		except NotImplementedError : pass
	raise RuntimeError(f'no source control found')
