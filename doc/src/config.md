<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Config fields

Depending on when each field can be modified, they are said:

- Clean : requires a fresh repo to change this value
- Static : requires that no `lmake` is running to change this value
- Dynamic : field can be changed any time.

The default value is mentioned in ().

### `colors` : Dynamic (reasonably readable)

Open-lmake generate colorized output if it is connected to a terminal (and if it understands the color escape sequences) (cf. [video-mode](video_mode.html)).

This attribute is a `pdict` with one entry for each symbolic color.
Each entry is a 2-tuple of 3-tuple's.
The first 3-tuple provides the color in normal video mode (black/white) and the second one the color in reverse video (white/black).
Each color is a triplet RGB of values between 0 and 255.

### `disk_date_precision` : Static (`0.010`)

This attribute instruct open-lmake to take some margin (expressed in seconds) when it must rely on file dates to decide about event orders.
It must account for file date granularity (generally a few ms) and date discrepancy between executing hosts and disk servers (generally a few ms when using NTP).

- If too low, there are risks that open-lmake consider that data read by a job are up to date while they have been modified shortly after.
- If too high, there is a small impact on performance as open-lmake will consider out of date data that are actually up to date.

The default value should be safe in usual cases and user should hardly need to modify it.

### `domain_name` : Static ('')

Open-lmake needs to know its own network address so that jobs can connect to it when they start.

By default, jobs connect to the fqdn (Fully Qualified Domain Name), e.g. `my_host.my_network.local`. Fqdn is determined by calling `getaddrinfo` (3) and asking for the canonical name.
If this does not provide the adequate information, this field allows to specify a domain name, e.g. `my_network.local`, and jobs will connect to `my_host.<domain_name>` where `my_host` is the
value returned by `gethostname` (2) and `<domain_name>` is the value of this field.

### `file_sync` : Static (`'dir'` if non-local backends are used, else `None`)

This attribute specifies how to ensure file synchronization when a file is produced by a host and read by another one.

Possible values are:

- `'none'` or `None`: the filesystem is deemed reliable an no further protection is needed.
- `'dir'`: the enclosing dir (and recursively up-hill) of a file is closed after any write (or creation or removal) and open before any read.
- `'sync'`: `fsync` is called on file after any write.

Recommanded values for known file systems:

| File system | `file_sync` |
|-------------|-------------|
| NFS         | `'dir'`     |
| CEPH        | `None`      |

The expected performance impact is increasing in this order : `None`, `'dir'`, `'sync'`.
The expected reliability order is the reverse one.

### `debug` : Static

When `ldebug` is used, it consults this `dict`.

It maps debug keys to modules to import to implement the debug method (cf. *ldebug(1)*).
Values contain the module name optionnaly followed by a human description (that will appear with `ldebug -h`) separated with spaces.

### `heartbeat` : Static (`10`)

Open-lmake has a heartbeat mechanism to ensure a job does not suddenly disappear (for example if killed by the user, or if a remote host reboots).
If such an event occurs, the job will be restarted automatically.

This attribute specifies the minimum time between 2 successive checks for a given job.
If `None` (discouraged), the heartbeat mechanism is disabled.

The default value should suit the needs of most users.

- If too low, build performance will decrease as heartbeat will take significative resources
- If too high, reactivity in case of job loss will decrease

### `heartbeat_tick` : Static (`0.1`)

Open-lmake has a heartbeat mechanism to ensure a job does not suddenly disappear (for example if killed by the user, or if a remote host reboots).
If such an event occurs, the job will be restarted automatically.

This attribute specifies the minnimum time between 2 successive checks globally for all jobs.
If `None` (discouraged), it is equivalent to 0.

The default value should suit the needs of most users.

- If too low, build performance will decrease as heartbeat will take significative resources
- If too high, reactivity in case of job loss will decrease

### `local_admin_dir` : Clean (-)

This variable contains a dir to be used for open-lmake administration in addition to the `LMAKE` dir.

It is guaranteed that all such accesses are performed by the host, hence a dir in a locally mounted disk is fine.

- If unset, administration by user is simplified (no need to manage an external dir), but there may be a performance impact as network file systems are generally slower than local ones.
- If set to a local dir, user has to ensure that `lmake` and other commands are always launched from the host that has this locaol file system.
- If set to network dir, there is no performance gain and only added complexity.

### `link_support` : Clean (`'full'`)

Open-lmake fully handle symbolic links (cf. [data model](data_model.html)).

However, there is an associated cost which may be useless in some situations.

| Value    | Support level                                          |
|----------|--------------------------------------------------------|
| `'full'` | symbolic links are fully supported                     |
| `'file'` | symbolic links are only supported if pointing to files |
| `'none'` | symbolic links are not supported                       |

### `max_dep_depth`: Static (`100`)

The [rule selection](rule_selection.hml) process is a recursive one.
It is subject to infinite recursion and several means are provided to avoid it.

The search stops if the depth of the search reaches the value of this attribute, leading to the selection of a special internal rule called `infinite`.

- If too low, some legal targets may be considered infinite.
- If too high, the error message in case of infinite recursion will be more verbose.

### `max_error_lines` : Dynamic (`100`)

When a lot of error lines are generated by open-lmake, other than copying the `stderr` of a job, only the first `max_error_lines` ones are actually output,
followed by a line containing `...` if some lines have been suppressed.
The purpose is to ease reading.

### `network_delay` : Static (`1`)

This attribute provides an approximate upper bound of the time it takes for an event to travel from a host to another.

- If too low, there may be spurious lost jobs.
- If too high, there may be a loss of reactivity.

The default value should fit most cases.

### `nice` : Dynamic (`0`)

This attribute provides the nice value to apply to all jobs.
It is a value between 0 and 20 that decreases the priority of jobs (cf. *nice(2)*).

If available, the autogroup mecanism (cf. *sched(7)*) is used instead as jobs are launched as sessions.

Note that negative nice values are not supported as these require privileges.

### `path_max` : Static (`200`)

The [rule selection](rule_selection.html) process is a recursive one.
It is subject to infinite recursion and several means are provided to avoid it.

The search stops if any file with a name longer than the value of this attribute, leading to the selection of a special internal rule called `infinite`.

### `sub_repos` : Static (`()`)

This attribute provide the list of sub-repos.

Sub repos are sub-dirs of the repo that are themselves repos, i.e. they have a `Lmakefile.py`.
Inside such sub-repos, the applied flow is the one described in it (cf. [Subrepos](experimental_subrepos.html)).

### `system_tag` : Static (see below)

This attribute provide a way to identify hosts by category to ensure proper config reload.

Some configuration elements may vary from one host to another.
To ensure config is reloaded when elements change, this attribute, which must be function, is run and if its result is not the same as last time, config is reloaded.

By default, the `hostname` is returned, so config is reloaded as soon as the open-lmake server is launched on a different host.

### `backends` : Dynamic

This attribute is a `pdict` with one entry for each active backend (cf. [backends](backends.html)).

Each entry is a `pdict` providing resources. Such resources are backend specific.

#### `backends.*.environ` : Dynamic (`{}`)

Environment to pass when launching job in backend.
This environment is accessed when the value mentioned in the rule is `...`.

#### `backends.local.cpu` : Dynamic (number of phyical CPU's)

This is a normal resource that rules can require (by default, rule require 1 cpu).

#### `backends.local.mem` : Dynamic (size of physical memory in MB)

This is the pysical memory necessary for jobs.
It can be specified as a number or a string representing a number followed by a standard suffix such as `k`,  `M` or `G`.
Internally, the granularity is forced to MB.

#### `backends.local.tmp` : Dynamic (`0`)

This is the disk size in the temporary dir necessary for jobs.
It can be specified as a number or a string representing a number followed by a standard suffix such as `k`,  `M` or `G`.
Internally, the granularity is forced to MB.

### `caches` : Static

This attribute is a `pdict` with one entry for each cache.

Caches are named with an arbitrary `str` and are referenced in rules using this name.

By default, no cache is configured, but an example can be found in [lib/lmake/config_.py](../../lib/lmake/config_.py), commented out.

#### `caches.*.tag` : Static (-)

This attribute specifies the method used by open-lmake to cache values.
In the current version, only 2 tags may be used:

- `none` is a fake cache that cache nothing.
- `dir` is a cache working without daemon, data are stored in a dir.

#### `caches.<dir>.dir` : Static

Only valid if `caches.<dir>.tag=='dir'`

This attribute specifies the dir in which the cache puts its data.

The dir must pre-exist and contain a file `LMAKE/size` containing the size the cache may occupy on disk.
The size may be suffixed by a unit suffix (`k`, `M`, `G`, `T`, `P` or `E`). These refer to base 1024.

Also, an adequate default ACL (cf. *acl(5)*) must most probably be set for this dir to give adequate permissions to files created in it.
Typically, the command `setfacl -m d:g::rw,d:o::r CACHE` can be used to set up the dir `CACHE`.

#### `caches.<dir>.file_sync` : Static (`'dir'`)

Only valid if `caches.<dir>.tag=='dir'`

Same meaning as `config.file_sync` for accesses in the cache.

#### `caches.<dir>.key` : Static (repo root dir/git sha1)

Only valid if `caches.<dir>.tag=='dir'`

A key used to avoid cache pollution.
No more than a single entry can be stored for any job with a given key.

By default, it is made after the absolute root dir of the repo and the current git sha1 if repo is controlled by git.

#### `caches.<dir>.perm` : Static (dont share)

Only valid if `caches.<dir>.tag=='dir'`

This attribute specifies where this cache can be shared.
Values can be:

- `none`: cache is not share beyond permissions provided by `umask` and ACL. To access the cache, a user must have write permission to it, even to download.
- `group`: cache is share with the group, even if restricted by `umask` or ACL.
- `other`: cache is share with everybody, even if restricted by `umask` or ACL.

It is usual to use this attribute when ACL are not used as most of the time, the cache wants to be share among several users and `umask` forbids it.

On the contrary, when ACL are used, it is better to allow read/write accesses to the cache dir as this is more performant than relying on this attribute.

### `collect` : Dynamic

This attributes specifies files and dirs to be ignored (and hence kept) when `lcollect` is run.
Files are specified as in rule targets : with stems and patterns given with a syntax similar to python f-strings.

By default, no files are ignored when `lcollect` is run.

#### `collect.stems` : Dynamic

This attributes provides a `dict` as the `stems` `Rule` attribute.

#### `collect.ignore` : Dynamic

This attributes provides a `dict` as the `targets` `Rule` attribute.
However, contrarily to `Rule`s, several targets can be provided as a `list`/`tuple` for each key, and no flags can be passed in.

### `console` : Dynamic

This is a sub-configuration for all attributes pertaining to the console output of `lmake`.

#### `console.date_precision` : Dynamic (`None`)

This attribute specifies the precision (as the number of digit after the second field, for example 3 means we see milli-seconds) with which timestamps are generated on the console output.
If `None`, no timestamp is generated.

#### `console.has_exec_time` : Dynamic (`True`)

If this attribute is true, execution time is reported each time a job is completed.

#### `console.history_days` : Dynamic (`7`)

This attribute specifies the number of days the output log history is kept in the `LMAKE/outputs` dir.

#### `console.host_len` : Dynamic (`None`)

This attribute specifies the width of the field showing the host that executed or is about to execute the job.
If `None`, the host is not shown.
Note that no host is shown for local execution.

#### `console.show_eta` : Dynamic (`False`)

If this attribute is true, the title shows the ETA of the command, in addition to statistics about number of jobs.

#### `console.show_ete` : Dynamic (`True`)

If this attribute is true, the title shows the ETE of the command, in addition to statistics about number of jobs.

### `trace` : Dynamic

This is a sub-configuration for all attributes pertaining to the optional tracing facility of open-lmake.

For tracing to be active, it must be compiled in (cf. INSTALLATION), which is off by default as performances can be severly degraded.

#### `trace.size` : Static (`100_000_000`)

While open-lmake runs, it may generate an execution trace recording a lot of internal events meant for debugging purpose.

The trace is handled as a ring buffer, storing only the last events when the size overflows.
The larger the trace, the more probable the root cause of a potential problem is still recorded, but the more space it takes on disk.

This attributes contains the maximum size this trace can hold (open-lmake keeps the 5 last traces in case the root cause lies in a previous run).

#### `trace.n_jobs` : Static (`1000`)

While open-lmake runs, it generates execution traces for all jobs.

This attributes contains the overall number of such traces that are kept.

#### `trace.channels` : Static (all)

The execution trace @lmake generates is split into channels to better control what to trace.

This attributes contains a `list` or `tuple` of the channels to trace.
