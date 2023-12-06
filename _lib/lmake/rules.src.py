# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys    as _sys
import os     as _os
import pwd    as _pwd
import signal as _signal

import lmake
from . import has_ld_audit,pdict,root_dir                  # if not in an lmake repo, root_dir is not set to current dir

_std_path        = '$STD_PATH'                             # substituted at installation time
_bash            = '$BASH'                                 # .
_ld_library_path = '$LD_LIBRARY_PATH'                      # .
_lmake_dir       = __file__.rsplit('/lib/',1)[0]
_python          = _sys.executable

_rules = lmake._rules                  # list of rules that must be filled in by user code

# use these flags to completely ignore activity on target(s) they qualify
# this can also be used as a help to tailor your flags to your own needs
ignore_flags = (
	'-crc'         # do not generate a crc (just saves time)
,	'-dep'         # do not generate a dep if read
,	'-essential'   # do not show on graphical tool (to come)
,	'incremental'  # allow target to exist before job is run
,	'manual_ok'    # allow overwriting manual modifications
,	'-match'       # dont match (i.e. do no trigge rule on behalf of this target)
,	'source_ok'    # allow overwriting if target is a source
,	'star'         # consider target as star, even if no star stems : only actually considered a target if actually generated
,	'-stat'        # ignore stat-like accesses
,	'-uniquify'    # dont uniquify if target has several hard links pointing to it
#,	'write'        # allow writes (by default)
)

class _RuleBase :
	def __init_subclass__(cls) :
		_rules.append(cls)
	# combined attributes are aggregated, combine is itself combined but not listed here as it is dealt with separately
	# for dict, a value of None   discards the entry, sequences are mapped to dict with value True and non sequence is like a singleton
	# for set , a key   of -<key> discards <key>    , sequences are mapped to set                  and non sequence is like a singleton
	# for list & tuple, entries are concatenated    , sequences are mapped to list/tuple           and non sequence is like a singleton
	combine = {
		'path'
	,	'stems'
	,	'targets'     , 'post_targets'
	,	'deps'
	,	'environ_cmd' , 'environ_resources' , 'environ_ancillary'
	,	'resources'
	}
	# atributes listed in paths must be part of a combined dict and values are concatenated :
	# if a value contains '...' surrounded by separators, or at the beginning or end, these '...' are replaced by the inherited value
	paths = {
		'environ_cmd.PATH'            : ':'
	,	'environ_cmd.PYTHONPATH'      : ':'
	,	'environ_cmd.LD_LIBRARY_PATH' : ':'
	}
#	name                                                   # must be specific for each rule, defaults to class name
#	job_name                                               # defaults to first target
	stems = {                                              # defines stems as regexprs for use in targets & deps, e.g. {'File':'.*'}
		'__dir__'  : r'.+/|'
	,	'__file__' : r'.+'
	,	'__base__' : r'[^/]+'
	}
	targets      = {}                                      # patterns used to trigger rule execution, refers to stems above through {} notation, e.g. {'OBJ':'{File}.o'}
	#                                                      # in case of multiple matches, first matching entry is considered, others are ignored
	#                                                      # targets may have flags (use - to reset), e.g. {'TMP' : ('{File}.tmp/{Tmp*}','Phony','-Match','-Crc') }, flags may be :
	#                                                      #   flag         | default | description
	#                                                      #   -------------+---------+--------------------------------------------------------------------------------------------
	#                                                      #   Crc          | x       | compute CRC on target
	#                                                      #   Dep          | x       | make it a Dep if accessed read-only
	#                                                      #   Essential    | x       | show in graphic flow
	#                                                      #   Incremmental |         | file is not unlinked before job execution and can be read before written to or unlinked
	#                                                      #   ManualOk     |         | no error if file is modified outside lmake
	#                                                      #   Match        | x       | can match to select rule to rebuild this target
	#                                                      #   Phony        |         | file may not be generated or can be unlinked
	#                                                      #   SourceOk     |         | no error if file is a source that will be overwritten (e.g. by asking another target)
	#                                                      #   Star         |         | force target to be a star if no star stem appears (next rule will be tried if not generate)
	#                                                      #   Stat         | x       | consider stat-like accesses as reads, else ignore them
	#                                                      #   Warning      | x       | warn if unlinked by lmake before job execution and file was generated by another rule
	#                                                      #   Write        | x       | file may be written to
	post_targets = {}                                      # same as targets, except that they are processed after targets and in reverse order
#	target                                                 # syntactic sugar for targets      = {'<stdout>':<value>} (except that it is allowed)
#	post_target                                            # syntactic sugar for post_targets = {'<stdout>':<value>} (except that it is allowed)

class Rule(_RuleBase) :
	allow_stderr   = False                                 # if set, writing to stderr is not an error but a warning
	__special__    = None                                  # plain Rule
	auto_mkdir     = False                                 # auto mkdir directory in case of chdir
	backend        = 'local'                               # may be set anywhere in the inheritance hierarchy if execution must be remote
	chroot         = ''                                    # chroot directory to execute cmd (if empty, chroot cmd is not done)
	cache          = None                                  # cache used to store results for this rule. None means no caching
#	cmd                                                    # runnable if set anywhere in the inheritance hierarchy (as shell str or python function), chained if several definitions
#	cwd                                                    # cwd in which to run cmd. targets/deps are relative to it unless they start with /, in which case it means top root dir
#                                                          # defaults to the nearest root dir of the module in which the rule is defined
	deps           = {}                                    # patterns used to express explicit depencies, refers to stems through f-string notation, e.g. {'SRC':'{File}.c'}
	#                                                      # deps may have flags (use - to reset), e.g. {'TOOL':('tool','-Essential')}, flags may be :
	#                                                      #   flag         | default | description
	#                                                      #   -------------+---------+--------------------------------------------------------------------------------------------
	#                                                      #   Critical     |         | following deps are ignored if this dep is modified
	#                                                      #   Essential    | x       | show in graphic flow
#	dep                                                    # syntactic sugar for deps = {'<stdin>':<value>} (except that it is allowed)
	ete            = 0                                     # Estimated Time Enroute, initial guess for job exec time (in s)
	force          = False                                 # if set, jobs are never up-to-date, they are rebuilt every time they are needed
	ignore_stat    = False                                 # if set, stat-like syscalls do not, by themselves, trigger dependencies (but link_support is still ensured at required level)
	keep_tmp       = False                                 # keep tmp dir after job execution
	kill_sigs      = (_signal.SIGKILL,)                    # signals to use to kill jobs (send them in turn, 1s apart, until job dies, 0's may be used to set a larger delay between 2 trials)
	n_retries      = 1                                     # number of retries in case of job lost. 1 is a reasonable value
	n_tokens       = 1                                     # number of jobs likely to run in parallel for this rule (used for ETA estimation)
	prio           = 0                                     # in case of ambiguity, rules are selected with highest prio first
	python         = (_python,)                            # python used for callable cmd
	shell          = (_bash  ,)                            # shell  used for str      cmd (_sh is usually /bin/sh which may test for dir existence before chdir, which defeats auto_mkdir)
	start_delay    = 3                                     # delay before sending a start message if job is not done by then, 3 is a reasonable compromise
	max_stderr_len = 100                                   # maximum number of stderr lines shown in output (full content is accessible with lshow -e), 100 is a reasonable compromise
	timeout        = None                                  # timeout allocated to job execution (in s), must be None or an int
#	tmp            = '/tmp'                                # path under which the temporary directory is seen in the job
	job_tokens     = 1                                     # number of tokens taken by a job, follow the same syntax as deps (used for ETA estimation)
	if has_ld_audit : autodep = 'ld_audit'                 # may be set anywhere in the inheritance hierarchy if autodep uses an alternate method : none, ptrace, ld_audit, ld_preload
	else            : autodep = 'ld_preload'               # .
	resources = {                                          # used in conjunction with backend to inform it of the necessary resources to execute the job, same syntax as deps
		'cpu' : 1                                          # number of cpu's to allocate to job
#	,	'mem' : '0M'                                       # memory to allocate to job
#	,	'tmp' : '0M'                                       # temporary disk space to allocate to job
	}                                                      # follow the same syntax as deps
	environ_cmd = pdict(                                   # job execution environment, handled as part of cmd (trigger rebuild upon modification)
		HOME       = root_dir                              # favor repeatability by hiding use home dir some tools use at start up time
	,	PATH       = ':'.join((_lmake_dir+'/bin',_std_path))
	,	PYTHONPATH = ':'.join((_lmake_dir+'/lib',root_dir ))
	)
	environ_resources = pdict()                            # job execution environment, handled as resources (trigger rebuild upon modification for jobs in error)
	environ_ancillary = pdict(                             # job execution environment, does not trigger rebuild upon modification
		UID  = str(_os.getuid())                           # this may be necessary by some tools and usually does not lead to user specific configuration
	,	USER = _pwd.getpwuid(_os.getuid()).pw_name         # .
	)
	if _ld_library_path : environ_cmd['LD_LIBRARY_PATH'] = _ld_library_path

class AntiRule(_RuleBase) :
	__special__ = 'anti'               # AntiRule's are not executed, but defined at high enough prio, prevent other rules from being selected
	prio        = float('inf')         # default to high prio as the goal of AntiRule's is to hide other rules

class SourceRule(_RuleBase) :
	__special__ = 'generic_src'
	prio        = float('inf')

class HomelessRule(Rule) :
	'base rule to redirect the HOME environment variable to TMPDIR'
	def cmd() :
		import os
		os.environ['HOME'] = os.environ['TMPDIR']
	cmd.shell = 'export HOME=$TMPDIR'                                          # defining a function with a shell attribute is the way to have both python & shell pre-commands

class DirtyRule(Rule) :
	post_targets = { '__NO_MATCH__' : ('{*:.*}','-crc','-essential','incremental','-match','-warning') }

class _PyRuleBase(Rule) :
	'base rule that handle pyc creation when importing modules in Python'
	def __init_subclass__(cls) :
		super().__init_subclass__()
		if cls.ignore_stat and '-B' not in cls.python[1:] :
			raise AttributeError('if the ignore_stat attribute is set on one rule, python must run with the -B flag set on all rules')
class Py2Rule(_PyRuleBase) : post_targets = { '__PYC__' : ( r'{*:(.+/)?}{*:\w+}.pyc'                         , '-Dep','Incremental','ManualOk','-Match','-Crc' ) } # for Python2
class Py3Rule(_PyRuleBase) : post_targets = { '__PYC__' : ( r'{*:(.+/)?}__pycache__/{*:\w+}.{*:\w+-\d+}.pyc' , '-Dep','Incremental','ManualOk','-Match','-Crc' ) } # for Python3

class _DynamicPyRuleBase(Rule) :                                               # base rule that handle import of generated modules in Python
	def cmd() :                                                                # this will be executed before cmd() of concrete subclasses as cmd() are chained in case of inheritance
		from lmake.import_machinery import fix_imports
		fix_imports()
class DynamicPy2Rule(Py2Rule,_DynamicPyRuleBase) : virtual = True              # this class has targets and cmd, it looks like a concrete rule
class DynamicPy3Rule(Py3Rule,_DynamicPyRuleBase) : virtual = True              # .

PyRule        = Py3Rule
DynamicPyRule = DynamicPy3Rule

class RustRule(Rule) :
	'base rule for use by any code written in Rust (including cargo and rustc that are written in rust)'
	autodep = 'ld_preload'
