# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

from .utils import Job,mk_shell_str

class Job(Job) :
	def gen_script(self) :
		self.write_cmd()
		#
		call_line  = [ '"$(type -p gdb)"' ]
		call_line += ( '-ex' , '"set environment TMPDIR $TMPDIR"' )                                              # gdb keeps the environment except TMPDIR
		if self.stdin or self.stdout :
			if self.stdin or self.stdout :                                                                       # in case of redirections, define alias r to run with adequate redirections
				if True        : run  = 'run'
				if self.stdin  : run += ' <'+self.stdin
				if self.stdout : run += ' >'+self.stdout
				call_line += ( '-ex' , mk_shell_str('define r') )
				call_line += ( '-ex' , mk_shell_str(run)        )
				call_line += ( '-ex' , 'end'                    )
				self.stdin  = None                                                                               # gdb handles redirection
				self.stdout = None                                                                               # .
		call_line += ( '--args' , *(mk_shell_str(c) for c in self.interpreter) , mk_shell_str(self.cmd_file()) )
		#
		self.autodep_method = 'none'
		preamble,line       = self.starter(*call_line)
		return self.gen_preamble() + preamble + line + '\n'

def gen_script(**kwds) :
	return Job(kwds).gen_script()
