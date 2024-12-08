# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

'lmake.rules source file (as named in lmake.rules.__file__) is thoroughly commented, please refer to it.'

import sys     as _sys
import os      as _os
import os.path as _osp
import pwd     as _pwd
import signal  as _signal

import lmake
from . import autodeps,pdict

shell            = '$BASH'            # substituted at installation time
python2          = '$PYTHON2'         # .
python           = '$PYTHON'          # .
_std_path        = '$STD_PATH'        # .
_ld_library_path = '$LD_LIBRARY_PATH' # .

class _RuleBase :
	def __init_subclass__(cls) :
		lmake._rules.append(cls)                           # list of rules
		for old,new in (                                   # XXX : suppress when backward compatibility can be suppressed
			( 'environ_cmd'      , 'environ'             )
		,	( 'max_submit_count' , 'max_submits'         )
		,	( 'n_retries'        , 'max_retries_on_lost' )
		,	( 'root_view'        , 'repo_view'           )
		) :
			if old in cls.__dict__ and not new in cls.__dict__ :
				setattr( cls , new , getattr(cls,old) )
				delattr( cls , old                    )
	__special__ = None
	# combined attributes are aggregated, combine is itself combined but not listed here as it is dealt with separately
	# for dict, a value of None   discards the entry, sequences are mapped to dict with value True and non sequence is like a singleton
	# for set , a key   of -<key> discards <key>    , sequences are mapped to set                  and non sequence is like a singleton
	# for list & tuple, entries are concatenated    , sequences are mapped to list/tuple           and non sequence is like a singleton
	combine = {
		'stems'
	,	'targets' , 'side_targets'
	,	'deps'    , 'side_deps'
	,	'order'
	,	'environ' , 'environ_resources' , 'environ_ancillary'
	,	'resources'
	,	'views'
	}
	# atributes listed in paths must be part of a combined dict and values are concatenated :
	# if a value contains '...' surrounded by separators, or at the beginning or end, these '...' are replaced by the inherited value
	paths = {
		'environ.PATH'                : ':'
	,	'environ.PYTHONPATH'          : ':'
	,	'environ.LD_LIBRARY_PATH'     : ':'
	}
#	name              # must be specific for each rule, defaults to class name
#	job_name          # defaults to first target
	stems   = {}      # defines stems as regexprs for use in targets & deps, e.g. {'File':'.*'}
	targets = {}      # patterns used to trigger rule execution, refers to stems above through {} notation, e.g. {'OBJ':'{File}.o'}
	#                 # in case of multiple matches, first matching entry is considered, others are ignored
	#                 # targets may have flags (use - to reset), e.g. {'TMP' : ('{File}.tmp/{Tmp*}','Incremental','-Essential') }, flags may be :
	#                 #   flag         | default   | description
	#                 #   -------------+-----------+--------------------------------------------------------------------------------------------
	#                 #   Essential    | if static | show in graphic flow
	#                 #   Incremmental |           | file is not unlinked before job execution and can be read before written to or unlinked
	#                 #   SourceOk     |           | no error if target is actually a source
	#                 #   NoUniquify   |           | dont uniquify file if incremental and file has several links to it
	#                 #   NoWarning    |           | dont warn if either uniquified or unlinked before job execution and file was generated by another job
#	target            # syntactic sugar for targets = {'<stdout>':<value>} (except that it is allowed)
	side_targets = {} # patterns used to add flags based on pattern matching refering to stems above through {} notation, e.g. {'CACHE':'{File}.cache','incremental'}
	side_deps    = {} # .
	order        = [] # explicit matching order of keys in targets, side_targets and side_deps. If partial, other keys are put after specified ones

class Rule(_RuleBase) :
#	allow_stderr        = False                        # if set, writing to stderr is not an error but a warning
#	auto_mkdir          = False                        # auto mkdir directory in case of chdir
	backend             = 'local'                      # may be set anywhere in the inheritance hierarchy if execution must be remote
#	chroot_dir          = '/'                          # chroot directory to execute cmd (if None, empty or absent, no chroot is not done)
#	cache               = None                         # cache used to store results for this rule. None means no caching
#	cmd                                                # runnable if set anywhere in the inheritance hierarchy (as shell str or python function), chained if several definitions
	cwd                 = ''                           # cwd in which to run cmd. targets/deps are relative to it unless they start with /, in which case it means top root dir
#                                                      # defaults to the nearest root dir of the module in which the rule is defined
	deps                = {}                           # patterns used to express explicit depencies, full f-string notation with stems and targets defined, e.g. {'SRC':'{File}.c'}
	#                                                  # deps may have flags (use - to reset), e.g. {'TOOL':('tool','Critical','-Essential')}, flags may be :
	#                                                  #   flag        | default | description
	#                                                  #   ------------+---------+--------------------------------------------------------------------------------------------
	#                                                  #   Essential   | x       | show in graphic flow
	#                                                  #   Critical    |         | following deps are ignored if this dep is modified
	#                                                  #   IgnoreError |         | accept dep even if generated in error
#	dep                                                # syntactic sugar for deps = {'<stdin>':<value>} (except that it is allowed)
#	ete                 = 0                            # Estimated Time Enroute, initial guess for job exec time (in s)
#	force               = False                        # if set, jobs are never up-to-date, they are rebuilt every time they are needed
	max_submits         = 10                           # maximum number a job can be submitted in a single lmake command, unlimited if None
	max_submit_count    = max_submits                  # XXX : suppress when backward compatibility can be suppressed
#	ignore_stat         = False                        # if set, stat-like syscalls do not, by themselves, trigger dependencies (but link_support is still ensured at required level)
#	job_tokens          = 1                            # number of tokens taken by a job, follow the same syntax as deps (used for ETA estimation)
#	keep_tmp            = False                        # keep tmp dir after job execution
	kill_sigs           = (_signal.SIGKILL,)           # signals to use to kill jobs (send them in turn, 1s apart, until job dies, 0's may be used to set a larger delay between 2 trials)
	max_retries_on_lost = 1                            # max number of retries in case of job lost. 1 is a reasonable value
	n_retries           = max_retries_on_lost          # XXX : suppress when backward compatibility can be suppressed
	max_stderr_len      = 100                          # maximum number of stderr lines shown in output (full content is accessible with lshow -e), 100 is a reasonable compromise
#	prio                = 0                            # in case of ambiguity, rules are selected with highest prio first
	python              = (python,)                    # python used for callable cmd
#	repo_view           = '/repo'                      # absolute path under which the root directory of the repo is seen (if None, empty, or absent, no bind mount is done)
	shell               = (shell ,)                    # shell  used for str      cmd (_sh is usually /bin/sh which may test for dir existence before chdir, which defeats auto_mkdir)
	start_delay         = 3                            # delay before sending a start message if job is not done by then, 3 is a reasonable compromise
#	timeout             = None                         # timeout allocated to job execution (in s), must be None or an int
#	tmp_view            = '/tmp'                       # may be :
	#                                                  # - not specified, '' or None : do not mount tmp dir
	#                                                  # - str                       : must be an absolute path which tmp dir is mounted on.
	#                                                  # physical tmp dir is :
	#                                                  # - $TMPDIR if provided in the environment
	#                                                  # - else a tmpfs sized after the 'tmp' resource if specified (no tmpfs is created if value is 0)
	#                                                  # - else a private sub-directory in the LMAKE directory
#	use_script       = False                           # use a script to run job rather than calling interpreter with -c
	if 'ld_audit' in autodeps : autodep = 'ld_audit'   # may be set anywhere in the inheritance hierarchy if autodep uses an alternate method : none, ptrace, ld_audit, ld_preload
	else                      : autodep = 'ld_preload' # .
	resources = {                                      # used in conjunction with backend to inform it of the necessary resources to execute the job, same syntax as deps
		'cpu' : 1                                      # number of cpu's to allocate to job
#	,	'mem' : '100M'                                 # memory to allocate to job
#	,	'tmp' : '1G'                                   # temporary disk space to allocate to job
	}                                                  # follow the same syntax as deps
	environ = pdict(                                   # job execution environment, handled as part of cmd (trigger rebuild upon modification)
		HOME = '$REPO_ROOT'                            # favor repeatability by hiding use home dir some tools use at start up time
	,	PATH = ':'.join(('$LMAKE_ROOT/bin',_std_path))
	)
	environ_cmd       = environ                        # XXX : suppress when backward compatibility can be suppressed
	environ_resources = pdict()                        # job execution environment, handled as resources (trigger rebuild upon modification for jobs in error)
	environ_ancillary = pdict(                         # job execution environment, does not trigger rebuild upon modification
		UID  = str(_os.getuid())                       # this may be necessary by some tools and usually does not lead to user specific configuration
	,	USER = _pwd.getpwuid(_os.getuid()).pw_name     # .
	)
	if _ld_library_path : environ['LD_LIBRARY_PATH'] = _ld_library_path

class AntiRule(_RuleBase) :
	__special__ = 'anti'       # AntiRule's are not executed, but defined at high enough prio, prevent other rules from being selected
	prio        = float('inf') # default to high prio as the goal of AntiRule's is to hide other rules

class SourceRule(_RuleBase) :
	__special__ = 'generic_src'
	prio        = float('inf')

class HomelessRule(Rule) :
	'base rule to redirect the HOME environment variable to tmp'
	environ = pdict(HOME='$TMPDIR')

class TraceRule(Rule) :
	'base rule to trace shell commands to stdout'
	cmd = '''
		exec 3>&1
		export BASH_XTRACEFD=3
		set -x
	'''

class DirtyRule(Rule) :
	side_targets = { '__NO_MATCH__' : ('{*:.*}','Incremental','NoWarning') }

class _PyRule(Rule) :
	environ = pdict( PYTHONPATH=':'.join(('$LMAKE_ROOT/lib','$REPO_ROOT')) )
class Py2Rule(_PyRule) :
	'base rule that handle pyc creation when importing modules in Python'
	# python reads the pyc file and compare stored date with actual py date (through a stat), but semantic is to read the py file
	side_targets = { '__PYC__' : ( r'{*:(?:.+/)?}{*:\w+}.pyc' , 'incremental','top' ) }
	python       = python2
	# this will be executed before cmd() of concrete subclasses as cmd() are chained in case of inheritance
	def cmd() :
		import sys
		assert sys.version_info.major==2 , 'cannot use Py2Rule with python%d.%d'%(sys.version_info.major,sys.version_info.minor)
		try                : import lmake
		except ImportError : sys.path[0:0] = (_os.environ['LMAKE_ROOT']+'/lib',)
		from lmake.import_machinery import fix_import
		fix_import('Py2Rule')
	cmd.shell = ''                                                             # support shell cmd's that may launch python as a subprocess XXX : manage to execute fix_import()
class Py3Rule(_PyRule) :
	'base rule that handle pyc creation when importing modules in Python'
	# python reads the pyc file and compare stored date with actual py date (through a stat), but semantic is to read the py file
	side_targets = { '__PYC__' : ( r'{*:(?:.+/)?}__pycache__/{*:\w+}.{*:\w+-\d+}.pyc' , 'incremental','top' ) }
	# this will be executed before cmd() of concrete subclasses as cmd() are chained in case of inheritance
	def cmd() :
		import sys
		assert sys.version_info.major==3 , 'cannot use Py3Rule with python%d.%d'%(sys.version_info.major,sys.version_info.minor)
		try                : import lmake
		except ImportError : sys.path[0:0] = (_os.environ['LMAKE_ROOT']+'/lib',)
		from lmake.import_machinery import fix_import
		fix_import('Py3Rule')
	cmd.shell = ''                                                             # support shell cmd's that may launch python as a subprocess XXX : manage to execute fix_import()

PyRule = Py3Rule

class RustRule(Rule) :
	'base rule for use by any code written in Rust (including cargo and rustc that are written in rust)'
	autodep = 'ld_preload'                                                                               # rust use a dedicated loader that does not call auditing code when using ld_audit
	if 'RUSTUP_HOME' in _os.environ :
		environ = {
			'RUSTUP_HOME' : _os.environ['RUSTUP_HOME']                                                   # ensure var is passed to job
		,	'PATH'        : _osp.dirname(_os.environ['RUSTUP_HOME'])+'/.cargo/bin:...'                   # ... stands for inherited value
		}

class DirRule(Rule) :
	'''
		Base rule to ensure the existence of a dir by generating a target within said dir.
		The default marker is '...'.
		Usage :
			class MyDirRule(DirRule) : pass
		or :
			class MyDirRule(DirRule) : marker='my_marker'
		Note : in case of conflict with other rules, you may have to adjust prio
		Then to ensure that dir exists :
			lmake.depend(dir+'/'+marker)
	'''
	virtual = True
	marker  = '...'
	target  = fr'{{Dir:.+}}/{marker}'
	backend = 'local'                 # command is faster than any other backend overhead
	cmd     = ''
