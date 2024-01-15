# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import os
import os.path as osp
import re
import sys
import sysconfig
import tarfile
import zipfile
from subprocess import run,DEVNULL,STDOUT

gcc = os.environ.get('CC','gcc')

import lmake
from lmake       import config,pdict
from lmake.rules import Rule,AntiRule

if 'slurm' in lmake.backends :
	backend = 'slurm'
	config.backends.slurm = {
		'use_nice'          : True
	,	'n_max_queued_jobs' : 10
	}
else :
	backend = 'local'

config.caches.dir = {
	'tag'  : 'dir'
,	'repo' : lmake.root_dir
,	'dir'  : osp.dirname(lmake.root_dir)+'/lmake_env-cache'
}

lmake.version = (0,1)

config.console.date_precision = 2

config.local_admin_dir  = '/mnt/data/cdy/LMAKE_LOCAL'
config.remote_admin_dir = '/mnt/data/cdy/LMAKE_REMOTE'

config.link_support = 'full'

config.backends.local.cpu = int(os.cpu_count()*1.5)
config.backends.local.cc  = os.cpu_count()

red_hat_release = None

def dir_guard(file) :
	dir = osp.dirname(file)
	if dir : os.makedirs(dir,exist_ok=True)

#
# generics
#

class BaseRule(Rule) :
	stems = {
		'Dir'  : r'.+?'                # use non-greedy to be protected against dirs ending in .dir in tar file
	,	'DirS' : r'.+/|'               # include trailing / or empty to designate top-level
	,	'File' : r'.+'
	,	'Base' : r'[^/]+'
	,	'Ext'  : r'[^/]+'
	}
	autodep     = 'ptrace'
	backend     = backend
	resources   = { 'mem' : '100M' }
	n_retries   = 1
	start_delay = 2
	n_tokens    = config.backends.local.cpu

class Centos7Rule(BaseRule) :
	environ_cmd = { 'PATH' : '...:/opt/rh/devtoolset-11/root/usr/bin' }
	cache       = 'dir'

class Html(BaseRule) :
	targets = { 'HTML' : '{File}.html' }
	deps    = { 'TEXI' : '{File}.texi' }
	environ_cmd = {
		'LANGUAGE' : ''
	,	'LC_ALL'   : ''
	,	'LANG'     : ''
	}
	cmd = 'texi2any --html --no-split -o {HTML} {TEXI}'

class Unpack(BaseRule) :
	targets = {
		'FILE'     : 'ext/{Dir}.dir/{File*}'
	,	'MANIFEST' : 'ext/{Dir}.manifest'
	}

# it is unacceptable to have a pack inside a pack, as this creates ambiguities
class AntiPackPack(BaseRule,AntiRule) :
	targets = {
		'TAR' : '{Dir}.{DirExt:(patched_)?dir}/{File}.tar.gz'
	,	'ZIP' : '{Dir}.{DirExt               }/{File}.zip'
	}

class Untar(Unpack) :
	deps         = { 'TAR' : 'ext/{Dir}.tar.gz' }
	allow_stderr = True
	def cmd() :
		dir = f'ext/{Dir}.dir'
		n_files = 0
		with tarfile.open(TAR,'r:gz') as tf , open(MANIFEST,'w') as manifest :
			for member in tf.getmembers() :
				if member.isdir() :
					dir_guard(f'{dir}/{member.name}/')
				else :
					tf.extract(member,dir,set_attrs=False)
					if osp.basename(member.name) in ('configure','autogen.sh','arch-gperf-generate') :
						os.chmod(f'{dir}/{member.name}',0o755)
						print('make executable :',f'{dir}/{member.name}',file=sys.stderr)
					print(member.name,file=manifest)
					n_files += 1
		print(f'untar {n_files} files',file=sys.stderr)

class Unzip(Unpack) :
	deps         = { 'ZIP' : 'ext/{Dir}.zip' }
	allow_stderr = True
	def cmd() :
		dir = f'ext/{Dir}.dir'
		n_files  = 0
		with zipfile.ZipFile(ZIP,'r') as zf , open(MANIFEST,'w') as manifest :
			for member in zf.infolist() :
				if member.is_dir() :
					dir_guard(f'{dir}/{member.filename}/')
				else :
					zf.extract(member,dir)
					if ('/'+member.filename).endswith('/configure') :
						os.chmod(f'{dir}/{member.filename}',0o755)
						print('make executable :',f'{dir}/{member.filename}',file=sys.stderr)
					print(member.filename,file=manifest)
					n_files += 1
		print(f'unzip {n_files} files',file=sys.stderr)

class PatchDir(BaseRule) :
	targets = {
		'PATCHED_FILE'     : 'ext/{Dir}.patched_dir/{File*}'
	,	'PATCHED_MANIFEST' : 'ext/{Dir}.patched_manifest'
	}
	deps = {
		'MANIFEST'     : 'ext/{Dir}.manifest'
	,	'PATCH_SCRIPT' : 'ext/{Dir}.patch_script'
	}
	def cmd() :
		sdir = f'ext/{Dir}.dir'
		pdir = f'ext/{Dir}.patched_dir'
		with open(PATCHED_MANIFEST,'w') as pm :
			for f in open(MANIFEST).read().splitlines() :
				src  = f'{sdir}/{f}'
				dest = f'{pdir}/{f}'
				dir_guard(dest)
				open(dest,'w').write(open(src,'r').read())
				print(f,file=pm)
		run((osp.abspath(PATCH_SCRIPT),),cwd=pdir,stdin=DEVNULL,stderr=STDOUT,check=True)

class PatchFile(BaseRule) :
	targets = { 'DST' : 'ext/{DirS}{ File}.patched.{Ext}' }
	deps = {
		'SRC'          : 'ext/{DirS}{File}.{Ext}'
	,	'PATCH_SCRIPT' : 'ext/{DirS}{File}.patch_script'
	}
	def cmd() :
		dir = ('ext/'+DirS)[:-1]
		open(DST,'w').write(open(SRC,'r').read())
		run((osp.abspath(PATCH_SCRIPT),File+'.patched.'+Ext),cwd=dir,stdin=DEVNULL,stderr=STDOUT,check=True)

class ConfigH(BaseRule) :
	targets = {
		'CONFIG_H'   :   'ext/{DirS}config.h'
	,	'SCRATCHPAD' : ( 'ext/{DirS}{File*}'  , '-Match' )
	}
	deps = { 'CONFIGURE' : 'ext/{DirS}configure' }
	cmd  = 'cd ext/{DirS} ; ./configure'

class SysConfigH(Centos7Rule) :
    targets = {
		'H'     : 'sys_config.h'
	,	'TRIAL' : 'trial/{*:.*}'
	}
    deps = { 'EXE' : 'sys_config' }
    cmd  = 'CC={gcc} PYTHON={sys.executable} ./{EXE} 2>&1  >{H}'

opt_tab = {}
class GenOpts(BaseRule) :
	targets = { 'OPTS' : '{File}.opts' }
	def cmd() :
		try : open("pyvenv.cfg")
		except : pass
		res = []
		for key,opts in opt_tab.items() :
			if re.fullmatch(key,File) : res += opts
		res += ('-iquote','.')
		dir = osp.dirname(File)
		while dir :
			res += ('-iquote',dir)
			dir = osp.dirname(dir)
		print(tuple(res),file=open(OPTS,'w'))

# a rule to ensure dir exists
class Marker(BaseRule) :
	prio = 1                            # avoid untar when in a tar dir
	targets = { 'MRKR' : '{DirS}mrkr' }
	def cmd() :
		open(MRKR,'w')

basic_opts_tab = {
	'c'   : ('-g','-O3','-Wall','-Wextra','-pedantic','-fno-strict-aliasing',                   '-std=c99'  )
,	'cc'  : ('-g','-O3','-Wall','-Wextra','-pedantic','-fno-strict-aliasing','-Wno-type-limits','-std=c++20') # on some systems, we there is a warning type-limits
,	'cxx' : ('-g','-O3','-Wall','-Wextra','-pedantic','-fno-strict-aliasing','-Wno-type-limits','-std=c++20') # .
}
for ext,basic_opts in basic_opts_tab.items() :
	class Compile(Centos7Rule) :                                               # note that although class is overwritten at each iteration, each is recorded at definition time by the metaclass
		name    = f'compile {ext}'
		targets = { 'OBJ' : '{File}.o' }
		deps    = {
			'SRC'  : f'{{File}}.{ext}'
		,	'OPTS' : '{File}.opts'
		}
		basic_opts = basic_opts                                                # capture value while iterating (w/o this line, basic_opts would be the final value)
		def cmd() :
			add_flags = eval(open(OPTS).read())
			seen_inc  = False
			missings  = []
			mrkrs     = []
			for x in add_flags :
				if seen_inc and x[0]!='/' :
					if not File.startswith(x+'/') :                            # if x is a dir of File, it necessarily exists
						mrkrs.append(osp.join(x,'mrkr'))                       # gcc does not open includes from non-existent dirs
				seen_inc = x in ('-I','-iquote','-isystem','-idirafter')
			lmake.depend(*mrkrs)
			lmake.check_deps()
			cmd_line = (
				gcc , '-fdiagnostics-color=always' , '-c' , '-fPIC' , '-pthread' , f'-frandom-seed={OBJ}' , '-fvisibility=hidden'
			,	*basic_opts
			,	*add_flags
			,	'-o',OBJ , SRC
			)
			for k,v in os.environ.items() : print(f'{k}={v}')
			print(' '.join(cmd_line),flush=True)                               # in case of live out, we want to have the info early
			run( cmd_line , check=True )
		n_tokens  = config.backends.local.cc
		resources = pdict()
		if True             : resources.mem = '512M'
		if backend=='local' : resources.cc  = 1

class GccRule(Centos7Rule) :
	combine       = ('pre_opts','rev_post_opts')
	pre_opts      = []                                                         # options before inputs & outputs
	rev_post_opts = []                                                         # options after  inputs & outputs, combine appends at each level, but here we want to prepend
	def cmd() :
		cmd_line = (
			gcc , '-fdiagnostics-color=always'
		,	*pre_opts
		,	'-o',TARGET
		,	*deps.values()
		,	*reversed(rev_post_opts)
		)
		print(' '.join(cmd_line))
		run( cmd_line , check=True )

class LinkO(GccRule) :
	pre_opts = ('-r','-fPIC')

class LinkSo(GccRule) :
	pre_opts      = ('-shared-libgcc','-shared','-pthread')
	rev_post_opts = ('-lstdc++','-lm'                     )

class LinkExe(GccRule) :
	pre_opts      = '-pthread'
	rev_post_opts = ('-lstdc++','-lm')

#
# ext libraries
#

pycxx = 'pycxx-7.1.7'
objs = ('cxxsupport','cxx_extensions','cxx_exceptions','cxxextensions','IndirectPythonInterface')
class LinkPycxx(LinkO) :
	targets = { 'TARGET' : 'ext/{Pycxx:pycxx-[0-9.]*}.patched_dir/{Pycxx}/pycxx.o' }
	deps    = { f:f'ext/{{Pycxx}}.patched_dir/{{Pycxx}}/Src/{f}.o' for f in objs   }

#
# application
#

class TarLmake(BaseRule) :
	target = 'lmake.tar.gz'
	deps = {
		'SERIALIZE'          : '_lib/serialize.py'
	,	'READ_MAKEFILES_PY'  : '_lib/read_makefiles.py'
	,	'LD_AUDIT'           : '_lib/ld_audit.so'
	,	'LD_PRELOAD'         : '_lib/ld_preload.so'
	,	'AUTODEP'            : '_bin/autodep'
	,	'JOB_EXEC'           : '_bin/job_exec'
	,	'LDUMP'              : '_bin/ldump'
	,	'LDUMP_JOB'          : '_bin/ldump_job'
	,	'LMAKESERVER'        : '_bin/lmakeserver'
	,	'LIB1'               : 'lib/lmake/__init__.py'
	,	'LIB2'               : 'lib/lmake/auto_sources.py'
	,	'LIB3'               : 'lib/lmake/import_machinery.py'
	,	'LIB4'               : 'lib/lmake/rules.py'
	,	'LIB5'               : 'lib/lmake/sources.py'
	,	'LIB6'               : 'lib/lmake/utils.py'
	,	'LIB7'               : 'lib/lmake_runtime.py'
	,	'CLMAKE'             : 'lib/clmake.so'
	,	'LCHECK_DEPS'        : 'bin/lcheck_deps'
	,	'LDECODE'            : 'bin/ldecode'
	,	'LDEPEND'            : 'bin/ldepend'
	,	'LENCODE'            : 'bin/lencode'
	,	'LTARGET'            : 'bin/ltarget'
	,	'LMARK'              : 'bin/lmark'
	,	'LFORGET'            : 'bin/lforget'
	,	'LMAKE'              : 'bin/lmake'
	,	'LDBG'               : 'bin/ldebug'
	,	'LSHOW'              : 'bin/lshow'
	,	'XXHSUM'             : 'bin/xxhsum'
	,	'DOC'                : 'doc/lmake.html'
	}
	cmd = "tar -cz {' '.join(deps.values())}"

class CpyPy(BaseRule) :
	target = 'lib/{File}.py'
	dep    = '_lib/{File}.py'
	cmd    = 'cat'

class CpyLmakePy(BaseRule) :
	target          = 'lib/{File}.py'
	dep             = '_lib/{File}.src.py'
	def cmd() :
		import shutil
		txt = sys.stdin.read()
		txt = txt.replace('$BASH'           ,Rule.shell[0]                             )
		txt = txt.replace('$GIT'            ,shutil.which('git' )                      )
		txt = txt.replace('$LD_LIBRARY_PATH',Rule.environ_cmd.get('LD_LIBRARY_PATH',''))
		txt = txt.replace('$STD_PATH'       ,Rule.environ_cmd.PATH                     )
		sys.stdout.write(txt)

opt_tab.update({
	r'.*'                 : ( '-I'         , sysconfig.get_path("include")                                  )
,	r'src/.*'             : ( '-iquote'    , f'ext_lnk/{pycxx}.patched_dir/{pycxx}' , '-iquote' , 'ext_lnk' )
,	r'src/autodep/clmake' : (                '-Wno-cast-function-type'              ,                       )
	# On ubuntu, seccomp.h is in /usr/include. On CenOS7, it is in /usr/include/linux, but beware that otherwise, /usr/include must be prefered, hence -idirafter
,	r'src/autodep/ptrace' : ( '-idirafter' , f'/usr/include/linux'                                          )
})

class Link(BaseRule) :
	deps = {
		'DISK'         : 'src/disk.o'
	,	'FD'           : 'src/fd.o'
	,	'HASH'         : 'src/hash.o'
	,	'LIB'          : 'src/lib.o'
	,	'NON_PORTABLE' : 'src/non_portable.o'
	,	'PROCESS'      : 'src/process.o'
	,	'TIME'         : 'src/time.o'
	,	'UTILS'        : 'src/utils.o'
	}
	rev_post_opts = '-ldl'

class LinkLibSo(Link,LinkSo) :
	deps = { 'RPC_JOB' : 'src/rpc_job.o' }

class LinkAppExe(Link,LinkExe) :
	deps = {
		'APP'   : 'src/app.o'
	,	'TRACE' : 'src/trace.o'
	}

class LinkClientAppExe(LinkAppExe) :
	deps = {
		'RPC_CLIENT' : 'src/rpc_client.o'
	,	'CLIENT'     : 'src/client.o'
	}

class LinkAutodepEnv(Link) :
	deps = { 'ENV' : 'src/autodep/env.o' }

class LinkAutodep(LinkAutodepEnv) :
	deps = {
		'GATHER_DEPS' : 'src/autodep/gather_deps.o'
	,	'PTRACE'      : 'src/autodep/ptrace.o'
	,	'RECORD'      : 'src/autodep/record.o'
	,	'RPC_JOB'     : 'src/rpc_job.o'
	,	'RPC_CLIENT'  : None
	}
	# on CentOS7, gcc looks for libseccomp.so with -lseccomp, but only libseccomp.so.2 exists, and this works everywhere.
	if run((gcc,'-shared','-xc','-o','/dev/null','/dev/null','-l:libseccomp.so.2'),stderr=DEVNULL).returncode==0 :
		rev_post_opts = ('-l:libseccomp.so.2',)

class LinkPythonAppExe(LinkAppExe) :
	deps = {
		'PYCXX'  : f'ext/{pycxx}.patched_dir/{pycxx}/pycxx.o'
	,	'PYCXX_' : 'src/pycxx.o'
	}
	rev_post_opts = ( f"-L{sysconfig.get_config_var('LIBDIR')}" , f"-l{sysconfig.get_config_var('LDLIBRARY')[3:-3]}" )

class LinkAutodepLdSo(LinkLibSo,LinkAutodepEnv) :
	targets = { 'TARGET' : '_lib/ld_{Method:audit|preload}.so' }
	deps = {
		'LIB'    : None
	,	'RECORD' : 'src/autodep/record.o'
	,	'LD'     : 'src/autodep/ld_{Method}.o'
	}

class LinkClmakeSo(LinkLibSo,LinkAutodep) :
	targets = { 'TARGET' : 'lib/clmake.so' }
	deps = {
		'RECORD'  : 'src/autodep/record.o'
	,	'SUPPORT' : 'src/autodep/support.o'
	,	'MAIN'    : 'src/autodep/clmake.o'
	}

class LinkAutodepExe(LinkAutodep,LinkAppExe) :
	targets = { 'TARGET' : '_bin/autodep'          }
	deps    = { 'MAIN'   : 'src/autodep/autodep.o' }

class LinkJobExecExe(LinkPythonAppExe,LinkAutodep,LinkAppExe) :
	targets = { 'TARGET' : '_bin/job_exec'  }
	deps    = { 'MAIN'   : 'src/job_exec.o' }

class LinkLmakeserverExe(LinkPythonAppExe,LinkAutodep,LinkAppExe) :
	targets = { 'TARGET' : '_bin/lmakeserver' }
	deps = {
		'RPC_CLIENT' : 'src/rpc_client.o'
	,	'STORE_FILE' : 'src/store/file.o'
	,	'BE'         : 'src/lmakeserver/backend.o'
	,	'BE_LOCAL'   : 'src/lmakeserver/backends/local.o'
	,	'CACHE'      : 'src/lmakeserver/cache.o'
	,	'DIR_CACHE'  : 'src/lmakeserver/caches/dir_cache.o'
	,	'CMD'        : 'src/lmakeserver/cmd.o'
	,	'CODEC'      : 'src/lmakeserver/codec.o'
	,	'GLOBAL'     : 'src/lmakeserver/global.o'
	,	'JOB'        : 'src/lmakeserver/job.o'
	,	'MAKEFILES'  : 'src/lmakeserver/makefiles.o'
	,	'NODE'       : 'src/lmakeserver/node.o'
	,	'REQ'        : 'src/lmakeserver/req.o'
	,	'RULE'       : 'src/lmakeserver/rule.o'
	,	'STORE'      : 'src/lmakeserver/store.o'
	,	'MAIN'       : 'src/lmakeserver/main.o'
	}

class LinkLdumpExe(LinkPythonAppExe,LinkAutodepEnv) :
	targets = { 'TARGET' : '_bin/ldump' }
	deps = {
		'RPC_CLIENT' : 'src/rpc_client.o'
	,	'RPC_JOB'    : 'src/rpc_job.o'
	,	'STORE_FILE' : 'src/store/file.o'
	,	'BE'         : 'src/lmakeserver/backend.o'
	,	'CACHE'      : 'src/lmakeserver/cache.o'
	,	'DIR_CACHE'  : 'src/lmakeserver/caches/dir_cache.o'
	,	'CODEC'      : 'src/lmakeserver/codec.o'
	,	'GLOBAL'     : 'src/lmakeserver/global.o'
	,	'JOB'        : 'src/lmakeserver/job.o'
	,	'NODE'       : 'src/lmakeserver/node.o'
	,	'REQ'        : 'src/lmakeserver/req.o'
	,	'RULE'       : 'src/lmakeserver/rule.o'
	,	'STORE'      : 'src/lmakeserver/store.o'
	,	'MAIN'       : 'src/ldump.o'
	}

class LinkLdumpJobExe(LinkAppExe,LinkAutodepEnv) :
	targets = { 'TARGET' : '_bin/ldump_job' }
	deps = {
		'RPC_JOB' : 'src/rpc_job.o'
	,	'MAIN'    : 'src/ldump_job.o'
	}

for client in ('ldebug','lforget','lmake','lmark','lshow') :
	class LinkLmake(LinkClientAppExe) :
		name    = f'link {client}'
		targets = { 'TARGET' : f'bin/{client}'   }
		deps    = { 'MAIN'   : f'src/{client}.o' }

class LinkXxhsum(LinkAppExe) :
	targets = { 'TARGET' : 'bin/xxhsum'   }
	deps    = { 'MAIN'   : 'src/xxhsum.o' }

class LinkJobSupport(LinkClientAppExe) :
	deps = {
		'SUPPORT' : 'src/autodep/support.o'
	,	'RECORD'  : 'src/autodep/record.o'
	,	'RPC_JOB' : 'src/rpc_job.o'
	}

for remote in ('lcheck_deps','ldecode','ldepend','lencode','ltarget') :
	class LinkRemote(LinkJobSupport,LinkAutodepEnv) :
		name    = f'link {remote}'
		targets = { 'TARGET' : f'bin/{remote}'           }
		deps    = { 'MAIN'   : f'src/autodep/{remote}.o' }
