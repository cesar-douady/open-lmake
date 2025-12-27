<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Config fields

Depending on when each field can be modified, they are said:

| Tag     | Description                                                                                                            |
|---------|------------------------------------------------------------------------------------------------------------------------|
| Clean   | Requires a fresh repo to change this value.                                                                            |
| Static  | Requires that no `lmake` is running to change this value.                                                              |
| Dynamic | Field can be changed any time, even while lmake is running, it will be taken into account at the next `lmake` command. |

The default value is mentioned in ().

### [`backends`](unit_tests/slurm.html#:~:text=lmake%2Econfig%2Ebackends%2Eslurm%20%3D%20%7B%20%27environ%27%20%3A%20%7B%20%27DUT%27%3A%27dut%27%20%7D%20%7D) : Dynamic

This attribute is a [`pdict`](lmake_module.html#:~:text=class%20pdict) with one entry for each active [backend](backends.html).

Each entry is a `pdict` providing resources.
Such resources are backend specific.

### [`backends.<backend>.domain_name`](lib/lmake/config_.html#:~:text=%23%20%2C%20domain%5Fname%20%3D%20socket%2Egetfqdn%28%29%2Esplit%28%27%2E%27%2C1%29%5B%2D1%5D%20%23%20the%20domain%20name%20to%20host%20name%20to%20form%20the%20fqdn%20under%20which%20jobs%20must%20contact%20server,-%23%20%2C%20config) : Static (None)

Open-lmake needs to know its own network address so that jobs can connect to it when they start.

By default, jobs connect to the fqdn (Fully Qualified Domain Name), e.g. `my_host.my_network.local`.
Fqdn is determined by calling [getaddrinfo(3)](https://man7.org/linux/man-pages/man3/getaddrinfo.3.html) and asking for the canonical name.
If this does not provide the adequate information, this field allows to specify a domain name, e.g. `my_network.local`, and jobs will connect to `<my_host>.<domain_name>` where `<my_host>` is the
value returned by [gethostname(2)](https://man7.org/linux/man-pages/man2/gethostname.2.html) and `<domain_name>` is the value of this field.

#### [`backends.<backend>.environ`](unit_tests/slurm.html#:~:text=%27environ%27%20%3A%20%7B%20%27DUT%27%3A%27dut%27%20%7D) : Dynamic (`{}`)

Environment to pass when launching job in backend.
This environment is accessed when [the value mentioned in the rule is `...`](unit_tests/slurm.html#:~:text=environ%20%3D%20%7B%20%27DUT%27%3A%2E%2E%2E%20%7D).

#### [`backends.local.cpu`](unit_tests/stress_depend.html#:~:text=lmake%2Econfig%2Ebackends%2Elocal%2Ecpu%20%3D%201000%20%23%20a%20unreasonable%20but%20stressing%20value) : Dynamic (number of physical CPU's)

This is a normal resource that rules can require (by default, rules require 1 cpu).

#### [`backends.local.mem`](unit_tests/jobs.html#:~:text=lmake%2Econfig%2Ebackends%2Elocal%2Emem%20%3D%20f%27%7Bn%5Fjobs%2A2%7DM) : Dynamic (size of physical memory)

This is the physical memory necessary for each job.
It can be specified as a `int` or a `str` representing a number followed by a [standard suffix](https://en.wikipedia.org/wiki/Metric_prefix) (but uses powers of 1024).

Internally, the granularity is forced to 1MB.

#### `backends.local.tmp` : Dynamic (`0`)

This is the disk size in the temporary dir necessary for each job.
It can be specified as a `int` or a `str` representing a number followed by a [standard suffix](https://en.wikipedia.org/wiki/Metric_prefix) (but uses powers of 1024).

Internally, the granularity is forced to MB.

### [`caches`](unit_tests/cache.html#:~:text=lmake%2Econfig%2Ecaches%2Emy%5Fcache%20%3D%20%7B%20%27tag%27%20%3A%20%27dir%27%20%2C%20%27dir%27%20%3A%20lmake%2Erepo%5Froot%2B%27%2FCACHE%27%20%2C%20%27perm%27%20%3A%20%27group%27%20%7D) : Static

This attribute is a [`pdict`](lmake_module.html#:~:text=class%20pdict) with one entry for each cache.

Caches are named with an arbitrary `str` and are referenced in rules [using this name](unit_tests/cache.html#:~:text=cache%20%3D%20%27my%5Fcache).

By default, no cache is configured.

#### [`caches.*.tag`](unit_tests/cache.html#:~:text=%27tag%27%20%3A%20%27dir) : Static (-)

This attribute specifies the method used by open-lmake to cache values.

| Tag                                          | Default  | Description                                                                                         |
|----------------------------------------------|----------|-----------------------------------------------------------------------------------------------------|
| `'none'`                                     |          | fake cache that caches nothing                                                                      |
| [`'dir'`](cache.html#:~:text=DirCache)       |          | [a cache working without daemon, data are stored in a shared dir](cache.html#:~:text=configuration) |
| [`'daemon'`](cache.html#:~:text=DaemonCache) | X        | [a cache working with a daemon, data are stored in a shared dir](cache.html#:~:text=configuration)  |

### [`collect`](unit_tests/collect.html#:~:text=lmake%2Econfig%2Ecollect%2Estems%20%3D%20%7B%20%27SFX%27%20%3A%20r%27%5B%5Cw%2F%5D%2B%27%20%7D%20lmake%2Econfig%2Ecollect%2Eignore%20%3D%20%7B%20%27TOK%27%20%3A%20%28%20%27tok%2Eout%27%20%2C%20%27tok%2Eerr%27%20%29%20%2C%20%27COLLECT%27%20%3A%20%27dont%5Fcollect%7BSFX%7D%27%20%7D) : Dynamic

This attributes specifies files and dirs to be ignored (and hence kept) when `lcollect` is run.
Files are specified as in rule targets : with stems and patterns given with a syntax similar to python f-strings.

By default, no files are ignored when `lcollect` is run.

#### [`collect.stems`](unit_tests/collect.html#:~:text=lmake%2Econfig%2Ecollect%2Estems%20%3D%20%7B%20%27SFX%27%20%3A%20r%27%5B%5Cw%2F%5D%2B%27%20%7D) : Dynamic

This attributes provides a `dict` as the `stems` `Rule` attribute.

#### [`collect.ignore`](unit_tests/collect.html#:~:text=lmake%2Econfig%2Ecollect%2Eignore%20%3D%20%7B%20%27TOK%27%20%3A%20%28%20%27tok%2Eout%27%20%2C%20%27tok%2Eerr%27%20%29%20%2C%20%27COLLECT%27%20%3A%20%27dont%5Fcollect%7BSFX%7D%27%20%7D) : Dynamic

This attributes provides a `dict` as the `targets` `Rule` attribute.
However, contrarily to `Rule`s, several targets can be provided as a `list`/`tuple` for each key, and no flags can be passed in.

### [`colors`](lib/lmake/config_.html#:~:text=%2C%20colors,50%5D%20%5D%20%23%20red%20%29) : Dynamic (reasonably readable)

Open-lmake generate colorized output if it is connected to a terminal (and if it understands the color escape sequences) (cf. [video-mode](video_mode.html)).

This attribute is a `pdict` with one entry for each symbolic color.
Each entry is a 2-tuple of 3-tuple's.
The first 3-tuple provides the color in normal video mode (black/white) and the second one the color in reverse video (white/black).
Each color is a triplet RGB of values between 0 and 255.

### [`console`](lib/lmake/config_.html#:~:text=%2C%20console,command) : Dynamic

This is a sub-configuration for all attributes pertaining to the console output of `lmake`.

#### [`console.date_precision`](unit_tests/dyn_config.html#:~:text=lmake%2Econfig%2Econsole%2Edate%5Fprecision%20%3D%203) : Dynamic (`None`)

This attribute specifies the precision (as the number of digit after the second field, for example 3 means we see milli-seconds) with which timestamps are generated on the console output.
If `None`, no timestamp is generated.

#### `console.has_exec_time` : Dynamic (`True`)

If this attribute is true, execution time is reported each time a job is completed.

#### `console.history_days` : Dynamic (`7`)

This attribute specifies the number of days the output log history is kept in the `LMAKE/outputs` dir.

#### [`console.host_len`](unit_tests/slurm.html#:~:text=lmake%2Econfig%2Econsole%2Ehost%5Flen%20%3D%20host%5Flen) : Dynamic (`None`)

This attribute specifies the width of the field showing the host that executed or is about to execute the job.
If `None`, the host is not shown.
Note that no host is shown for local execution.

#### `console.show_eta` : Dynamic (`False`)

If this attribute is true, the title shows the ETA of the command, in addition to statistics about number of jobs.

#### `console.show_ete` : Dynamic (`True`)

If this attribute is true, the title shows the ETE of the command, in addition to statistics about number of jobs.

### [`debug`](lib/lmake/config_.html#:~:text=%2C%20debug,gdb%29%27%20%7D%29) : Static

When `ldebug` is used, it consults this `dict`.

It maps debug keys to modules to import to implement the debug method (cf. [ldebug(1)](man/man1/ldebug.html)).
Values contain the module name optionnaly followed by a human description (that will appear with `ldebug -h`) separated with spaces.

### [`disk_date_precision`](unit_tests/hot.html#:~:text=lmake%2Econfig%2Edisk%5Fdate%5Fprecision%20%3D%202) : Static (`0.010`)

This attribute instruct open-lmake to take some margin (expressed in seconds) when it must rely on file dates to decide about event orders.
It must account for file date granularity (generally a few ms) and date discrepancy between executing hosts and disk servers (generally a few ms when using NTP).

- If too low, there are risks that open-lmake consider that data read by a job are up to date while they have been modified shortly after.
- If too high, there is a small impact on performance as open-lmake will consider out of date data that are actually up to date.

The default value should be safe in usual cases and user should hardly need to modify it.

### [`file_sync`](lib/lmake/config_.html#:~:text=%2C%20file%5Fsync%20%3D%20%27dir%27%20%23%20method%20used%20to%20ensure%20real%20close%2Dto%2Dopen%20file%20synchronization%20%3A) : Static (`'dir'` if non-local backends are used, else `None`)

This attribute specifies how to ensure file synchronization when a file is produced by a host and read by another one.

Possible values are (ordered by decreasing performance):

| Value     | Recommanded for  | Default               | Comment                                                                                           |
|-----------|------------------|-----------------------|---------------------------------------------------------------------------------------------------|
| `'none'`  | local disk, CEPH | if no remote backends | no precaution, file system is coherent                                                            |
| `'dir'`   | NFS              | if any remote backend | enclosing dir (recursively) is open before any read and closed after any write                    |
| `'sync'`  |                  |                       | [`fsync`(2)](https://man7.org/linux/man-pages/man2/fsync.2.html) is called after any modification |

### [`heartbeat`](lib/lmake/config_.html#:~:text=%2C%20heartbeat%20%3D%2010%20%23%20in%20seconds%2C%20minimum%20interval%20between%202%20heartbeat%20checks%20%28and%20before%20first%20one%29%20for%20the%20same%20job%20%28no%20heartbeat%20if%20None) : Static (`10`)

Open-lmake has a heartbeat mechanism to ensure a job does not suddenly disappear (for example if killed by the user, or if a remote host reboots).
If such an event occurs, the job will be restarted automatically.

This attribute specifies the minimum time between 2 successive checks for a given job.
If `None` (discouraged), the heartbeat mechanism is disabled.

The default value should suit the needs of most users.

- If too low, build performance will decrease as heartbeat will take significative resources
- If too high, reactivity in case of job loss will decrease

### [`heartbeat_tick`](lib/lmake/config_.html#:~:text=%2C%20heartbeat%5Ftick%20%3D%200%2E1%20%23%20in%20seconds%2C%20minimum%20internval%20between%202%20heartbeat%20checks%20%28globally%29%20%28no%20heartbeat%20if%20None) : Static (`0.1`)

Open-lmake has a heartbeat mechanism to ensure a job does not suddenly disappear (for example if killed by the user, or if a remote host reboots).
If such an event occurs, the job will be restarted automatically.

This attribute specifies the minnimum time between 2 successive checks globally for all jobs.
If `None` (discouraged), it is equivalent to 0.

The default value should suit the needs of most users.

- If too low, build performance will decrease as heartbeat will take significative resources
- If too high, reactivity in case of job loss will decrease

### [`link_support`](unit_tests/depend.html#:~:text=lmake%2Econfig%2Elink%5Fsupport%20%3D%20step%2Elink%5Fsupport) : Clean (`'full'`)

Open-lmake fully handle symbolic links (cf. [data model](data_model.html)).

However, there is an associated cost which may be useless in some situations.

| Value    | Support level                                          |
|----------|--------------------------------------------------------|
| `'full'` | symbolic links are fully supported                     |
| `'file'` | symbolic links are only supported if pointing to files |
| `'none'` | symbolic links are not supported                       |

### [`local_admin_dir`](unit_tests/admin.html#:~:text=lmake%2Econfig%2Elocal%5Fadmin%5Fdir%20%3D%20%27LMAKE%5FLOCAL%27%20%23%20declared%20within%20repo%20for%20test%20ease%20of%20use%2C%20but%20goal%20is%20to%20make%20it%20absolute%20in%20a%20fast%20local%20disk) : Clean (-)

This variable contains a dir to be used for open-lmake administration in addition to the `LMAKE` dir.

It is guaranteed that all such accesses are performed by the host, hence a dir in a locally mounted disk is fine.

- If unset, administration by user is simplified (no need to manage an external dir), but there may be a performance impact as network file systems are generally slower than local ones.
- If set to a local dir, user has to ensure that `lmake` and other commands are always launched from the host that has this locaol file system.
- If set to network dir, there is no performance gain and only added complexity.

### [`max_dep_depth`](unit_tests/infinite.html#:~:text=lmake%2Econfig%2Emax%5Fdep%5Fdepth%20%3D%2010): Static (`100`)

The [rule selection](rule_selection.hml) process is a recursive one.
It is subject to infinite recursion and several means are provided to avoid it.

The search stops if the depth of the search reaches the value of this attribute, leading to the selection of a special internal rule called `infinite`.

- If too low, some legal targets may be considered infinite.
- If too high, the error message in case of infinite recursion will be more verbose.

### [`max_error_lines`](lib/lmake/config_.html#:~:text=%2C%20max%5Ferror%5Flines%20%3D%20100%20%23%20used%20to%20limit%20the%20number%20of%20error%20lines%20when%20not%20reasonably%20limited%20otherwise) : Dynamic (`100`)

When a lot of error lines are generated by open-lmake, other than copying the `stderr` of a job, only the first `max_error_lines` ones are actually output,
followed by a line containing `...` if some lines have been suppressed.
The purpose is to ease reading.

### [`network_delay`](unit_tests/stress_depend.html#:~:text=lmake%2Econfig%2Enetwork%5Fdelay%20%3D%2010%20%23%20under%20heavy%20load%2C%20delays%20can%20grow%20up) : Static (`1`)

This attribute provides an approximate upper bound of the time it takes for an event to travel from a host to another.

- If too low, there may be spurious lost jobs.
- If too high, there may be a loss of reactivity.

The default value should fit most cases.

### [`nice`](lib/lmake/config_.html#:~:text=%23%2C%20nice%20%3D%200%20%23%20nice%20value%20to%20apply%20to%20all%20jobs) : Dynamic (`0`)

This attribute provides the nice value to apply to all jobs.
It is a value between 0 and 20 that decreases the priority of jobs (cf. [nice (2)](https://man7.org/linux/man-pages/man2/nice.2.html)).

If available, the autogroup mecanism (cf. [sched(7)](https://man7.org/linux/man-pages/man7/sched.7.html#:~:text=2%29%2E-,The%20autogroup%20feature)) is used instead as jobs are launched as sessions.

Note that negative nice values are not supported as these require privileges.

### [`path_max`](unit_tests/path_max.html#:~:text=lmake%2Econfig%2Epath%5Fmax%20%3D%20path%5Fmax) : Static (`200`)

The [rule selection](rule_selection.html) process is a recursive one.
It is subject to infinite recursion and several means are provided to avoid it.

The search stops if any file with a name longer than the value of this attribute, leading to the selection of a special internal rule called `infinite`.

### [`sub_repos`](unit_tests/sub_repos.html#:~:text=lmake%2Econfig%2Esub%5Frepos%20%3D%20%28%27a%27%2C%27b%27%29%20%23%20for%20top%20level%20only%2C%20overwritten%20in%20sub%2Drepos) : Static (`()`)

This attribute provide the list of sub-repos.

Sub repos are sub-dirs of the repo that are themselves repos, i.e. they have a `Lmakefile.py`.
Inside such sub-repos, the applied flow is the one described in it (cf. [Subrepos](experimental_subrepos.html)).

### [`system_tag`](lib/lmake/config_.html#:~:text=%2C%20system%5Ftag%20%3D%20%5Fsystem%5Ftag%20%23%20force%20config%20re%2Dread%20if%20the%20result%20of%20this%20function%20changes) : Static (see below)

This attribute provide a way to identify hosts by category to ensure proper config reload.

Some configuration elements may vary from one host to another.
To ensure config is reloaded when elements change, this attribute, which must be function, is run and if its result is not the same as last time, config is reloaded.

By default, the `hostname` is returned, so config is reloaded as soon as the open-lmake server is launched on a different host.

### [`trace`](lib/lmake/config_.html#:~:text=%2C%20trace%20%3D%20pdict%28%20%23%20size%20%3D%20100%3C%3C20%20%23%20overall%20size%20of%20lmake_server%20trace%20%23%20%2C%20n%5Fjobs%20%3D%201000%20%23%20number%20of%20kept%20job%20traces%20%23%20%2C%20channels%20%3D%20%28%27backend%27%2C%27default%27%29%20%23%20channels%20traced%20in%20lmake_server%20trace%20%29) : Dynamic

This is a sub-configuration for all attributes pertaining to the optional tracing facility of open-lmake.

For tracing to be active, it must be [compiled in](install.html#:~:text=T%20controls%20%2DDTRACE%20%28default%3A%20traces%20are%20disabled%20%29),
which is off by default as performances can be severly degraded.

#### [`trace.channels`](unit_tests/cache.html#:~:text=lmake%2Econfig%2Etrace%2Echannels%20%3D%20%28%27cache%27%2C%29) : Static (all)

The execution trace open-lmake generates is split into channels to better control what to trace.

This attributes contains a `list` or `tuple` of the channels to trace.

#### [`trace.n_jobs`](unit_tests/stress_codec.html#:~:text=lmake%2Econfig%2Etrace%2En%5Fjobs%20%3D%2020000%20%23%20ensure%20we%20keep%20all%20traces%20for%20analysis) : Static (`1000`)

While open-lmake runs, it generates execution traces for all jobs.

This attributes contains the overall number of such traces that are kept.

#### [`trace.size`](unit_tests/stress_codec.html#:~:text=lmake%2Econfig%2Etrace%2Esize%20%3D%201%3C%3C30) : Static (`'100M'`)

While open-lmake runs, it may generate an execution trace recording a lot of internal events meant for debugging purpose of itself (not for user usage).
Interpretation of the trace must be done in conjunction of open-lmake sources.

The trace is handled as a ring buffer, storing only the last events when the size overflows.
The larger the trace, the more probable the root cause of a potential problem is still recorded, but the more space it takes on disk.

This attributes contains the maximum size this trace can hold (open-lmake keeps the 5 last traces in case the root cause lies in a previous run).
