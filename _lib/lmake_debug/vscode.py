# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import json
import os
import os.path    as osp
import subprocess as sp

import lmake
from lmake.utils import multi_strip

from .       import utils
from .utils  import mk_shell_str
from pathlib import Path

def run(*args) :
	return sp.check_output(args,universal_newlines=True)

class Job (utils.Job) :

	Extensions = (
		'ms-python.python'
	,	'ms-vscode.cpptools'
	,	'coolchyni.beyond-debug' # necessary to provide a Python/C debug experience
	)

	Associations = {}

	Exclude = {
		'.git*/**' : True
	}

	def config(self) :
		root = self.root_view or lmake.root_dir
		cwd  = osp.join(root,self.cwd) if self.cwd else root
		return {
			'folders': [
				{ 'path' : root }
			]
		,	'settings': {
				'files.associations' : {
					'cmd' : 'python'
				,	**self.Associations
				}
			,	'files.exclude' : {
					'.vscode/**' : True
				,	**self.Exclude
				}
			,	'telemetry.enableTelemetry' : False
			,	'telemetry.telemetryLevel'  : 'off'
			}
		,	'launch' : {
				'configurations' : [             # env is not set as it is already set by lautodep before vscode is launched
					{	'type'       : 'debugpy'
					,	'request'    : 'launch'
					,	'name'       : self.name
					,	'python'     : self.interpreter[0 ]
					,	'pythonArgs' : self.interpreter[1:]
					,	'program'    : self.cmd_file()
					,	'console'    : 'integratedTerminal'
					,	'cwd'        : cwd
					,	'subProcess' : True
					}
				,	{	'type'      : 'by-gdb'
					,	'request'   : 'attach'
					,	'name'      : 'Attach C/C++'
					,	'program'   : self.interpreter[0]
					,	'cwd'       : cwd
					,	'processId' : 0          # dynamically set when attaching
					}
				]
			}
		,	'extensions' : {
				'recommendations' : [
					self.Extensions
				]
			}
		}

	def gen_workspace(self) :
		return json.dumps( self.config() , indent=4 , separators=(' , ',' : ') )

	def gen_script(self) :
		#
		# sanity checks
		#
#		vscode_exe = shutil.which('code')
		if not self.is_python : raise ValueError       ('cannot debug shell job with vscode')
#		if not vscode_exe     : raise FileNotFoundError('cannot find code'                  )
		#
		# install necessary extensions
		#
#		exts = set(run('code','--list-extensions').split())
#		for e in self.Extensions :
#			if e not in exts : run('code','--install-extension',mk_shell_str(e))
		#
		# generate ancillary files
		#
		self.write_cmd(runner='lmake_debug.runtime.vscode')
		workspace     = self.debug_dir+'/vscode/ldebug.code-workspace'
		user_data_dir = self.debug_dir+'/vscode/user'
		ext_dir       = str(Path.home()/'.vscode/extensions')
		os.makedirs(osp.dirname(workspace),exist_ok=True)
		open(workspace,'w').write(self.gen_workspace())
		#
		# generate vscode call line
		#
		call_line  = ['"$(type -p code)"','-n','-w','--password-store=basic']
		call_line += ( '--user-data-dir'  , mk_shell_str(user_data_dir  )                           )
		call_line += ( '--extensions-dir' , mk_shell_str(ext_dir        )                           )
		call_line += (                      mk_shell_str(d              ) for d in self.static_deps )
		call_line += (                      mk_shell_str(self.cmd_file())                          ,)
		call_line += (                      mk_shell_str(workspace      )                          ,)
		#
		# generate script
		#
		self.cwd            = ''                      # cwd is handled in vscode config
		self.autodep_method = 'none'                  # XXX : fix incompatibilities between lautodep and vscode
		preamble,line       = self.starter(*call_line)
		return self.gen_preamble() + preamble + line + '&\n'

def gen_script(**kwds) :
	return Job(kwds).gen_script()
