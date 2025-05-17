# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import os
import os.path as osp
import re
import socket
import sys
import sysconfig
import tarfile
import zipfile

import lmake

version = lmake.user_environ.get('VERSION','??.??')
gxx     = lmake.user_environ.get('CXX'    ,'g++'  )

from lmake       import config , pdict , repo_root , run_cc
from lmake.rules import Rule , PyRule , AntiRule , TraceRule , DirRule

if 'slurm' in lmake.backends :
	backend = 'slurm'
	config.backends.slurm = {
		'interface'         : socket.getfqdn()
	,	'use_nice'          : True
	,	'n_max_queued_jobs' : 10
	}
else :
	backend = 'local'

config.caches.dir = {
	'tag'  : 'dir'
,	'repo' : repo_root
,	'dir'  : osp.dirname(repo_root)+'/lmake_env-cache'
}

config.console.date_precision = 2
config.console.show_eta       = True

config.local_admin_dir = repo_root+'/LMAKE_LOCAL'

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
	backend   = backend
	tmp_view  = '/tmp'
	resources = {
		'mem' : '100M'
	,	'tmp' : '1G'
	}
	max_retries_on_lost = 1
	start_delay         = 2

class PathRule(BaseRule) :                   # compiler must be accessed using the PATH as it must find its sub-binaries
	environ = { 'PATH' : os.getenv('PATH') }
	cache   = 'dir'

class HtmlInfo(BaseRule) :
	target = '{DirS}info.texi'
	cmd = '''
		echo "@set VERSION       {version}"
		echo "@set UPDATED       $(env -i date '+%d %B %Y')"
		echo "@set UPDATED-MONTH $(env -i date '+%B %Y'   )"
	'''
class Html(BaseRule,TraceRule) :
	targets = { 'HTML' : '{DirS}{Base}.html' }
	deps    = { 'TEXI' : '{DirS}{Base}.texi' }
	environ = {
		'LANGUAGE' : ''
	,	'LC_ALL'   : ''
	,	'LANG'     : ''
	}
	cmd = '''
		cd {DirS}.                                            # manage case where DirS is empty
		texi2any --html --no-split -o {Base}.html {Base}.texi
	'''

class Unpack(BaseRule) :
	targets = {
		'FILE'     : 'ext/{Dir}.dir/{File*}'
	,	'MANIFEST' : 'ext/{Dir}.manifest'
	}

# it is unacceptable to have a pack inside a pack, as this creates ambiguities
class AntiPackPack(BaseRule,AntiRule) :
	targets = {
		'TAR' : '{Dir}.dir/{File}.tar.gz'
	,	'ZIP' : '{Dir}.dir/{File}.zip'
	}

class Untar(Unpack,PyRule) :
	deps      = { 'TAR' : 'ext/{Dir}.tar.gz' }
	stderr_ok = True
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
	deps      = { 'ZIP' : 'ext/{Dir}.zip' }
	stderr_ok = True
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

class SysConfig(PathRule,TraceRule) : # XXX : handle PCRE
	targets = {
		'H'     : 'sys_config.h'
	,	'TRIAL' : r'trial/{*:.*}'
	,	'MK'    : r'sys_config.dir/{*:.*}'
	}
	deps = { 'EXE' : '_bin/sys_config' }
	cmd  = '''
		OS={os.uname().sysname} CXX={gxx} PYTHON={sys.executable} ./{EXE} $TMPDIR/mk {H} 2>&1
		while read k e v ; do
			case $k in
				'#'*      ) ;;
				*HAS_PCRE*) echo    > {MK('$k')} ;;
				*         ) echo $v > {MK('$k')} ;;
			esac
		done < $TMPDIR/mk
		echo '#undef  HAS_PCRE'   >> {H}
		echo '#define HAS_PCRE 0' >> {H}
		#echo > {MK('HAS_PCRE')}
	'''
def sys_config(key) :
	try    : return open(f'sys_config.dir/{key}').read().strip()
	except : return ''

class VersionH(BaseRule) :
	target = 'version.hh'
	deps = { 'EXE' : '_bin/version' }
	cmd  = r"./{EXE} $(grep '\.cc$' Manifest) $(grep '\.hh$' Manifest)"

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

# ensure dir exists by depending on dir/...
class Marker(DirRule) :
	prio = 1            # avoid untar when in a tar dir

basic_opts = ('-O0','-pedantic','-fno-strict-aliasing','-DNDEBUG','-DNO_TRACE','-Werror','-Wall','-Wextra') # minimize compilation time
basic_opts_tab = {
	'c'   :   basic_opts
,	'cc'  : (*basic_opts,'-Wno-type-limits','-Wno-cast-function-type')                                      # on some systems, there is a warning type-limits
,	'cxx' : (*basic_opts,'-Wno-type-limits','-Wno-cast-function-type')                                      # .
}
def run_gxx(target,*args) :
		cmd_line = ( gxx , '-o' , target , '-fdiagnostics-color=always' , *args )
		if '/' in gxx : os.environ['PATH'] = ':'.join((osp.dirname(gxx),os.environ['PATH']))                # gxx calls its subprograms (e.g. as) using PATH, ensure it points to gxx dir
		for k,v in os.environ.items() : print(f'{k}={v}')
		print(' '.join(cmd_line))
		run_cc(*cmd_line)
for ext,basic_opts in basic_opts_tab.items() :
	class Compile(PathRule,PyRule) :           # note that although class is overwritten at each iteration, each is recorded at definition time by the metaclass
		name    = f'compile {ext}'
		targets = { 'OBJ' : '{File}.o' }
		deps    = {
			'SRC'  : f'{{File}}.{ext}'
		,	'OPTS' : '{File}.opts'
		}
		basic_opts = basic_opts                # capture value while iterating (w/o this line, basic_opts would be the final value)
		def cmd() :
			add_flags = eval(open(OPTS).read())
			if sys_config('CXX_FLAVOR')=='clang' : clang_opts = ('-Wno-misleading-indentation','-Wno-unknown-warning-option','-Wno-c2x-extensions','-Wno-unused-function','-Wno-c++2b-extensions')
			else                                 : clang_opts = ()
			if ext=='c'                          : std        = 'c99'
			else                                 : std        = sys_config('CXX_STD')
			run_gxx( OBJ
			,	f'-std={std}'
			,	'-c' , '-fPIC' , '-pthread' , f'-frandom-seed={OBJ}' , '-fvisibility=hidden'
			,	*basic_opts
			,	*clang_opts
			,	*add_flags
			,	SRC
			)
		resources = {
			'mem' : '2G'
		,	'cc'  : 1
		}

class LinkRule(PathRule,PyRule) :
	combine       = ('opts',)
	opts          = []                                   # options before inputs & outputs
	resources     = {'mem':'1G'}
	need_python   = False
	need_seccomp  = False
	need_compress = False
	def cmd() :
		ns  = need_seccomp and sys_config('HAS_SECCOMP')
		lst = sys_config('LIB_STACKTRACE')
		lnz = need_compress
		if lnz :
			if   sys_config('HAS_ZSTD') : lnz = '-lzstd'
			elif sys_config('HAS_ZLIB') : lnz = '-lz'
			else                        : lnz = ''
		if True : post_opts = ['-ldl']
		if lst  : post_opts.append(f'-l{lst}')
		if ns   : post_opts.append('-l:libseccomp.so.2') # on CentOS7, gcc looks for libseccomp.so with -lseccomp, but only libseccomp.so.2 exists
		if lnz  : post_opts.append(lnz                 )
		if need_python :
			post_opts.append(f"-L{sysconfig.get_config_var('LIBDIR')}")
			lib = sysconfig.get_config_var('LDLIBRARY')
			assert lib.startswith('lib')
			lib = lib[3:].rsplit('.',1)[0]
			post_opts.append(f"-l{lib}")
		run_gxx( TARGET
		,	*opts
		,	*deps.values()
		,	*post_opts
		)

class LinkO(LinkRule) :
	opts = ('-r','-fPIC')

class LinkSo(LinkRule) :
	opts = ('-shared-libgcc','-shared','-pthread')

class LinkExe(LinkRule) :
	opts = '-pthread'

#
# application
#

class TarLmake(BaseRule) :
	targets = { 'TAR' : 'lmake.tar.gz' }
	deps = {
		'SERIALIZE'           : '_lib/serialize.py'
	,	'READ_MAKEFILES_PY'   : '_lib/read_makefiles.py'
	,	'LD_PRELOAD'          : '_lib/ld_preload.so'
	,	'LD_PRELOAD_JEMALLOC' : '_lib/ld_preload_jemalloc.so'
	,	'AUTODEP'             : '_bin/lautodep'
	,	'JOB_EXEC'            : '_bin/job_exec'
	,	'LDUMP'               : '_bin/ldump'
	,	'LDUMP_JOB'           : '_bin/ldump_job'
	,	'LMAKESERVER'         : '_bin/lmakeserver'
	,	'LIB_UTILS'           : 'lib/lmake/utils.py'
	,	'LIB_INIT'            : 'lib/lmake/__init__.py'
	,	'LIB1'                : 'lib/lmake/auto_sources.py'
	,	'LIB2'                : 'lib/lmake/import_machinery.py'
	,	'LIB3'                : 'lib/lmake/rules.py'
	,	'LIB4'                : 'lib/lmake/sources.py'
	,	'LIB_DBG_UTILS'       : 'lib/lmake_debug/utils.py'
	,	'LIB_DBG1'            : 'lib/lmake_debug/default.py'
	,	'LIB_DBG2'            : 'lib/lmake_debug/enter.py'
	,	'LIB_DBG3'            : 'lib/lmake_debug/gdb.py'
	,	'LIB_DBG4'            : 'lib/lmake_debug/none.py'
	,	'LIB_DBG5'            : 'lib/lmake_debug/pudb.py'
	,	'LIB_DBG6'            : 'lib/lmake_debug/vscode.py'
	,	'LIB_DBG_RT_UTILS'    : 'lib/lmake_debug/runtime/utils.py'
	,	'LIB_DBG_RT1'         : 'lib/lmake_debug/runtime/pdb_.py'
	,	'LIB_DBG_RT2'         : 'lib/lmake_debug/runtime/pudb_.py'
	,	'LIB_DBG_RT3'         : 'lib/lmake_debug/runtime/vscode.py'
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
	}
	def gen_files() :
		files = list(deps.values())
		if sys_config('HAS_LD_AUDIT') : files.append('_lib/ld_audit.so')
		return files
	cmd = '''
		for f in       {' '.join(gen_files())} ; do echo $f ; done
		tar -czf {TAR} {' '.join(gen_files())}
	'''

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
		txt = txt.replace('$BASH'           ,Rule.shell[0]                         )
		txt = txt.replace('$GIT'            ,shutil.which('git' ) or ''            )
		txt = txt.replace('$LD_LIBRARY_PATH',Rule.environ.get('LD_LIBRARY_PATH',''))
		txt = txt.replace('$STD_PATH'       ,Rule.environ.PATH                     )
		sys.stdout.write(txt)

opt_tab.update({
	r'.*'                 : ( '-I'         , sysconfig.get_path("include")  )
,	r'src/.*'             : ( '-iquote'    , 'ext_lnk'                      )
,	r'src/autodep/clmake' : (                '-Wno-cast-function-type'     ,)
,	r'src/autodep/ptrace' : ( '-idirafter' , f'/usr/include/linux'          ) # On ubuntu, seccomp.h is in /usr/include. On CenOS7, it is in /usr/include/linux, ...
})

class Link(BaseRule) :
	deps = {
		'DISK'         : 'src/disk.o'
	,	'FD'           : 'src/fd.o'
	,	'HASH'         : 'src/hash.o'
	,	'LIB'          : 'src/lib.o'
	,	'NON_PORTABLE' : 'src/non_portable.o'
	,	'PROCESS'      : 'src/process.o'
	,	'RE'           : 'src/re.o'
	,	'TIME'         : 'src/time.o'
	,	'UTILS'        : 'src/utils.o'
	}

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

class LinkPython(Link) :
	deps = {
		'PY' : 'src/py.o'
	}
	need_python = True

class LinkAutodep(LinkAutodepEnv) :
	virtual = True
	deps = {
		'BACKDOOR'     : 'src/autodep/backdoor.o'
	,	'GATHER'       : 'src/autodep/gather.o'
	,	'PTRACE'       : 'src/autodep/ptrace.o'
	,	'RECORD'       : 'src/autodep/record.o'
	,	'SYSCALL'      : 'src/autodep/syscall_tab.o'
	,	'RPC_JOB'      : 'src/rpc_job.o'
	,	'DIR_CACHE'    : 'src/caches/dir_cache.o'
	,	'RPC_JOB_EXEC' : 'src/rpc_job_exec.o'
	,	'RPC_CLIENT'   : None
	}
	need_seccomp  = True
	need_compress = True

class LinkAutodepLdSo(LinkLibSo,LinkAutodepEnv) :
	targets = { 'TARGET' : '_lib/ld_{Method:audit|preload|preload_jemalloc}.so' }
	deps = {
		'LIB' : None
	,	'LD'  : 'src/autodep/ld_{Method}.o'
	}

class LinkAutodepExe(LinkPython,LinkAutodep,LinkAppExe) :
	targets = { 'TARGET' : '_bin/lautodep'          }
	deps    = { 'MAIN'   : 'src/autodep/lautodep.o' }

class LinkJobExecExe(LinkPython,LinkAutodep,LinkAppExe) :
	targets = { 'TARGET' : '_bin/job_exec'  }
	deps    = { 'MAIN'   : 'src/job_exec.o' }

class LinkLmakeserverExe(LinkPython,LinkAutodep,LinkAppExe) :
	targets = { 'TARGET' : '_bin/lmakeserver' }
	deps = {
		'RPC_CLIENT' : 'src/rpc_client.o'
	,	'RPC_JOB'    : 'src/rpc_job.o'
	,	'DIR_CACHE'  : 'src/caches/dir_cache.o'
	,	'LD'         : 'src/autodep/ld_server.o'
	,	'BE'         : 'src/lmakeserver/backend.o'
	,	'BE_LOCAL'   : 'src/lmakeserver/backends/local.o'
	#,	'BE_SLURM'   : 'src/lmakeserver/backends/slurm.o' # XXX : add slurm compilation
	#,	'BE_SGE'     : 'src/lmakeserver/backends/sge.o'   # XXX : add sge compilation
	,	'CMD'        : 'src/lmakeserver/cmd.o'
	,	'CODEC'      : 'src/lmakeserver/codec.o'
	,	'GLOBAL'     : 'src/lmakeserver/global.o'
	,	'CONFIG'     : 'src/lmakeserver/config.o'
	,	'JOB'        : 'src/lmakeserver/job.o'
	,	'MAKEFILES'  : 'src/lmakeserver/makefiles.o'
	,	'NODE'       : 'src/lmakeserver/node.o'
	,	'REQ'        : 'src/lmakeserver/req.o'
	,	'RULE'       : 'src/lmakeserver/rule.o'
	,	'STORE'      : 'src/lmakeserver/store.o'
	,	'MAIN'       : 'src/lmakeserver/main.o'
	}
	need_compress = True

class LinkLrepairExe(LinkLmakeserverExe) :
	targets = { 'TARGET' : 'bin/lrepair' }
	deps = {
		'MAIN' : 'src/lrepair.o' # lrepair is a server with another main
	}

class LinkLdumpExe(LinkPython,LinkAutodep,LinkAppExe) :
	targets = { 'TARGET' : '_bin/ldump' }
	deps = {
		'RPC_CLIENT' : 'src/rpc_client.o'
	,	'RPC_JOB'    : 'src/rpc_job.o'
	,	'DIR_CACHE'  : 'src/caches/dir_cache.o'
	,	'LD'         : 'src/autodep/ld_server.o'
	,	'BE'         : 'src/lmakeserver/backend.o'
	,	'CODEC'      : 'src/lmakeserver/codec.o'
	,	'GLOBAL'     : 'src/lmakeserver/global.o'
	,	'CONFIG'     : 'src/lmakeserver/config.o'
	,	'JOB'        : 'src/lmakeserver/job.o'
	,	'NODE'       : 'src/lmakeserver/node.o'
	,	'REQ'        : 'src/lmakeserver/req.o'
	,	'RULE'       : 'src/lmakeserver/rule.o'
	,	'STORE'      : 'src/lmakeserver/store.o'
	,	'MAIN'       : 'src/ldump.o'
	}
	need_compress = True

class LinkLdumpJobExe(LinkAppExe,LinkAutodepEnv) :
	targets = { 'TARGET' : '_bin/ldump_job' }
	deps = {
		'RPC_JOB'   : 'src/rpc_job.o'
	,	'DIR_CACHE' : 'src/caches/dir_cache.o'
	,	'MAIN'      : 'src/ldump_job.o'
	}
	need_compress = True

for client in ('lforget','lmake','lmark','lshow') :
	class LinkLmake(LinkClientAppExe) :
		name    = f'link {client}'
		targets = { 'TARGET' : f'bin/{client}'   }
		deps    = { 'MAIN'   : f'src/{client}.o' }
		if client=='ldebug' : deps['PY'] = 'src/py.o'

class LinkLdebug(LinkClientAppExe,LinkPython) :
	targets = { 'TARGET' : f'bin/ldebug'   }
	deps    = { 'MAIN'   : f'src/ldebug.o' }

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
