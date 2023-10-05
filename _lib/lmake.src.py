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

_reading_makefiles = getattr(_sys,'reading_makefiles',False)

from clmake import *                                                           # if not in an lmake repo, root_dir is not set to current dir

Kilo =      1024                       # convenient constants
Mega = Kilo*1024                       # .
Giga = Mega*1024                       # .
Tera = Giga*1024                       # .
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

if _reading_makefiles :
	#
	# read lmakefile
	#

	import os         as _os
	import pwd        as _pwd
	import signal     as _signal
	import subprocess as _sp

	_std_path = _sp.run( 'echo $PATH' , shell=True , check=True , stdout=_sp.PIPE , env={} , universal_newlines=True ).stdout.strip()
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

	config = pdict(
		hash_algo        = 'Xxh'                           # algorithm to use to compute checksums on files, one of 'Xxh' or 'Md5'
	,	heartbeat        = 60                              # delay between heartbeat checks of running jobs
	,	link_support     = 'Full'                          # symlinks are supported. Other values are 'None' (no symlink support) or 'File' (symlink to file only support)
#	,	local_admin_dir  = 'LMAKE'                         # directory in which to store data that are private to the server (not accessed remote executing hosts) (default is to use LMAKE dir)
	,	max_dep_depth    = 1000                            # used to detect infinite recursions and loops
#	,	max_error_lines  = 30                              # used to limit the number of error lines when not reasonably limited otherwise
	,	network_delay    = 3                               # delay between job completed and server aware of it. Too low, there may be spurious lost jobs. Too high, tool reactivity may rarely suffer.
	,	trace_size       = 100*Mega                        # size of trace
#	,	path_max         = 400                             # max path length, but a smaller value makes debugging easier (by default, not activated)
#	,	remote_admin_dir = 'LMAKE'                         # directory in which to store job trace during remote job execution (not used when keep_tmp is enforced) (default is to use LMAKE dir)
#	,	remote_tmp_dir   = 'LMAKE'                         # directory in which to store tmp data during remote job execution (not used when keep_tmp is enforced) (default is to use LMAKE dir)
	,	source_dirs      = []                              # files in these directories are deemed to be sources
	,	sub_prio_boost   = 1                               # increment to add to rules defined in sub-repository (multiplied by directory depth of sub-repository) to boost local rules
	,	console = pdict(                                   # tailor output lines
			date_precision = None                          # number of second decimals in the timestamp field
		,	host_length    = None                          # length of the host field (lines will be misaligned if a host is longer)
		,	has_exec_time  = True                          # if True, output the exec_time field
		)
	,	backends = pdict(                                  # PER_BACKEND : provide a default configuration for each backend
			precisions = pdict(                            # precision of resources allocated for jobs, one entry for each standard resource (for all backends).
		#		cpu = 4                                    # 4 means possible values are 1 2, 3, 4, 6, 8, 12, ...
		#	,	mem = 4                                    # 8 would mean possible values are 1 2, 3, 4, 5, 6, 7, 8, 10, ...
		#	,	tmp = 4                                    # .
			)
		,	local = pdict(                                 # entries mention the total availability of resources
		#	,	interface = socket.getfqdn()               # address at which lmake can be contacted from jobs launched by this backend, can be :
				#                                            - ''                     : loop-back address (127.0.0.1) for local backend, hostname for remote backends
				#                                            - standard dot notation  : for example '192.168.0.1'
				#                                            - network interface name : the address of the host on this interface (as shown by ifconfig)
				#                                            - a host name            : the address of the host as found in networkd database (as shown by ping)
				#                                            default is loopback for local backend and hostname for the others
				cpu = len(_os.sched_getaffinity(0))        # total number of cpus available for the process, and hence for all jobs launched locally
			,	mem = _physical_mem                        # total available memory in bytes
			,	tmp = 0                                    # total available temporary disk space in bytes
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
		,	'targets'     , 'post_targets'
		,	'deps'
		,	'environ_cmd' , 'environ_resources' , 'environ_ancillary'
		,	'resources'
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
		__special__  = None                                # plain Rule
		auto_mkdir   = False                               # auto mkdir directory in case of chdir
		backend      = 'local'                             # may be set anywhere in the inheritance hierarchy if execution must be remote
		chroot       = ''                                  # chroot directory to execute cmd (if empty, chroot cmd is not done)
		cache        = None                                # cache used to store results for this rule. None means no caching
	#	cmd                                                # runnable if set anywhere in the inheritance hierarchy (as shell str or python function), chained if several definitions
	#	cwd                                                # cwd in which to run cmd. targets/deps are relative to it unless they start with /, in which case it means top root dir
	#                                                      # defaults to the nearest root dir of the module in which the rule is defined
		deps         = {}                                  # patterns used to express explicit depencies, refers to stems through f-string notation, e.g. {'SRC':'{File}.c'}
		#                                                  # deps may have flags (use - to reset), e.g. {'TOOL':('tool','-Essential')}, flags may be :
		#                                                  #   flag         | default | description
		#                                                  #   -------------+---------+--------------------------------------------------------------------------------------------
		#                                                  #   Critical     |         | following deps are ignored if this dep is modified
		#                                                  #   Essential    | x       | show in graphic flow
	#	dep                                                # syntactic sugar for deps = {'<stdin>':<value>} (except that it is allowed)
		ete          = 0                                   # Estimated Time Enroute, initial guess for job exec time (in s)
		force        = False                               # if set, jobs are never up-to-date, they are rebuilt every time they are needed
		ignore_stat  = False                               # if set, stat-like syscalls do not, by themselves, trigger dependencies (but link_support is still ensured at required level)
		keep_tmp     = False                               # keep tmp dir after job execution
		kill_sigs    = (_signal.SIGKILL,)                  # signals to use to kill jobs (send them in turn, 1s apart, until job dies, 0's may be used to set a larger delay between 2 trials)
		local_marker = '$CWD'                              # a marker recognized in environ_* attributes and replaced by the cwd of the rule to allow cache effenciency
	#	n_retries    = 1                                   # number of retries in case of job lost. 1 might be a reasonable value
		n_tokens     = 1                                   # number of jobs likely to run in parallel for this rule (used for ETA estimation)
		prio         = 0                                   # in case of ambiguity, rules are selected with highest prio first
		python       = (_python,)                          # python used for callable cmd
		shell        = (_get_std('bash'),)                 # shell  used for str      cmd (_sh is usually /bin/sh which may test for dir existence before chdir, which defeats auto_mkdir)
	#	start_delay  = 1                                   # delay before sending a start message if job is not done by then
	#	stderr_len   = 20                                  # maximum number of stderr lines shown in output (full content is accessible with lshow -e)
		timeout      = None                                # timeout allocated to job execution (in s), must be None or an int
	#	tmp          = '/tmp'                              # path under which the temporary directory is seen in the job
		job_tokens   = 1                                   # number of tokens taken by a job, follow the same syntax as deps (used for ETA estimation)
		if has_ld_audit : autodep = 'ld_audit'             # may be set anywhere in the inheritance hierarchy if autodep uses an alternate method : none, ptrace, ld_audit, ld_preload
		else            : autodep = 'ld_preload'           # .
		resources = {                                      # used in conjunction with backend to inform it of the necessary resources to execute the job, same syntax as deps
			'cpu' : 1                                      # number of cpu's to allocate to job
	#	,	'mem' : 0                                      # memory to allocate to job
	#	,	'tmp' : 0                                      # temporary disk space to allocate to job
		}                                                  # follow the same syntax as deps
		environ_cmd = pdict(                               # job execution environment, handled as part of cmd (trigger rebuild upon modification)
			HOME       = '$CWD'                            # favor repeatability by hiding use home dir some tools use at start up time
		,	PATH       = ':'.join(( _lmake_dir+'/bin' , _std_path ))
		,	PYTHONPATH = ':'.join(( _lmake_dir+'/lib' ,           ))
		)
		environ_resources = pdict()                        # job execution environment, handled as resources (trigger rebuild upon modification for jobs in error)
		environ_ancillary = pdict(                         # job execution environment, does not trigger rebuild upon modification
			UID  = str(_os.getuid())                       # this may be necessary by some tools and usually does not lead to user specific configuration
		,	USER = _pwd.getpwuid(_os.getuid()).pw_name     # .
		)

	class AntiRule(_RuleBase) :
		__special__ = 'anti'                               # AntiRule's are not executed, but defined at high enough prio, prevent other rules from being selected
		prio        = float('inf')                         # default to high prio as the goal of AntiRule's is to hide other rules

	class SourceRule(_RuleBase) :
		__special__ = 'generic_src'
		prio        = float('inf')

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

	#sources = ()  # this variable may be set by Lmakefile.py to provide the list of sources, if it is not, auto_sources() is called to fill it

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

	_git = 'git'                                                                 # overridden by installation configuration
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
		root_dir    = _os.getcwd()
		git_dir     = root_dir
		rel_git_dir = ''
		while git_dir!='/' and not _osp.isdir(_osp.join(git_dir,'.git')) :
			git_dir      = _osp.dirname(git_dir)
			rel_git_dir += '../'
		if git_dir =='/' : raise NotImplementedError('not in a git repository')
		git_dir_s  = _osp.join(git_dir ,'')
		root_dir_s = _osp.join(root_dir,'')
		assert root_dir_s.startswith(git_dir_s),f'found git dir {git_dir} is not a prefix of root dir {root_dir}'
		repo_dir_s = root_dir_s[len(git_dir_s):]
		#
		# compute file lists
		#
		if recurse :
			# compute submodules
			# old versions of git (e.g. 1.8) do not support submodule command when not launched from top nor $displaypath
			submodules = run( (_git,'submodule','--quiet','foreach','--recursive','echo $toplevel/$sm_path') , git_dir )    # less file accesses than git submodule status
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
		config.source_dirs.append( rel_git_dir+'.git' )
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

#
# job execution, available in rules to have full access when writing rule cmd's
#

module_suffixes = ('.so','.py','/__init__.py')                                 # can be tailored to suit application needs, the lesser the better (less spurious dependencies)

import importlib.machinery as _machinery

_std_suffixes = _machinery.all_suffixes()+['/__init__.py']                     # account for packages, not included in all_suffixes()
_local_admin  = 'LMAKE/'
_global_admin = root_dir+'/'+_local_admin

def _maybe_local(file) :
	'fast check for local files, avoiding full absolute path generation'
	return not file or file[0]!='/' or file.startswith(root_dir)

def fix_imports() :
	'''fixes imports so as to be sure all files needed to do an import is correctly reported (not merely those which exist)'''
	class Depend :
		@staticmethod
		def find_spec(module_name,path,target=None) :
			if path==None : path = _sys.path
			tail = module_name.rsplit('.',1)[-1]
			for dir in path :
				if dir : dir += '/'
				base = dir+tail
				if _maybe_local(base) :
					for suffix in module_suffixes :
						file = base+suffix
						depend(file,required=False,essential=False)
						if _osp.exists(file) : return
				else :
					for suffix in _std_suffixes :
						if _osp.exists(base+suffix) : return

	# give priority to system so as to avoid too numerous dependencies
	_sys.path = (
		[ d for d in _sys.path if not _maybe_local(d+'/') ]
	+	[ d for d in _sys.path if     _maybe_local(d+'/') ]
	)

	# put dependency checker before the first path based finder
	for i in range(len(_sys.meta_path)) :
		if _sys.meta_path[i]==_machinery.PathFinder :
			_sys.meta_path.insert(i,Depend)
			break
	else :
		_sys.meta_path.append(Depend)

#
# system configuration computed at installation time
#

