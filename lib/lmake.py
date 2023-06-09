# This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

# there are 2 entirely different modules here :
# - support module to read lmakefile
# - support module during job execution
# but for user convenience, we want to name both lmake

native_version = (1,0)

import sys     as _sys
import os.path as _osp

from clmake import *                                                           # if not in an lmake repo, root_dir is not set to current dir

Kilo = 1_000                           # convenient constants
Mega = 1_000_000                       # .
Giga = 1_000_000_000                   # .
inf  = float('inf')                    # use the repr value as variable name

def version(major,minor) :
	'''check version'''
	if major!=native_version[0] or minor<native_version[1] : raise RuntimeError(f'required version {(major,minor)} is incompatible with native version {native_version}')

class pdict(dict) :
	def __getattr__(self,attr) :
		try             : return self[attr]
		except KeyError : raise AttributeError(attr)
	def __setattr__(self,attr,val) :
		try             : self[attr] = val
		except KeyError : raise AttributeError(attr)
	def __delattr__(self,attr) :
		try             : del self[attr]
		except KeyError : raise AttributeError(attr)

if getattr(_sys,'reading_makefiles',False) :
	#
	# read lmakefile
	#

	import os         as _os
	import pwd        as _pwd
	import signal     as _signal
	import subprocess as _sp

	_std_path = _sp.check_output( 'echo $PATH' , shell=True , env={} , universal_newlines=True ).strip()
	def _get_std(cmd) :
		for d in _std_path.split(':') :
			abs_cmd = _osp.join(d,cmd)
			if _os.access(abs_cmd,_os.X_OK) : return abs_cmd
		raise FileNotFoundError(f'cannot find {cmd} in standard path {_std_path}')
	_lmake_dir    = _osp.dirname(_osp.dirname(__file__))
	_python       = _sys.executable
	_physical_mem = _os.sysconf('SC_PHYS_PAGES') * _os.sysconf('SC_PAGE_SIZE')

	# Lmakefile must define the following variables in module lmake :
	# - config  : the server configuration
	# - rules   : no default, must be set by user as the list of classes representing rules (use Rule & AntiRule base classes to help)
	# - sources : defaults to files listed in Manifest or by searching git

	#
	# config
	#

	local_admin_dir  = ''                                  # directory in which to store data that are private to the server (not accessed remote executing hosts) (default is to use LMAKE dir)
	remote_admin_dir = ''                                  # directory in which to store tmp data during remote job execution (not used when keep_tmp is enforced) (default is to use LMAKE dir)

	config = pdict(
		hash_algo       = 'Xxh'                            # algorithm to use to compute checksums on files, one of 'Xxh' or 'Md5'
	,	heartbeat       = 60                               # delay between heartbeat checks of running jobs
	,	interface       = ''                               # network interface that connect to remote hosts executing jobs
	,	link_support    = 'Full'                           # symlinks are supported. Other values are 'None' (no symlink support) or 'File' (symlink to file only support)
	,	max_dep_depth   = 1000                             # used to detect infinite recursions and loops
	,	trace_size      = 100*Mega                         # size of trace
#	,	path_max        = 400                              # max path length, but a smaller value makes debugging easier (by default, not activated)
	,	sub_prio_boost  = 1                                # increment to add to rules defined in sub-repository (multiplied by directory depth of sub-repository) to boost local rules
	,	console = pdict(                                   # tailor output lines
			date_precision = None                          # number of second decimals in the timestamp field
		,	host_length    = None                          # length of the host field (lines will be misaligned if a host is longer)
		,	has_exec_time  = True                          # if True, output the exec_time field
		)
	,	backends = pdict(                                  # PER_BACKEND : provide a default configuration for each backend
			local = pdict(                                 # besides margin, other entries mention the total availability of resources
				margin  = 0                                # compulsery in all backends, the margin to take (in seconds) to consider a newly discovered dep as reliable
		#	,	address = socket.getfqdn()                 # address at which lmake can be contacted from jobs launched by this backend, can be :
				#                                            - ''                     : loop-back address (127.0.0.1)
				#                                            - standard dot notation  : for example '192.168.0.1'
				#                                            - network interface name : the address of the host on this interface (as shown by ifconfig)
				#                                            - a host name            : the address of the host as found in networkd database (as shown by ping)
				#                                            default is loopback for local backend and hostname for the others
			,	cpu     = len(_os.sched_getaffinity(0))    # total number of cpus available for the process, and hence for all jobs launched locally
			,	mem     = _physical_mem//Mega              # total available memory (in MB)
			)
		)
	,	caches = pdict(                                    # PER_CACHE : provide an explanation for each cache method
	#		dir = pdict(                                   # when rule specifies cache = 'dir' , this cache is selected
	#			tag      'dir'                             # specify the caching method, must be one of the supported method
	#		,	repo   = root_dir                          # an id that identifies the repository, no more than one entry is stored in the cache for a given job and tag
	#		,	dir    = '/cache_dir'                      # the directory in which cached results are stored
	#		,	size   = 10*Giga                           # the overall size of this cache
	#		,	group  = os.getgroups()[0]                 # the group used to write to the cache. If user does not belong to this group, read-only access is still possible
	#		)
		)
	,	colors = pdict(
			#               normal video    reverse video
			hidden_note = ( (192,192,192) , ( 96, 96, 96) ) # gray
		,	hidden_ok   = ( (176,208,176) , ( 80,112, 80) ) # greenish gray
		,	note        = ( (  0,  0,255) , ( 64,160,255) ) # blue
		,	ok          = ( (  0,128,  0) , (128,255,128) ) # green
		,	warning     = ( (155,  0,255) , (255,  0,255) ) # magenta
		,	err         = ( (155,  0,  0) , (255,  0,  0) ) # red
		)
	)

	#
	# rules
	#
	rules = []                                             # list of rules that must be filled in by user code

	class _RuleBase :
		def __init_subclass__(cls) :
			rules.append(cls)
		# combined attributes are aggregated, combine is itself combined but not listed here as it is dealt with separately
		# for dict, a value of None   discards the entry, sequences are mapped to dict with value True and non sequence is like a singleton
		# for set , a key   of -<key> discards <key>    , sequences are mapped to set                  and non sequence is like a singleton
		# for list & tuple, entries are concatenated    , sequences are mapped to list/tuple           and non sequence is like a singleton
		combine = {
			'stems'
		,	'targets' , 'post_targets'
		,	'deps'
		,	'environ'
		,	'cmd'     , 'resources'
		}
	#	name                                               # must be specific for each rule, defaults to class name
	#	job_name                                           # defaults to first target
		stems = {                                          # defines stems as regexprs for use in targets & deps, e.g. {'File':'.*'}
			'__dir__'  : r'.+/|'
		,	'__file__' : r'.+'
		,	'__base__' : r'[^/]+'
		}
		targets      = {}                                  # patterns used to trigger rule execution, refers to stems above through {} notation, e.g. {'OBJ':'{File}.o'}
		#                                                  # in case of multiple matches, first matching entry is considered, others are ignored
		#                                                  # targets may have flags (use - to reset), e.g. {'TMP' : ('{File}.tmp/{Tmp*}','Phony','-Match','-Crc') }, flags may be :
		#                                                  #   flag         | default | description
		#                                                  #   -------------+---------+--------------------------------------------------------------------------------------------
		#                                                  #   Crc          | x       | compute CRC on target
		#                                                  #   Dep          | x       | make it a Dep if accessed read-only
		#                                                  #   Essential    | x       | show in graphic flow
		#                                                  #   Incremmental |         | file is not unlinked before job execution and can be read before written to or unlinked
		#                                                  #   ManualOk     |         | no error if file is modified outside lmake
		#                                                  #   Match        | x       | can match to select rule to rebuild this target
		#                                                  #   Phony        |         | file may not be generated or can be unlinked
		#                                                  #   SourceOk     |         | no error if file is a source that will be overwritten (e.g. by asking another target)
		#                                                  #   Star         |         | force target to be a star if no star stem appears (next rule will be tried if not generate)
		#                                                  #   Stat         | x       | consider stat-like accesses as reads, else ignore them
		#                                                  #   Warning      | x       | warn if unlinked by lmake before job execution and file was generated by another rule
		#                                                  #   Write        | x       | file may be written to
		post_targets = {}                                  # same as targets, except that they are processed after targets and in reverse order
	#	target                                             # syntactic sugar for targets      = {'<stdout>':<value>} (except that it is allowed)
	#	post_target                                        # syntactic sugar for post_targets = {'<stdout>':<value>} (except that it is allowed)

	class Rule(_RuleBase) :
		allow_stderr = False                               # if set, writing to stderr is not an error but a warning
		__anti__     = False                               # plain Rule
		auto_mkdir   = False                               # auto mkdir directory in case of chdir
		backend      = 'local'                             # may be set anywhere in the inheritance hierarchy if execution must be remote
		chroot       = ''                                  # chroot directory to execute cmd (if empty, chroot cmd is not done)
		cache        = None                                # cache used to store results for this rule. None means no caching
		cmd          = []                                  # must be set anywhere in the inheritance hierarchy for the rule to be runable, if several definitions, they are chained
	#	cwd                                                # cwd in which to run cmd. targets/deps are relative to it unless they start with /, in which case it means top root dir
	#                                                      # defaults to the nearest root dir of the module in which the rule is defined
		deps         = {}                                  # patterns used to express explicit depencies, refers to stems through f-string notation, e.g. {'SRC':'{File}.c'}
		#                                                  # deps may have flags (use - to reset), e.g. {'TOOL':('tool','-Essential')}, flags may be :
		#                                                  #   flag         | default | description
		#                                                  #   -------------+---------+--------------------------------------------------------------------------------------------
		#                                                  #   Essential    | x       | show in graphic flow
		#                                                  #   Required     | x       | requires that dep is buildable
	#	dep                                                # syntactic sugar for deps = {'<stdin>':<value>} (except that it is allowed)
		ete          = 0                                   # Estimated Time Enroute, initial guess for job exec time (in s)
		force        = False                               # if set, jobs are never up-to-date, they are rebuilt every time they are needed
		ignore_stat  = False                               # if set, stat-like syscalls do not, by themselves, trigger dependencies (but link_support is still ensured at required level)
		keep_tmp     = False                               # keep tmp dir after job execution
		kill_sigs    = (_signal.SIGKILL,)                  # signals to use to kill jobs (send them in turn, 1s apart, until job dies, 0's may be used to set a larger delay between 2 trials)
		n_tokens     = 1                                   # number of jobs likely to run in parallel for this rule (used for ETA estimation)
		prio         = 0                                   # in case of ambiguity, rules are selected with highest prio first
		python       = (_python,)                          # python used for callable cmd
		shell        = (_get_std('bash'),)                 # shell  used for str      cmd (_sh is usually /bin/sh which may test for dir existence before chdir, which defeats auto_mkdir)
	#	start_delay  = 1                                   # delay before sending a start message if job is not done by then
	#	stderr_len   = 20                                  # maximum number of stderr lines shown in output (full content is accessible with lshow -e)
		timeout      = None                                # timeout allocated to job execution (in s), must be None or an int
		job_tokens   = 1                                   # number of tokens taken by a job, follow the same syntax as deps (used for ETA estimation)
		if has_ld_audit : autodep = 'ld_audit'             # may be set anywhere in the inheritance hierarchy if autodep uses an alternate method : none, ptrace, ld_audit, ld_preload
		else            : autodep = 'ld_preload'           # .
		resources = {                                      # used in conjunction with backend to inform it of the necessary resources to execute the job, same syntax as deps
			'cpu' : 1                                      # number of cpu's to allocate to jobs
		,	'mem' : config.backends.local.mem//config.backends.local.cpu # memory to allocate to jobs
		}                                                  # follow the same syntax as deps
	#	resource                                           # syntactic sugar, a short hand for resources = {'resource':<value>}
		environ = pdict(                                   # job execution environment, default is to favor repeatability by filtering as many user specific config as possible
		#                                                  # syntax for value is either a mere str carrying the value
		#                                                  # or a tuple (value,flag) where flag specifies how lmake must handle value modification
		#                                                  # flag may be :
		#                                                  # - 'none'     :           ignore
		#                                                  # - 'resource' :           as a resource, i.e. rerun job if it was in error
		#                                                  # - 'cmd'      : (default) as a cmd     , i.e. rerun job upon any modification
			HOME       = root_dir                                    # favor repeatability by hiding use home dir some tools use at start up time
		,	PATH       = ':'.join(( _lmake_dir+'/bin' , _std_path ))
		,	PYTHONPATH = ':'.join(( _lmake_dir+'/lib' ,           ))
		,	UID        = ( str(_os.getuid())                  ,'none') # this may be necessary by some tools and usually does not lead to user specific configuration
		,	USER       = ( _pwd.getpwuid(_os.getuid()).pw_name,'none') # .
		)

	class AntiRule(_RuleBase) :
		__anti__ = True                                    # AntiRule's are not executed, but defined at high enough prio, prevent other rules from being selected
		prio     = float('inf')                            # default to high prio as the goal of AntiRule's is to hide other rules

	class GitRule(Rule) :
		'base rule that ignores read accesses (and forbid writes) to git administrative files'
		post_targets = { '__GIT__' : ( '{__dir__*}.git/{__file__*}' , '-Dep','Incremental','-Match','-Write' ) }

	class HomelessRule(Rule) :
		'base rule to redirect the HOME environment variable to TMPDIR'
		def cmd() :
			import os
			os.environ['HOME'] = os.environ['TMPDIR']
		cmd.shell = 'export HOME=$TMPDIR'                                      # defining a function with a shell attribute is the way to have both python & shell pre-commands

	class DirtyRule(Rule) :
		post_targets = { '__NO_MATCH__' : ('{*:.*}','-crc','-essential','incremental','-match','-warning') }

	class PyRule(Rule) :
		'base rule that handle pyc creation when importing modules in Python'
		def __init_subclass__(cls) :
			super().__init_subclass__()
			if cls.ignore_stat and '-B' not in cls.python[1:] :
				raise AttributeError('if the ignore_stat attribute is set on one rule, python must run with the -B flag set on all rules')
		# ignore pyc files, Python takes care of them
		post_targets = { '__PYC__' : ( r'{__dir__*}__pycache__/{__base__*}.{__cache_tag__*:\w+-\d+}.pyc' , '-Dep','Incremental','ManualOk','-Match','-Crc' ) }

	class DynamicPyRule(PyRule) :                                              # base rule that handle import of generated modules in Python
		virtual = True
		def cmd() :                                                            # this will be executed before cmd() of concrete subclasses as cmd() are chained in case of inheritance
			fix_imports()

	class RustRule(Rule) :
		'base rule for use by any code written in Rust (including cargo and rustc that are written in rust)'
		autodep = 'ld_preload'

	#
	# sources
	#

#sources = ()      # this variable may be set by Lmakefile.py to provide the list of sources, if it is not, auto_sources() is called to fill it

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
		return [ f for f in ( line_re.fullmatch(l).group('file') for l in open(manifest) ) if f ]

	def git_sources(recurse=True,**kwds) :
		'''
			gather git controled files.
			recurse to sub-modules if recurse is True
			kwds are ignored which simplifies the usage of auto_sources
		'''
		import subprocess
		if not _osp.isdir('.git') : raise FileNotFoundError('.git')
		git_cmd = ['git','ls-files']
		if recurse : git_cmd.append('--recurse-submodules')
		srcs = subprocess.check_output( git_cmd , universal_newlines=True ).splitlines()
		srcs += ('.git/HEAD','.git/config','.git/index')                                 # not listed by git, but actually a source as it is read by git ls-files # XXX : apply to submodules as well
		return srcs
	def git_sources( recurse=True , ignore_missing_submodules=False , **kwds ) :
		'''
			gather git controled files.
			recurse to sub-modules if recurse is True
			kwds are ignored which simplifies the usage of auto_sources
		'''
		import subprocess
		if not _osp.isdir('.git') : raise FileNotFoundError('.git')

		if recurse :
			git_cmd = ['git','submodule','--quiet','foreach','--recursive','echo $displaypath']
			submodules = subprocess.check_output( git_cmd , universal_newlines=True ).splitlines()

		git_cmd = ['git','ls-files']
		if recurse : git_cmd.append('--recurse-submodules')
		srcs  = subprocess.check_output( git_cmd , universal_newlines=True ).splitlines()
		srcs += ('.git/HEAD','.git/config','.git/index')
		srcs += [_osp.join('.git',f) for f in ('HEAD','config','index') ]      # not listed by git, but actually a source as it is read by git ls-files

		for submodule in submodules :
			sub_git = _osp.join(submodule,'.git')
			# XXX : fix using 'git submodule status' to detect and handle cases where submodules are not loaded but this creates more dependencies
			if not _osp.exists(sub_git) and not ignore_missing_submodules : raise FileNotFoundError(f'cannot find git submodule {submodule}')
			git_dir  = open(sub_git).readline().split()[1]
			sub_git_dir  = _osp.normpath(_osp.join(_osp.dirname(sub_git),git_dir))
			srcs    += ( sub_git , *( _osp.join(sub_git_dir,f) for f in ('HEAD','config','index') ) ) # not listed by git, but actually a source as it is read by git ls-files
		return srcs

	def auto_sources(**kwds) :
		'''
			auto-determine used source control
			kwds can be passed to underlying source control
		'''
		for ctl in ( manifest_sources , git_sources ) :
			try    : return ctl(**kwds)
			except : pass
		raise FileNotFoundError(f'no source control found')

#
# job execution, available in rules to have full access when writing rule cmd's
#

import importlib.machinery as _machinery

module_suffixes = ('.so','.py','/__init__.py')                             # you can put the adequate list for your application here, the lesser the better (less spurious dependencies)
_std_suffixes   = _machinery.all_suffixes()+['/__init__.py']               # account for packages, not included in all_suffixes()
_local_admin    = 'LMAKE/'
_global_admin   = root_dir+_local_admin

def _is_local(file) :
	if file[0]!='/'              : return not file.startswith(_local_admin )
	if file.startswith(root_dir) : return not file.startswith(_global_admin)
	else                         : return False

def fix_imports() :
	'''fixes imports so as to be sure all files needed to do an import is correctly reported (not merely those which exist)'''
	class Depend :
		@staticmethod
		def find_spec(file_name,path,target=None) :
			if path==None : path = _sys.path
			tail = file_name.rsplit('.',1)[-1]
			for dir in path :
				dir += '/'
				base = dir+tail
				if _is_local(dir) :
					for suffix in module_suffixes :
						file = base+suffix
						depend(file)
						if _osp.exists(file) : return
				else :
					for suffix in _std_suffixes :
						if _osp.exists(base+suffix) : return

	# give priority to system so as to avoid too numerous dependencies
	_sys.path = (
		[ d for d in _sys.path if not _is_local(d+'/') ]
	+	[ d for d in _sys.path if     _is_local(d+'/') ]
	)

	# put dependency checker before the first path based finder
	for i in range(len(_sys.meta_path)) :
		if _sys.meta_path[i]==_machinery.PathFinder :
			_sys.meta_path.insert(i,Depend)
			break
	else :
		_sys.meta_path.append(Depend)
