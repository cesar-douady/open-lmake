# This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
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

import lmake
from lmake import AntiRule,Rule,config

lmake.version = (1,0)

lmake.local_admin_dir  = 'LMAKE_LOCAL'
lmake.remote_admin_dir = 'LMAKE_REMOTE'

config.link_support = 'full'

config.backends.local.cpu = int(os.cpu_count()*1.5)
config.backends.local.gcc = os.cpu_count()

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
	resources   = { 'mem' : 100 }      # in MB
	start_delay = 2
	n_tokens    = config.backends.local.cpu

class Centos7Rule(BaseRule) :
	environ = { 'PATH' : '/opt/rh/devtoolset-11/root/usr/bin:'+BaseRule.environ.PATH }

class Html(BaseRule) :
	targets = { 'HTML' : '{File}.html' }
	deps    = { 'TEXI' : '{File}.texi' }
	cmd     = 'texi2any --html --no-split -o $HTML $TEXI'

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
					if ('/'+member.name).endswith('/configure') :
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
	cmd  = 'cd ext/$DirS ; ./configure'

class SysConfigH(Centos7Rule) :
    targets = {
		'H'     : 'sys_config.h'
	,	'TRIAL' : 'trial/{*:.*}'
	}
    deps = { 'EXE' : 'sys_config' }
    cmd  = f'CC=gcc PYTHON={sys.executable} ./$EXE 2>&1  >$H'

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
	targets = { 'MRKR' : '{DirS}mrkr' }
	def cmd() :
		open(MRKR,'w')

basic_opts_tab = {
	'c'   : ('-g','-O3','-Wall','-Wextra','-pedantic','-fno-strict-aliasing','-std=c99'  )
,	'cc'  : ('-g','-O3','-Wall','-Wextra','-pedantic','-fno-strict-aliasing','-std=c++20')
,	'cxx' : ('-g','-O3','-Wall','-Wextra','-pedantic','-fno-strict-aliasing','-std=c++20')
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
				'gcc' , '-fdiagnostics-color=always' , '-c' , '-fPIC' , '-pthread' , f'-frandom-seed={OBJ}' , '-fvisibility=hidden'
			,	f'-DHAS_PTRACE={int(lmake.has_ptrace)}' ,  f'-DHAS_LD_AUDIT={int(lmake.has_ld_audit)}'
			,	*basic_opts
			,	*add_flags
			,	'-o',OBJ , SRC
			)
			for k,v in os.environ.items() : print(f'{k}={v}')
			print(' '.join(cmd_line))
			run( cmd_line , check=True )
		n_tokens = config.backends.local.gcc
		resources = {
			'mem' : 500                                                        # in MB
		,	'gcc' : 1
		}

class GccRule(Centos7Rule) :
	combine       = ('pre_opts','rev_post_opts')
	pre_opts      = []                                                         # options before inputs & outputs
	rev_post_opts = []                                                         # options after  inputs & outputs, combine appends at each level, but here we want to prepend
	def cmd() :
		cmd_line = (
			'gcc' , '-fdiagnostics-color=always'
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

libseccomp = 'libseccomp-2.1.0'
class InstallSeccomp(BaseRule) :
	stems       = { 'Version' : r'[0-9]+(\.[0-9]+)*' }
	install_dir = 'ext/libseccomp-{Version}.dir/libseccomp-{Version}'
	targets = {
		'SO'         : ( f'{install_dir}/src/libseccomp.so.{{Version}}' )      # gcc reads output to check if it is a link before compilation
	,	'A'          : ( f'{install_dir}/src/libseccomp.a'              )
	,	'INCLUDE'    : ( f'{install_dir}/include/seccomp.h'             )
	,	'I386'       : ( f'{install_dir}/{{*:.*-i386.*}}.o'             , '-Match' , 'Incremental' , '-Write' ) # i386 info is delivered as binary
	,	'MAKE_DEPS'  : ( f'{install_dir}/{{File*}}.d'                   , '-Match' , 'Incremental' , '-Dep'   )
	,	'OBJ'        : ( f'{install_dir}/{{File*}}.o'                   , '-Match' , 'Incremental' , '-Dep'   )
	,	'TOOLS'      : ( f'{install_dir}/tools/{{File*}}'               , '-Match' , 'Incremental' , '-Dep'   )
	,	'SCRATCHPAD' : ( f'{install_dir}/{{File*}}'                     , '-Match' , 'Incremental'            )
	}
	deps         = { 'CONFIGURE' : f'{install_dir}/configure' }
	allow_stderr = True
	autodep      = 'ld_preload'
	cmd          = ' cd $(dirname $CONFIGURE) ; ./configure ; make '

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
	,	'AUTODEP_LD_AUDIT'   : '_lib/autodep_ld_audit.so'
	,	'AUTODEP_LD_PRELOAD' : '_lib/autodep_ld_preload.so'
	,	'AUTODEP'            : '_bin/autodep'
	,	'JOB_EXEC'           : '_bin/job_exec'
	,	'LDUMP'              : '_bin/ldump'
	,	'LDUMP_JOB'          : '_bin/ldump_job'
	,	'LMAKESERVER'        : '_bin/lmakeserver'
	,	'LMAKE_PY'           : 'lib/lmake.py'
	,	'CLMAKE'             : 'lib/clmake.so'
	,	'LCRITICAL_BARRIER'  : 'bin/lcritical_barrier'
	,	'LCHECK_DEPS'        : 'bin/lcheck_deps'
	,	'LDEP_CRCS'          : 'bin/ldep_crcs'
	,	'LDEPEND'            : 'bin/ldepend'
	,	'LUNLINK'            : 'bin/lunlink'
	,	'LTARGET'            : 'bin/ltarget'
	,	'LFREEZE'            : 'bin/lfreeze'
	,	'LFORGET'            : 'bin/lforget'
	,	'LMAKE'              : 'bin/lmake'
	,	'LSHOW'              : 'bin/lshow'
	,	'XXHSUM'             : 'bin/xxhsum'
	,	'DOC'                : 'doc/lmake.html'
	}
	cmd = f"tar -cz {' '.join(deps.values())}"

opt_tab.update({
	r'.*'                        : ( '-I'      , sysconfig.get_path("include")                                            )
,	r'src/.*'                    : ( '-iquote' , f'ext_lnk/{pycxx}.patched_dir/{pycxx}'           , '-iquote' , 'ext_lnk' )
,	r'src/autodep/clmake'        : (             '-Wno-cast-function-type'                        ,                       )
,	r'src/autodep/ptrace'        : ( '-iquote' , f'ext_lnk/{libseccomp}.dir/{libseccomp}/include'                         )
,	r'src/lmakeserver/makefiles' : ( '-D'      , f'PYTHON="{sys.executable}"'                                             )
})

class Link(BaseRule) :
	deps = {
		'DISK'         : 'src/disk.o'
	,	'HASH'         : 'src/hash.o'
	,	'LIB'          : 'src/lib.o'
	,	'NON_PORTABLE' : 'src/non_portable.o'
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

class LinkAutodepAppExe(LinkAppExe) :
	deps = {
		'GATHER_DEPS' : 'src/autodep/gather_deps.o'
	,	'PTRACE'      : 'src/autodep/ptrace.o'
	,	'RECORD'      : 'src/autodep/record.o'
	,	'RPC_JOB'     : 'src/rpc_job.o'
	,	'RPC_CLIENT'  : None
	}
	rev_post_opts = ( f'-Lext/{libseccomp}.dir/{libseccomp}/src' , '-lseccomp' )

class LinkPythonAppExe(LinkAppExe) :
	deps = {
		'PYCXX'  : f'ext/{pycxx}.patched_dir/{pycxx}/pycxx.o'
	,	'PYCXX_' : 'src/pycxx.o'
	}
	rev_post_opts = ( f"-L{sysconfig.get_config_var('LIBDIR')}" , f"-l{sysconfig.get_config_var('LDLIBRARY')[3:-3]}" )

class LinkAutodepLdSo(LinkLibSo) :
	targets = { 'TARGET' : '_lib/autodep_ld_{Method:audit|preload}.so' }
	deps = {
		'LIB'    : None
	,	'RECORD' : 'src/autodep/record.o'
	,	'LD'     : 'src/autodep/autodep_ld_{Method}.o'
	}

class LinkClmakeSo(LinkLibSo) :
	targets = { 'TARGET' : 'lib/clmake.so' }
	deps = {
		'RECORD'  : 'src/autodep/record.o'
	,	'SUPPORT' : 'src/autodep/autodep_support.o'
	,	'MAIN'    : 'src/autodep/clmake.o'
	}

class LinkAutodepExe(LinkAutodepAppExe) :
	targets = { 'TARGET' : '_bin/autodep'          }
	deps    = { 'MAIN'   : 'src/autodep/autodep.o' }

class LinkJobExecExe(LinkPythonAppExe,LinkAutodepAppExe) :
	targets = { 'TARGET' : '_bin/job_exec'  }
	deps    = { 'MAIN'   : 'src/job_exec.o' }

class LinkLmakeserverExe(LinkPythonAppExe,LinkAutodepAppExe) :
	targets = { 'TARGET' : '_bin/lmakeserver' }
	deps = {
		'RPC_CLIENT' : 'src/rpc_client.o'
	,	'STORE_FILE' : 'src/store/file.o'
	,	'BE'         : 'src/lmakeserver/backend.o'
	,	'BE_LOCAL'   : 'src/lmakeserver/backends/local.o'
	,	'CMD'        : 'src/lmakeserver/cmd.o'
	,	'GLOBAL'     : 'src/lmakeserver/global.o'
	,	'JOB'        : 'src/lmakeserver/job.o'
	,	'MAKEFILES'  : 'src/lmakeserver/makefiles.o'
	,	'NODE'       : 'src/lmakeserver/node.o'
	,	'REQ'        : 'src/lmakeserver/req.o'
	,	'RULE'       : 'src/lmakeserver/rule.o'
	,	'STORE'      : 'src/lmakeserver/store.o'
	,	'MAIN'       : 'src/lmakeserver/main.o'
	}

class LinkLdumpExe(LinkPythonAppExe) :
	targets = { 'TARGET' : '_bin/ldump' }
	deps = {
		'RPC_CLIENT' : 'src/rpc_client.o'
	,	'RPC_JOB'    : 'src/rpc_job.o'
	,	'STORE_FILE' : 'src/store/file.o'
	,	'BE'         : 'src/lmakeserver/backend.o'
	,	'GLOBAL'     : 'src/lmakeserver/global.o'
	,	'JOB'        : 'src/lmakeserver/job.o'
	,	'NODE'       : 'src/lmakeserver/node.o'
	,	'REQ'        : 'src/lmakeserver/req.o'
	,	'RULE'       : 'src/lmakeserver/rule.o'
	,	'STORE'      : 'src/lmakeserver/store.o'
	,	'MAIN'       : 'src/ldump.o'
	}

class LinkLdumpJobExe(LinkAppExe) :
	targets = { 'TARGET' : '_bin/ldump_job' }
	deps = {
		'RPC_JOB' : 'src/rpc_job.o'
	,	'MAIN'    : 'src/ldump_job.o'
	}

class LinkLmake(LinkClientAppExe) :
	targets = { 'TARGET' : 'bin/lmake'   }
	deps    = { 'MAIN'   : 'src/lmake.o' }

class LinkLshow(LinkClientAppExe) :
	targets = { 'TARGET'  : 'bin/lshow'   }
	deps    = { 'MAIN' : 'src/lshow.o' }

class LinkLforget(LinkClientAppExe) :
	targets = { 'TARGET' : 'bin/lforget'   }
	deps    = { 'MAIN'   : 'src/lforget.o' }

class LinkLcriticalBarrier(LinkClientAppExe) :
	targets = { 'TARGET' : 'bin/lcritical_barrier'   }
	deps    = { 'MAIN'   : 'src/lcritical_barrier.o' }

class LinkLdepend(LinkClientAppExe) :
	targets = {
		'TARGET'  : 'bin/ldepend'
	,	'TARGET2' : 'bin/lunlink'
	,	'TARGET3' : 'bin/ltarget'
	,	'TARGET4' : 'bin/lcritical_barrier'
	,	'TARGET5' : 'bin/lcheck_deps'
	,	'TARGET6' : 'bin/ldep_crcs'
	}
	deps = {
		'SUPPORT' : 'src/autodep/autodep_support.o'
	,	'RECORD'  : 'src/autodep/record.o'
	,	'MAIN'    : 'src/autodep/ldepend.o'
	}
	def cmd() :
		# TARGET is computed by base class, just add necessary links
		for key,target in targets.items() :
			if key!='TARGET' : os.link(TARGET,target)

class LinkLfreeze(LinkClientAppExe) :
	targets = { 'TARGET' : 'bin/lfreeze'   }
	deps    = { 'MAIN'   : 'src/lfreeze.o' }

class LinkXxhsum(LinkClientAppExe) :
	targets = { 'TARGET' : 'bin/xxhsum'   }
	deps    = { 'MAIN'   : 'src/xxhsum.o' }
