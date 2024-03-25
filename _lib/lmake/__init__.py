# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys as _sys
import os  as _os

if _sys.version_info.major<3 : from clmake2 import * # if not in an lmake repo, root_dir is not set to current dir
else                         : from clmake  import * # .

from .utils import *

native_version = (1,0)

def version(major,minor) :
	'''check version'''
	if major!=native_version[0] or minor<native_version[1] : raise RuntimeError('required version '+str((major,minor))+' is incompatible with native version '+str(native_version))

# Lmakefile must :
# - update variable lmake.config : the server configuration, default is a reasonable configuration
# - define rules :
#	- either by defining classes inheriting from one of the base rule classes : lmake.Rule, lmake.Antirule, lmake.PyRule, etc.
#	- or set lmake.config.rules_module to specify a module that does the same thing when imported
# - define sources :
#	- do nothing : default is to list files in Manifest or by searching git (including sub-modules)
#	- define variable lmake.manifest as a list or a tuple that lists sources
#	- set lmake.config.sources_module to specify a module that does the same thing when imported

manifest = []
_rules   = []

#
# config
#

_mem = _os.sysconf('SC_PHYS_PAGES') * _os.sysconf('SC_PAGE_SIZE')
if _sys.version_info.major<3 :
	import multiprocessing as _mp
	try                        : _cpu = _mp.cpu_count()
	except NotImplementedError : _cpu = 1
else :
	_cpu = len(_os.sched_getaffinity(0))
#_interface = _socket.getfqdn()
#_group     = _os.getgroups()[0]

config = pdict(
	heartbeat        = 10             # in seconds, minimum interval between 2 heartbeat checks (and before first one) for the same job (no heartbeat if None)
,	heartbeat_tick   =  0.1           # in seconds, minimum internval between 2 heartbeat checks (globally)                             (no heartbeat if None)
,	link_support     = 'Full'         # symlinks are supported. Other values are 'None' (no symlink support) or 'File' (symlink to file only support)
#,	local_admin_dir  = 'LMAKE_LOCAL'  # directory in which to store data that are private to the server (not accessed by remote executing hosts) (default is within LMAKE dir)
,	max_dep_depth    = 1000           # used to detect infinite recursions and loops
,	max_error_lines  = 100            # used to limit the number of error lines when not reasonably limited otherwise
,	network_delay    = 1              # delay between job completed and server aware of it. Too low, there may be spurious lost jobs. Too high, tool reactivity may rarely suffer.
,	path_max         = 400            # max path length, but a smaller value makes debugging easier (by default, not activated)
#,	reliable_dirs    = False          # if true, close to open coherence is deemed to encompass enclosing directory coherence (improve performances)
#	                                  # - forced true if only local backend is used
#	                                  # - set   true  for ceph
#	                                  # - leave false for NFS
#,	remote_admin_dir = 'LMAKE_REMOTE' # directory in which to store job trace during remote job execution                                      (default is within LMAKE dir)
#,	remote_tmp_dir   = 'LMAKE_TMP'    # directory in which to store tmp data  during remote job execution (not used when keep_tmp is enforced) (default is within LMAKE dir)
#,	rules_module     = 'rules'        # module to import to define rules  . By default, rules are directly defined in Lmakefile.py
#,	sources_module   = 'sources'      # module to import to define sources. By default, 'lmake.auto_sources' which lists files in Manifest or searches git (recursively) if lmake.sources is not set
,	sub_prio_boost   = 1              # increment to add to rules defined in sub-repository (multiplied by directory depth of sub-repository) to boost local rules
,	console = pdict(                  # tailor output lines
		date_precision = None         # number of second decimals in the timestamp field
	,	host_length    = None         # length of the host field (lines will be misaligned if a host is longer)
	,	has_exec_time  = True         # if True, output the exec_time field
	)
,	n_tokens_tab = pdict()            # table of number of tokens referenced by rules. This indirection allows dynamic update of this value while rules cannot be dynamically updated
,	backends = pdict(                 # PER_BACKEND : provide a default configuration for each backend
		precisions = pdict(           # precision of resources allocated for jobs, one entry for each standard resource (for all backends).
			cpu = 4                   # encodes the highest number with full granularity, 4 is a reasonable value
		,	mem = 4                   # 4 means possible values are 1 2, 3, 4, 6, 8, 10, 12, 14, 16, 20, 24, 28, 32, 40...
		,	tmp = 4                   # 8 would mean possible values are 1 2, 3, 4, 5, 6, 7, 8, 10, ...
		)
	,	local = pdict(                # entries mention the total availability of resources
		#	interface = _interface    # address at which lmake can be contacted from jobs launched by this backend, can be :
		#	                          # - ''                     : loop-back address (127.0.0.1) for local backend, hostname for remote backends
		#	                          # - standard dot notation  : for example '192.168.0.1'
		#	                          # - network interface name : the address of the host on this interface (as shown by ifconfig)
		#	                          # - a host name            : the address of the host as found in networkd database (as shown by ping)
		#	                          # - default is loopback for local backend and hostname for the others
			cpu = _cpu                # total number of cpus available for the process, and hence for all jobs launched locally
		,	mem = str(_mem>>20)+'M'   # total available memory in MBytes
		,	tmp = 0                   # total available temporary disk space in MBytes
		)
	)
,	caches = pdict(                   # PER_CACHE : provide an explanation for each cache method
	#	dir = pdict(                  # when rule specifies cache = 'dir' , this cache is selected
	#		tag    = 'dir'            # specify the caching method, must be one of the supported method
	#	,	repo   = root_dir         # an id that identifies the repository, no more than one entry is stored in the cache for a given job and tag
	#	,	dir    = '/cache_dir'     # the directory in which cached results are stored
	#	,	size   = 10<<30           # the overall size of this cache
	#	,	group  = _group           # the group used to write to the cache. If user does not belong to this group, read-only access is still possible
	#	)
	)
,	colors = pdict(
		#                 normal video    reverse video
		hidden_note   = [ [192,192,192] , [ 96, 96, 96] ] # gray
	,	hidden_ok     = [ [176,208,176] , [ 80,112, 80] ] # greenish gray
	,	note          = [ [  0,  0,255] , [ 64,160,255] ] # blue
	,	ok            = [ [  0,128,  0] , [128,255,128] ] # green
	,	warning       = [ [155,  0,255] , [255,  0,255] ] # magenta
	,	err           = [ [180,  0,  0] , [255, 60, 60] ] # red
	,	speculate_err = [ [220, 80,  0] , [255,128, 50] ] # red
	)
,	trace = pdict(
#		size     = 100<20                                 # overall size of lmakeserver trace
#	,	n_jobs   = 1000                                   # number of kept job traces
#	,	channels = ('backend','default')                  # channels traced in lmakeserver trace
	)
)

class Autodep :
	"""context version of the set_autodep function (applies to this process and all processes started in the protected core)
usage :
	with Autodep(enable) :
		<code with autodep activate(enable=True) or deactivated (enable=False)>
	"""
	def __init__(self,enable) :
		self.cur  = enable
	def __enter__(self) :
		self.prev = get_autodep()
		set_autodep(self.cur)
	def __exit__(self,*args) :
		set_autodep(self.prev)
