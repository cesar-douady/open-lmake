<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2026 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Cache

The cache is based on a shared dir controlled by a daemon.
The daemon is started automatically when needed and dies as soon as it becomes useless.
It is also possible to run it manually with the `lcache_server` command.

It must be initialized with a file `LMAKE/config.py` defining some variables:

| Variable           | Default      | Possible values                                                            | Comment                                                                             |
|--------------------|--------------|----------------------------------------------------------------------------|-------------------------------------------------------------------------------------|
| `file_sync`        | 'dir'        | `'none'`, `'dir'`, `'sync'`                                                | the method used to ensure consistent access to the file system containing the cache |
| `max_rate`         | '1G'         | any positive `int` or `str` composed of a number followed by a unit suffix | the maximum rate in B/s above which entries are not recorded in the cache           |
| `max_runs_per_job` | 100          | any positive `int`                                                         | the maximum number of runs kept for a given job                                     |
| `size`             | \<required\> | any `int` or a `str` composed of a number followed by a unit suffix        | the overall size the cache is allowed to occupy                                     |

Unit suffixes can be `k`, `M`, `G` or `T` (powers of 1024).

For example `LMAKE/config.py` can contain:
```
max_rate = '100M'
size     = '1.5T'
```

## configuration

In `Lmakefile.py`, `lmake.config.caches.<this_cache>` may be set to access a cache with the following attributes:

| Attribute   |           | Possible values                     | Description                            |
|-------------|-----------|-------------------------------------|----------------------------------------|
| `dir`       | required  | a `str` containing an absolute path | the root dir of the cache              |
| `repo_key`  | see below | a `str`                             | an identifier to avoid cache pollution |

By default, the `repo_key` is made after the absolute root of the repo and the current git sha1 if repo is controlled by git.
No more than 2 entries can be stored for any job with a given `repo_key`, the first time and the last time the `repo_key` is used.

## execution

A job is defined as a particular instance of a rule characterized by the rule and the non-star stems.
For example, if a rule produces has `target = {File1}+{File2}`, a job may be characterized by the rule and `File1`=`hello` and `File2`=`world` leading to target `hello+world`.

A run is defined as a particular instance of a job characterized by the content of the deps.
For example, for the example job above, a particular run would be if file `hello` contains `hi` and file `world` contains `John` if these files are the (only) deps.

The cache caches results (i.e. targets and all associated meta-data) of runs.
Runs are uploaded when executed in a repo and the rule mentions it in its `cache` attribute (and upload is autorized).

Because the cache has a maximum size, after an intial phase during which it fills up, it must make room each time a new run must be stored.
Hence, there must be a cache replacement policy.

### cache replacement policy

There are several factors that are taken into account when deciding which run to victimize from the cache to make room.

#### 1/ At most 2 runs are kept from a given repo a given commit (e.g. the git commit hash).

To avoid cache pollution in case a repo evolves rapidly (which is typical during active development), the number of stored runs from this repo must be limited
as the cache is typically useless locally.
However, when a repo is pushed (using e.g. `git push`), best is that the cache retains the runs corresponding to what is pushed so that other users can download the results.
Such runs may have been executed before or after the commit (using e.g. `git commit`), hence they are:

- the last job of the previous commit sha1
- or the first job of the current commit sha1

By keeping both, we get a reasonable compromize between pollution avoidance and anticipation of use by other users.

#### 2/ At most `max_runs_per_job` runs are kept for a given job.

The reason is two-fold.

First, if there are, say, 50 users using the cache, there is not real reason to keep more than 100 runs for a given job.
If more than this many runs have been uploaded, Probably that the older ones are now useless.

Second, the cache must frequently walk through all the runs of a given job (a direct key look-up cannot be used as deps are not always known when the cache is consulted for download).
Limiting the maximum number of runs for a given job guarantees that the cache will keep a high performance level.

#### 3/ A special LRU mechanism is used

While LRU (Least Recently Used) is known to provide a good heuristic for cache replacement policy, it must be taken into account that the cost of computing a given run varies from one another.
For example is a user spent 1 hour to compute 1MB of data for a given run and another spent 2 hours to compute a result of the same size, it is wise make efforts to keep the second one longer
as this MB will avoid a higher cost in case it is hit.

The special LRU used in the cache ressembles a classical LRU except that the aging speed is proportional to the ratio between size and cpu time to generate it.
This is a natural extension of the classical LRU algorithm devised for cases where size and cost are fixed for all cache entries.

## Permissions

For all users accessing the cache:

- All dirs must have read/write/execute accesses.
- The `LMAKE/config.py` must have read access.

And if such acceses are at group level (but not other), all dirs must have the setgid bit set.

When the group has read/write/execute access, best (performance wise) is to set the default ACL, e.g. using:

`setfacl -d -R -m u::rwX,g::rwX,o::- <cache_dir>`

Similarly, when all users have read/write/execute access, best (performance wise) is to set the default ACL, e.g. using:

`setfacl -d -R -m u::rwX,g::rwX,o::rwX <cache_dir>`

## Coherence

Some file systems, such as NFS, lack coherence.
In that case, precautions must be taken to ensure coherence.

This can be achieved by setting the `file_sync` variable in `LMAKE/config.py` to:

| Value     | Recommanded for  | Default | Comment                                                                        |
|-----------|------------------|---------|--------------------------------------------------------------------------------|
| `'none'`  | local disk, CEPH |         | no precaution, file system is coherent                                         |
| `'dir'`   | NFS              | X       | enclosing dir (recursively) is open before any read and closed after any write |
| `'sync'`  |                  |         | `fsync` is called after any modification                                       |
