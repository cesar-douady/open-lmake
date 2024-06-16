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
from subprocess import run,check_output,DEVNULL,STDOUT

gxx          = os.environ.get('CXX','g++')
gxx_is_clang = 'clang' in check_output( (gxx,"--version") , universal_newlines=True )

import lmake
from lmake       import config,pdict
from lmake.rules import Rule,PyRule,AntiRule

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

config.local_admin_dir = lmake.root_dir+'/LMAKE_LOCAL'

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
		'Dir'  : r'(.+?)'  # use non-greedy to be protected against dirs ending in .dir in tar file
	,	'DirS' : r'(.+/|)' # include trailing / or empty to designate top-level
	,	'File' : r'(.+)'
	,	'Base' : r'([^/]+)'
	,	'Ext'  : r'([^/]+)'
	}
	backend     = backend
	resources   = { 'mem' : '100M' }
	n_retries   = 1
	start_delay = 2
	n_tokens    = config.backends.local.cpu

class PathRule(BaseRule) :                       # compiler must be accessed using the PATH as it must find its sub-binaries
	environ_cmd = { 'PATH' : os.getenv('PATH') }
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
		'TAR' : '{Dir}..dir/{File}.tar.gz'
	,	'ZIP' : '{Dir}..dir/{File}.zip'
	}

class Untar(Unpack,PyRule) :
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

class Unzip(Unpack,PyRule) :
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

class ConfigH(BaseRule) :
	targets      = { 'CONFIG_H'   : 'ext/{DirS}config.h'  }
	side_targets = { 'SCRATCHPAD' : 'ext/{DirS}{File*}'   }
	deps         = { 'CONFIGURE'  : 'ext/{DirS}configure' }
	cmd          = 'cd ext/{DirS} ; ./configure'

class SysConfig(PathRule) : # XXX : handle PCRE
	targets = {
		'H'     : 'sys_config.h'
	,	'TRIAL' : 'trial/{*:.*}'
	}
	side_targets = {
		'MK'    : 'sys_config.mk'
	}
	deps = { 'EXE' : '_bin/sys_config' }
	cmd  = '''
		CXX={gxx} PYTHON={sys.executable} ./{EXE} {MK} {H} 2>&1
		echo '#undef  HAS_PCRE'   >> {H}
		echo '#define HAS_PCRE 0' >> {H}
	'''

class VersionH(BaseRule) :
	target = 'version.hh'
	deps = { 'EXE' : '_bin/version' }
	cmd  = "./{EXE} $(grep '\.cc$' Manifest) $(grep '\.hh$' Manifest)"

opt_tab = {}
class GenOpts(BaseRule,PyRule) :
	targets = { 'OPTS' : '{File}.opts' }
	backend = 'local'
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
class Marker(BaseRule,PyRule) :
	prio    = 1                         # avoid untar when in a tar dir
	targets = { 'MRKR' : '{DirS}mrkr' }
	backend = 'local'
	def cmd() :
		open(MRKR,'w')

basic_opts_tab = {
	'c'   : ('-g','-O3','-pedantic','-fno-strict-aliasing','-Werror','-Wall','-Wextra',                                             '-std=c99'  )
,	'cc'  : ('-g','-O3','-pedantic','-fno-strict-aliasing','-Werror','-Wall','-Wextra','-Wno-type-limits','-Wno-cast-function-type','-std=c++20') # on some systems, there is a warning type-limits
,	'cxx' : ('-g','-O3','-pedantic','-fno-strict-aliasing','-Werror','-Wall','-Wextra','-Wno-type-limits','-Wno-cast-function-type','-std=c++20') # .
}
def run_gxx(target,*args) :
		cmd_line = ( gxx , '-o' , target , '-fdiagnostics-color=always' , *args )
		if '/' in gxx : os.environ['PATH'] = ':'.join((osp.dirname(gxx),os.environ['PATH'])) # gxx calls its subprograms (e.g. as) using PATH, ensure it points to gxx dir
		for k,v in os.environ.items() : print(f'{k}={v}')
		print(' '.join(cmd_line))
		run( cmd_line , check=True )
for ext,basic_opts in basic_opts_tab.items() :
	class Compile(PathRule,PyRule) :                                     # note that although class is overwritten at each iteration, each is recorded at definition time by the metaclass
		name    = f'compile {ext}'
		targets = { 'OBJ' : '{File}.o' }
		deps    = {
			'SRC'  : f'{{File}}.{ext}'
		,	'OPTS' : '{File}.opts'
		}
		basic_opts = basic_opts                                          # capture value while iterating (w/o this line, basic_opts would be the final value)
		def cmd() :
			add_flags = eval(open(OPTS).read())
			seen_inc  = False
			missings  = []
			mrkrs     = []
			for x in add_flags :
				if seen_inc and x[0]!='/' :
					if not File.startswith(x+'/') :                      # if x is a dir of File, it necessarily exists
						mrkrs.append(osp.join(x,'mrkr'))                 # gxx does not open includes from non-existent dirs
				seen_inc = x in ('-I','-iquote','-isystem','-idirafter')
			lmake.depend(*mrkrs)
			lmake.check_deps()
			if gxx_is_clang : clang_opts = ('-Wno-misleading-indentation','-Wno-unknown-warning-option','-Wno-c2x-extensions','-Wno-unused-function','-Wno-c++2b-extensions')
			else            : clang_opts = ()
			run_gxx( OBJ
			,	'-c' , '-fPIC' , '-pthread' , f'-frandom-seed={OBJ}' , '-fvisibility=hidden'
			,	*basic_opts
			,	*clang_opts
			,	*add_flags
			,	SRC
			)
		n_tokens  = config.backends.local.cc
		resources = pdict()
		if True             : resources.mem = '1G'
		if backend=='local' : resources.cc  = 1

class LinkRule(PathRule,PyRule) :
	combine       = ('pre_opts','rev_post_opts')
	pre_opts      = []                           # options before inputs & outputs
	rev_post_opts = []                           # options after  inputs & outputs, combine appends at each level, but here we want to prepend
	resources     = {'mem': '1G'}
	def cmd() :
		run_gxx( TARGET
		,	*pre_opts
		,	*deps.values()
		,	*reversed(rev_post_opts)
		)

class LinkO(LinkRule) :
	pre_opts = ('-r','-fPIC')

class LinkSo(LinkRule) :
	pre_opts      = ('-shared-libgcc','-shared','-pthread')
	rev_post_opts = ()

class LinkExe(LinkRule) :
	pre_opts      = '-pthread'
	rev_post_opts = ()

#
# application
#

class TarLmake(BaseRule) :
	target = 'lmake.tar.gz'
	deps = {
		'SERIALIZE'           : '_lib/serialize.py'
	,	'READ_MAKEFILES_PY'   : '_lib/read_makefiles.py'
	,	'LD_AUDIT'            : '_lib/ld_audit.so'
	,	'LD_PRELOAD'          : '_lib/ld_preload.so'
	,	'LD_PRELOAD_JEMALLOC' : '_lib/ld_preload_jemalloc.so'
	,	'AUTODEP'             : '_bin/autodep'
	,	'JOB_EXEC'            : '_bin/job_exec'
	,	'LDUMP'               : '_bin/ldump'
	,	'LDUMP_JOB'           : '_bin/ldump_job'
	,	'LMAKESERVER'         : '_bin/lmakeserver'
	,	'LIB1'                : 'lib/lmake/__init__.py'
	,	'LIB2'                : 'lib/lmake/auto_sources.py'
	,	'LIB3'                : 'lib/lmake/import_machinery.py'
	,	'LIB4'                : 'lib/lmake/rules.py'
	,	'LIB5'                : 'lib/lmake/sources.py'
	,	'LIB6'                : 'lib/lmake/utils.py'
	,	'LIB7'                : 'lib/lmake_runtime.py'
	,	'CLMAKE'              : 'lib/clmake.so'
	,	'ALIGN_COMMENTS'      : 'bin/align_comments'
	,	'LCHECK_DEPS'         : 'bin/lcheck_deps'
	,	'LDBG'                : 'bin/ldebug'
	,	'LDECODE'             : 'bin/ldecode'
	,	'LDEPEND'             : 'bin/ldepend'
	,	'LENCODE'             : 'bin/lencode'
	,	'LFORGET'             : 'bin/lforget'
	,	'LMAKE'               : 'bin/lmake'
	,	'LMARK'               : 'bin/lmark'
	,	'LREPAIR'             : 'bin/lrepair'
	,	'LSHOW'               : 'bin/lshow'
	,	'LTARGET'             : 'bin/ltarget'
	,	'XXHSUM'              : 'bin/xxhsum'
	,	'DOC'                 : 'doc/lmake.html'
	}
	cmd = "tar -cz {' '.join(deps.values())}"

class CpyPy(BaseRule) :
	target = 'lib/{File}.py'
	dep    = '_lib/{File}.py'
	cmd    = 'cat'

class CpyLmakePy(BaseRule,PyRule) :
	target = 'lib/{File}.py'
	dep    = '_lib/{File}.src.py'
	def cmd() :
		import shutil
		txt = sys.stdin.read()
		txt = txt.replace('$BASH'           ,Rule.shell[0]                             )
		txt = txt.replace('$GIT'            ,shutil.which('git' )                      )
		txt = txt.replace('$LD_LIBRARY_PATH',Rule.environ_cmd.get('LD_LIBRARY_PATH',''))
		txt = txt.replace('$STD_PATH'       ,Rule.environ_cmd.PATH                     )
		sys.stdout.write(txt)

opt_tab.update({
	r'.*'                 : ( '-I'         , sysconfig.get_path("include")   )
,	r'src/.*'             : ( '-iquote'    , 'ext_lnk'                       )
,	r'src/autodep/clmake' : (                '-Wno-cast-function-type'     , )
	# On ubuntu, seccomp.h is in /usr/include. On CenOS7, it is in /usr/include/linux, but beware that otherwise, /usr/include must be prefered, hence -idirafter
,	r'src/autodep/ptrace' : ( '-idirafter' , f'/usr/include/linux'           )
,	r'src/fuse'           : ( '-idirafter' , f'/usr/include/fuse3'           )
,	r'src/rpc_job'        : ( '-idirafter' , f'/usr/include/fuse3'           )
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
	deps = { 'RPC_JOB_EXEC' : 'src/rpc_job_exec.o' }

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
	deps = {
		'ENV' : 'src/autodep/env.o'
	}

class LinkAutodep(LinkAutodepEnv) :
	deps = {
		'BACKDOOR'     : 'src/autodep/backdoor.o'
	,	'GATHER'       : 'src/autodep/gather.o'
	,	'PTRACE'       : 'src/autodep/ptrace.o'
	,	'RECORD'       : 'src/autodep/record.o'
	,	'SYSCALL'      : 'src/autodep/syscall_tab.o'
	,	'RPC_JOB'      : 'src/rpc_job.o'
	,	'RPC_JOB_EXEC' : 'src/rpc_job_exec.o'
	,	'FUSE'         : 'src/fuse.o'
	,	'RPC_CLIENT'   : None
	}
	# on CentOS7, gcc looks for libseccomp.so with -lseccomp, but only libseccomp.so.2 exists, and this works everywhere.
	rev_post_opts = ('-lfuse3',)
	if run((gxx,'-shared','-xc','-o','/dev/null','/dev/null','-l:libseccomp.so.2'),stderr=DEVNULL).returncode==0 :
		rev_post_opts += ('-l:libseccomp.so.2',)

class LinkPythonAppExe(LinkAppExe) :
	deps = {
		'PY' : 'src/py.o'
	}
	rev_post_opts = ( f"-L{sysconfig.get_config_var('LIBDIR')}" , f"-l{sysconfig.get_config_var('LDLIBRARY')[3:-3]}" )

class LinkAutodepLdSo(LinkLibSo,LinkAutodepEnv) :
	targets = { 'TARGET' : '_lib/ld_{Method:audit|preload|preload_jemalloc}.so' }
	deps = {
		'LIB' : None
	,	'LD'  : 'src/autodep/ld_{Method}.o'
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
	,	'RPC_JOB'    : 'src/rpc_job.o'
	,	'FUSE'       : 'src/fuse.o'
	,	'LD'         : 'src/autodep/ld_server.o'
	,	'STORE_FILE' : 'src/store/file.o'
	,	'BE'         : 'src/lmakeserver/backend.o'
	,	'BE_LOCAL'   : 'src/lmakeserver/backends/local.o'
	#,	'BE_SLURM'   : 'src/lmakeserver/backends/slurm.o' # XXX : add conditional compilation to compile slurm when it is available
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
	rev_post_opts = ('-lfuse3',)

class LinkLrepairExe(LinkLmakeserverExe) :
	targets = { 'TARGET' : 'bin/lrepair' }
	deps = {
		'MAIN' : 'src/lrepair.o' # lrepair is a server with another main
	}

class LinkLdumpExe(LinkPythonAppExe,LinkAutodep) :
	targets = { 'TARGET' : '_bin/ldump' }
	deps = {
		'RPC_CLIENT' : 'src/rpc_client.o'
	,	'RPC_JOB'    : 'src/rpc_job.o'
	,	'FUSE'       : 'src/fuse.o'
	,	'LD'         : 'src/autodep/ld_server.o'
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
	rev_post_opts = ('-lfuse3',)

class LinkLdumpJobExe(LinkAppExe,LinkAutodepEnv) :
	targets = { 'TARGET' : '_bin/ldump_job' }
	deps = {
		'RPC_JOB' : 'src/rpc_job.o'
	,	'FUSE'    : 'src/fuse.o'
	,	'MAIN'    : 'src/ldump_job.o'
	}
	rev_post_opts = ('-lfuse3',)

for client in ('ldebug','lforget','lmake','lmark','lshow') :
	class LinkLmake(LinkClientAppExe) :
		name    = f'link {client}'
		targets = { 'TARGET' : f'bin/{client}'   }
		deps    = { 'MAIN'   : f'src/{client}.o' }

for app in ('xxhsum','align_comments') :
	class LinkXxhsum(LinkAppExe) :
		name    = f'link {app}'
		targets = { 'TARGET' : f'bin/{app}'   }
		deps    = { 'MAIN'   : f'src/{app}.o' }

class LinkJobSupport(LinkAutodepEnv) :
	deps = {
		'BACKDOOR'     : 'src/autodep/backdoor.o'
	,	'JOB_SUPPORT'  : 'src/autodep/job_support.o'
	,	'RECORD'       : 'src/autodep/record.o'
	,	'RPC_JOB_EXEC' : 'src/rpc_job_exec.o'
	}

class LinkClmakeSo(LinkLibSo,LinkJobSupport) :
	targets = { 'TARGET' : 'lib/clmake.so'        }
	deps    = { 'MAIN'   : 'src/autodep/clmake.o' }

for remote in ('lcheck_deps','ldecode','ldepend','lencode','ltarget') :
	class LinkRemote(LinkAppExe,LinkJobSupport) :
		name    = f'link {remote}'
		targets = { 'TARGET' : f'bin/{remote}'           }
		deps    = { 'MAIN'   : f'src/autodep/{remote}.o' }
