# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import os
import os.path as osp
import re
import textwrap

import lmake

no_quote     = re.compile(r'[\w+,-./:=@^]*')
single_quote = re.compile(r"[^']*"         )
double_quote = re.compile(r'[^!"$\\`]*'    )
def mk_shell_str(s) :
	if no_quote    .fullmatch(s) : return     s
	if single_quote.fullmatch(s) : return "'"+s+"'"
	if double_quote.fullmatch(s) : return '"'+s+'"'
	else                         : return "'"+s.replace("'",r"'\''")+"'"

def add_nl(s) :
	if not s or s[-1]=='\n' : return s
	else                    : return s+'\n'

def no_nl(s) :
	if s and s[-1]=='\n' : return s[:-1]
	else                 : return s

class Job :

	auto_mkdir      = False
	chroot_dir      = None
	cwd             = None
	lmake_root      = None
	lmake_view      = None
	readdir_ok      = False
	repo_view       = None
	simple_cmd_line = None
	source_dirs     = None
	stdin           = None
	stdout          = None
	sub_repo        = None
	tmp_dir         = '/tmp'
	tmp_view        = None

	def __init__(self,attrs) :
		self.env      = {}
		self.keep_env = ()
		for k,v in attrs.items() : setattr(self,k,v)
		self.env['SEQUENCE_ID'] = str(0)
		self.env['SMALL_ID'   ] = str(0)
		self.keep_env           = (*self.keep_env,'DISPLAY','LMAKE_HOME','LMAKE_SHLVL','XAUTHORITY','XDG_RUNTIME_DIR')
		#
		assert not ( self.is_python and self.simple_cmd_line ) , "cannot handle simple cmd with python interpreter"

	#
	# functions for generating cmd file
	#

	def gen_shebang(self) :
		# shebang is not strictly necessary, but at least, it allows editors (e.g. vi) to do syntax coloring
		res = f"#!{' '.join(self.interpreter)}\n"
		if len(self.interpreter)>2 or len(res)>256 :                                                    # inform user we do not use the sheebang line if it actually does not work ...
			res += '# the shebang line above is informative only, interpreter is called explicitly\n' ; # ... just so that it gets no headache wondering why it works with a apparently buggy line
		return res

	def gen_cmd(self) :
		return (
			self.gen_shebang()
		+	add_nl(self.preamble)
		+	add_nl(self.cmd     )
		)

	def interpreter_line(self) :
		return ' '.join(mk_shell_str(c) for c in self.interpreter)

	#
	# functions for generating script file
	#

	def gen_pre_actions(self) :
		res = ''
		if any(a=='uniquify' for a in self.pre_actions.values()) :
			res += textwrap.dedent('''
				uniquify() {
					if [ -f "$1" -a $(stat -c%h "$1" 2>/dev/null||echo 0) -gt 1 ] ; then
						echo warning : uniquify "$1" >&2
						mv "$1" "$1.$$" ; cp -p "$1.$$" "$1" ; rm -f "$1.$$"
					fi
				}
			'''[1:])                                                                           # strip initial \n
		if any(a in ('unlink_warning','unlink_polluted') for a in self.pre_actions.values()) :
			res += textwrap.dedent('''
				rm_warning() {
					if [ -f "$2" ] ; then
						echo $1 : rm "$2" >&2
						rm -f "$2"
					fi
				}
			'''[1:])                                                                           # strip initial \n
		actions = []
		for f,a in self.pre_actions.items() :
			f = mk_shell_str(f)
			if   a=='mkdir'           : actions.append(( 'mkdir -p'            , f                ))
			elif a=='rmdir'           : actions.append(( 'rmdir'               , f+' 2>/dev/null' ))
			elif a=='unlink'          : actions.append(( 'rm -f'               , f                ))
			elif a=='unlink_warning'  : actions.append(( 'rm_warning warning'  , f                ))
			elif a=='unlink_polluted' : actions.append(( 'rm_warning polluted' , f                ))
			elif a=='uniquify'        : actions.append(( 'uniquify'            , f                ))
		w = max( (len(a) for a,_ in actions) , default=0 )
		for a,f in actions :
			res += f'{a:{w}} {f}\n'
		if res : res = '#\n' + res
		return res

	def starter(self,*args,enter=False,**kwds) :
		autodep = f'{osp.dirname(osp.dirname(osp.dirname(__file__)))}/bin/lautodep'
		#
		preamble = '#\n'
		if enter : preamble +=  'export LMAKE_HOME="$HOME"\n'                            # use specified value in job, but original one in entered shell
		if enter : preamble +=  'export LMAKE_SHLVL="${SHLVL:-1}"\n'                     # .
		if True  : preamble += f'export LMAKE_DEBUG_KEY={mk_shell_str(self.key)}\n'
		if True  : preamble +=  'export XAUTHORITY="${XAUTHORITY:-$HOME/.Xauthority}"\n'
		#
		keep_env = list(self.keep_env)
		if self.env : preamble += '#\n'
		for k,v in self.env.items() :
			if k=='UID' : preamble += f'export {k}={mk_shell_str(v)} 2>/dev/null || :\n' # UID is read-only on some systems
			else        : preamble += f'export {k}={mk_shell_str(v)}\n'
			keep_env.append(k)
		#
		simple = True
		res    = ''
		if True             :          res =         res+mk_shell_str(autodep)
		if self.auto_mkdir  : simple , res = False , res+ ' -a'
		if self.chroot_dir  : simple , res = False , res+f' -c{mk_shell_str(     self.chroot_dir            )}'
		if self.cwd         : simple , res = False , res+f' -d{mk_shell_str(     self.cwd                   )}'
		if self.readdir_ok  :          res =         res+ ' -D'
		if True             :          res =         res+f' -e{mk_shell_str(repr(keep_env                  ))}'
		if True             :          res =         res+f' -k'
		if True             :          res =         res+f' -l{                  self.link_support           }'
		if self.lmake_root  : simple , res = False , res+f' -r{mk_shell_str(     self.lmake_root            )}'
		if self.lmake_view  : simple , res = False , res+f' -L{mk_shell_str(     self.lmake_view            )}'
		if True             :          res =         res+f' -m{                  self.autodep_method         }'
		if True             :          res =         res+f" -o{mk_shell_str(     self.debug_dir+'/accesses' )}"
		if self.repo_view   : simple , res = False , res+f' -R{mk_shell_str(     self.repo_view             )}'
		if self.source_dirs : simple , res = False , res+f' -s{mk_shell_str(repr(self.source_dirs          ))}'
		if self.tmp_dir     :          res =         res+f' -t{mk_shell_str(     self.tmp_dir               )}'
		if self.tmp_view    : simple , res = False , res+f' -T{mk_shell_str(     self.tmp_view              )}'
		if self.views       : simple , res = False , res+f' -V{mk_shell_str(repr(self.views                ))}'
		if True             :          res =         res+ ' -- \\\n'
		#
		if True        : res += ' '.join(x for x in args)                                # must be before redirections to files if args contains redirections
		#
		if self.stdin  : res += f' <{mk_shell_str(self.stdin )}'
		if self.stdout : res += f' >{mk_shell_str(self.stdout)}'
		#
		if simple : res = '#\n# following line can be suppressed to get lautodep out\n' + res
		else      : res = '#\n'                                                         + res
		#
		return preamble,res

	def gen_init(self) :
		res = textwrap.dedent(f'''
			#!/bin/bash
			#
			cd {mk_shell_str(lmake.top_repo_root)}
		'''[1:])                                   # strip initial \n
		return res

	def gen_shell_cmd( self , trace=False , enter=False , **kwds ) :
		res = ''
		if True            : res += self.gen_shebang()
		if trace           : res += '#\n'
		if trace and enter : res += '(\n'
		if trace           : res += 'set -x\n'
		if True            : res += add_nl(self.cmd)
		if trace and enter : res += ')\n'
		if enter           :
			res += textwrap.dedent(f'''
				#
				export HOME="$LMAKE_HOME"   ; unset LMAKE_HOME
				export SHLVL="$LMAKE_SHLVL" ; unset LMAKE_SHLVL
				[ "$LMAKE_DEBUG_STDIN"  ] && exec <"$LMAKE_DEBUG_STDIN"
				[ "$LMAKE_DEBUG_STDOUT" ] && exec >"$LMAKE_DEBUG_STDOUT"
				exec {self.interpreter_line()} -i
			'''[1:])                              # strip initial \n
		return res

	def gen_py_cmd( self , runner=None , **kwds ) :
		preamble,cmd = self.cmd.rsplit('\n',1)
		if runner :
			assert cmd[-1]==')'                                                     # cmd must be of the form func(args,...) with 0 or more args
			func,args = cmd.split('(',1)
			args      = args[:-1]
			func_args = func
			if args : func_args = f'{func},{args}'                                  # generate func,args,...
			else    : func_args = func                                              # handle no args case
			cmd = textwrap.dedent(f'''
				import sys as lmake_sys
				lmake_sys.path.append({osp.dirname(osp.dirname(lmake.__file__))!r}) # ensure lmake lib is accessible
				from {runner} import run_py as lmake_runner
				lmake_sys.path.pop()                                                # restore
				lmake_runner({self.debug_dir!r},{self.static_deps!r},{func_args})
			'''[1:])                                                                # strip initial \n
		fix_path = "import sys ; sys.path[0] = '' ; del sys\n"                      # ensure same sys.path as if run with -c, del sys to ensure total transparency
		if not preamble.startswith(fix_path) : preamble = fix_path+'#\n'+preamble   # if cmd was already in a script, it already contains the fix
		return (
			self.gen_shebang()
		+	preamble+'\n'
		+	cmd
		)

	def cmd_file(self) :
		return self.debug_dir+'/cmd'

	def cmd_line(self) :
		if self.simple_cmd_line : return self.simple_cmd_line
		else                    : return ( *self.interpreter , self.cmd_file() )

	def gen_start_line(self,**kwds) :
		preamble,line = self.starter( *(mk_shell_str(c) for c in self.cmd_line()) , **kwds )
		return preamble+line+'\n'                                                            # do not use exec to launch lautodep so as to keep stdin & stdout open

	def gen_lmake_env(self,enter=False,**kwds) :
		res = ''
		if enter :
			def add_env(key,val) :
				nonlocal res
				res += f'export {key}={val}\n'
				self.keep_env.append(key)
			if self.stdin  : add_env('LMAKE_DEBUG_STDIN' ,'/proc/$$/fd/0')
			if self.stdout : add_env('LMAKE_DEBUG_STDOUT','/proc/$$/fd/1')
		if res : res = '#\n'+res
		return res

	def gen_preamble(self,**kwds) :
		res  = self.gen_init       (      )
		res += self.gen_pre_actions(      )
		res += self.gen_lmake_env  (**kwds)
		return res

	def write_cmd(self,**kwds) :
		if self.is_python : cmd = self.gen_py_cmd   (**kwds)
		else              : cmd = self.gen_shell_cmd(**kwds)
		cmd_file = self.cmd_file()
		os.makedirs(osp.dirname(cmd_file),exist_ok=True)
		open(cmd_file,'w').write(cmd)

	def gen_script(self,**kwds) :
		if not self.simple_cmd_line : self.write_cmd(**kwds)
		res  = self.gen_preamble  (**kwds)
		res += self.gen_start_line(**kwds)
		return res
