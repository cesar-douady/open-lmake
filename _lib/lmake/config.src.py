# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys     as _sys
import os      as _os
import os.path as _osp

try :
	if _sys.version_info.major<3 : from clmake2 import * # if not in an lmake repo, root_dir is not set to current dir
	else                         : from clmake  import * # .
	_has_clmake = True
except :
	_has_clmake = False
	from py_clmake import *

from .utils import *

_mem = _os.sysconf('SC_PHYS_PAGES') * _os.sysconf('SC_PAGE_SIZE')
_tmp = _os.statvfs('.').f_bfree*_os.statvfs('.').f_bsize
if _sys.version_info.major<3 :
	import multiprocessing as _mp
	try                        : _cpu = _mp.cpu_count()
	except NotImplementedError : _cpu = 1
else :
	_cpu = len(_os.sched_getaffinity(0))
#_interface = _socket.getfqdn()
#_group     = _os.getgroups()[0]

config = pdict(
	disk_date_precision = 0.010         # in seconds, precisions of dates on disk, must account for date granularity and date discrepancy between executing hosts and disk servers
,	heartbeat           = 10            # in seconds, minimum interval between 2 heartbeat checks (and before first one) for the same job (no heartbeat if None)
,	heartbeat_tick      =  0.1          # in seconds, minimum internval between 2 heartbeat checks (globally)                             (no heartbeat if None)
,	link_support        = 'Full'        # symlinks are supported. Other values are 'None' (no symlink support) or 'File' (symlink to file only support)
#,	local_admin_dir     = 'LMAKE_LOCAL' # directory in which to store data that are private to the server (not accessed by remote executing hosts) (default is within LMAKE dir)
,	max_dep_depth       = 1000          # used to detect infinite recursions and loops
,	max_error_lines     = 100           # used to limit the number of error lines when not reasonably limited otherwise
,	network_delay       = 1             # delay between job completed and server aware of it. Too low, there may be spurious lost jobs. Too high, tool reactivity may rarely suffer.
,	path_max            = 400           # max path length, but a smaller value makes debugging easier (by default, not activated)
#,	reliable_dirs       = False         # if true, close to open coherence is deemed to encompass enclosing directory coherence (improve performances)
#	                                    # - forced true if only local backend is used
#	                                    # - set   true  for ceph
#	                                    # - leave false for NFS
#,	rules_module        = 'rules'       # module to import to define rules  . By default, rules are directly defined in Lmakefile.py
#,	sources_module      = 'sources'     # module to import to define sources. By default, 'lmake.auto_sources' which lists files in Manifest or searches git (recursively) if lmake.sources is not set
,	sub_prio_boost      = 1             # increment to add to rules defined in sub-repository (multiplied by directory depth of sub-repository) to boost local rules
,	console = pdict(                    # tailor output lines
		date_precision = None           # number of second decimals in the timestamp field
	,	has_exec_time  = True           # if True, output the exec_time field
	,	host_length    = None           # length of the host field (lines will be misaligned if a host is longer)
	,	show_eta       = True           # if True, the title includes the ETA of the lmake command
	)
,	backends = pdict(                                       # PER_BACKEND : provide a default configuration for each backend
		precisions = pdict(                                 # precision of resources allocated for jobs, one entry for each standard resource (for all backends).
			cpu = 8                                         # encodes the highest number with full granularity, 8 is a reasonable value
		,	mem = 8                                         # 8 means possible values are 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 20, ...
		,	tmp = 8                                         # 4 would mean possible values are 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, ...
		)
	,	local = pdict(                                      # entries mention the total availability of resources
			cpu =     _cpu                                  # total number of cpus available for the process, and hence for all jobs launched locally
		,	mem = str(_mem>>20)+'M'                         # total available memory in MBytes, defaults to all available memory
		,	tmp = str(_tmp>>20)+'M'                         # total available temporary disk space in MBytes, defaults to free space in current filesystem
		)
	#,	sge = pdict(
	#		interface         = _interface                  # address at which lmake can be contacted from jobs launched by this backend, can be :
	#		                                                # - ''                     : loop-back address (127.0.0.1) for local backend, hostname for remote backends
	#		                                                # - standard dot notation  : for example '192.168.0.1'
	#		                                                # - network interface name : the address of the host on this interface (as shown by ifconfig)
	#		                                                # - a host name            : the address of the host as found in networkd database (as shown by ping)
	#		                                                # - default is loopback for local backend and hostname for the others
	#	,	bin_dir           = '/opt/sge/bin/ls-amd64'     # directory where sge binaries are located
	#	,	cell              = 'default'                   # cell     used for SGE job submission, by default, SGE automatically determines it
	#	,	cluster           = 'p6444'                     # cluseter used for SGE job submission, by default, SGE automatically determines it
	#	,	default_prio      = 0                           # default priority to use if none is specified on the lmake command line (this is the default)
	#	,	n_max_queued_jobs = 10                          # max number of queued jobs for a given set of asked resources
	#	,	repo_key          = _osp.basename(_os.getcwd()) # prefix used before job name to name slurm jobs (this is the default if not specified)
	#	,	root_dir          = '/opt/sge'                  # root directory of the SGE installation
	#	,	cpu_resource      = 'cpu'                       # resource used to require cpus                 (e.g. qsub -l cpu=1   to require 1 cpu ), not managed if not specified
	#	,	mem_resource      = 'mem'                       # resource used to require memory         in MB (e.g. qsub -l mem=10  to require 10 MB ), not managed if not specified
	#	,	tmp_resource      = 'tmp'                       # resource used to require tmp disk space in MB (e.g. qsub -l tmp=100 to require 100MB ), not managed if not specified
	#	)
	#,	slurm = pdict(
	#		interface         = _interface                  # cf sge entry above
	#	,	config            = '/etc/slurm/slurm.conf'     # config file (this is the default value if not specified)
	#	,	n_max_queued_jobs = 10                          # max number of queued jobs for a given set of asked resources
	#	,	repo_key          = _osp.basename(_os.getcwd()) # prefix used before job name to name slurm jobs
	#	,	use_nice          = True                        # if True (default is False), nice value is used to automatically prioritize jobs between repositories
	#		                                                # requires slurm configuration collaboration
	#	)
	)
,	debug = pdict({
		''  : 'lmake_debug.default'                         # use pdb  as the default debugger
	,	'u' : 'lmake_debug.pudb'                            # use pudb as the default graphic debugger
	,	'n' : 'lmake_debug.none'                            # record n as a key to indicate no debug
	,	'e' : 'lmake_debug.enter'                           # record e as a key to indicate we just want to enter into job view without executing any code
	,	'c' : 'lmake_debug.vscode'
	,	'g' : 'lmake_debug.gdb'
	})
,	caches = pdict(                                         # PER_CACHE : provide an explanation for each cache method
	#	dir = pdict(                                        # when rule specifies cache = 'dir' , this cache is selected
	#		tag    = 'dir'                                  # specify the caching method, must be one of the supported method
	#	,	repo   = root_dir                               # an id that identifies the repository, no more than one entry is stored in the cache for a given job and tag
	#	,	dir    = '/cache_dir'                           # the directory in which cached results are stored
	#	,	size   = 10<<30                                 # the overall size of this cache
	#	,	group  = _group                                 # the group used to write to the cache. If user does not belong to this group, read-only access is still possible
	#	)
	)
,	colors = pdict(
		#                 normal video    reverse video
		hidden_note   = [ [192,192,192] , [ 96, 96, 96] ]   # gray
	,	hidden_ok     = [ [176,208,176] , [ 80,112, 80] ]   # greenish gray
	,	note          = [ [  0,  0,255] , [ 64,160,255] ]   # blue
	,	ok            = [ [  0,128,  0] , [128,255,128] ]   # green
	,	warning       = [ [155,  0,255] , [255,  0,255] ]   # magenta
	,	err           = [ [180,  0,  0] , [255, 60, 60] ]   # red
	,	speculate_err = [ [220, 80,  0] , [255,128, 50] ]   # red
	)
,	trace = pdict(
#		size     = 100<20                                   # overall size of lmakeserver trace
#	,	n_jobs   = 1000                                     # number of kept job traces
#	,	channels = ('backend','default')                    # channels traced in lmakeserver trace
	)
)