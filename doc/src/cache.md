<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Cache

Several cache mechanisms will be implemented but for now, ony one exists.

## DirCache

This cache is based on a shared dir and requires no daemon.

It must be initialized with a file `LMAKE/config.py` defining some variables:

| Variable    | Default      | Overridable | Possible values                                                     | Comment                                                                             |
|-------------|--------------|-------------|---------------------------------------------------------------------|-------------------------------------------------------------------------------------|
| `file_sync` | 'dir'        | yes         | `'none'`, `'dir'`, `'sync'`                                         | the method used to ensure consistent access to the file system containing the cache |
| `perm`      | 'none'       | yes         | `'none'`, `'group'`, `'other'`                                      | the permissions to use when creating files in the cache, overriding current umask   |
| `size`      | \<required\> | no          | any `int` or a `str` composed of a number followed by a unit suffix | the overall size the cache is allowed to occupy                                     |

Overridable variables may be overidden by setting the corresponding attribute in `lmake.config.caches.<this cache>.<attribute>` in `Lmakefile.py`.

Unit suffixes can be `k`, `M`, `G` or `T` (powers of 1024).

For example `LMAKE/config.py` can contain:
```
perm = 'group'
size = '1.5T'
```

### configuration

In `Lmakefile.py`, `lmake.config.caches.<this_cache>` may be set to access a DirCache with the following attributes:

| Attribute   |           | Possible values                     | Description                                                                         |
|-------------|-----------|-------------------------------------|-------------------------------------------------------------------------------------|
| `dir`       | required  | a `str` containing an absolute path | the root dir of the cache                                                           |
| `file_sync` |           | `'none'`, `'dir'`, `'sync'`         | the method used to ensure consistent access to the file system containing the cache |
| `key`       | see below | a `str`                             | an identifier to avoid cache pollution                                              |
| `perm`      |           | `'none'`, `'group'`, `'other'`      | the permissions to use when creating files in the cache, overriding current umask   |

If the same attribute is defined in `LMAKE/config.py` and in the configuration of a repo, the repo overrides the value in `LMAKE/config.py`.

By default, the key is made after the absolute root of the repo and the current git sha1 if repo is controlled by git.
No more than 2 entries can be stored for any job with a given key, the first time and the last time the key is used.

To avoid cache pollution when a repo evolves rapidly (which is typical during active development), the number of stored states must be limited as the cache is typically useless locally.
However, when a repo is pushed, best is that the cache retains the jobs corresponding to what is pushed so that other users can download the results.
Such jobs may have run before or after the commit, hence they are:

- the last job of the previous commit sha1
- or the first job of the current commit sha1

By keeping both, we get a reasonable compromize between pollution avoidance and anticipation of use by other users.


### Permissions

For all users accessing the cache:

- Its root dir and its `LMAKE` dir must have read/write access (including if only download is done to maintain the LRU state).
- The `LMAKE/config.py` must have read access.

If the group to use for access permission is not the default group of the users:

- the root dir must have this group, e.g. with `chgrp -hR <group> <cache_dir>`.
- its setgid bit must be set, e.g. with `chmod g+s <cache_dir> <cache_dir>/LMAKE` (this allows the group to propagate as sub-dirs are created)
- the operation above may have to be done recursively if the cache dir is already populated

To allow the group to have read/write access to all created dirs and files, there are 2 possibilities:

- Best is to use the ACL's, e.g. using `setfacl -d -R -m u::rwX,g::rwX,o::- <cache_dir>`
- Altenatively, `perm = 'group'` can be set in `LMAKE/config.py`. This is slightly less performant as additional calls to `chmod` are necessary in that case.

Similarly, to allow all users to have read/write access to all created dirs and files, there are 2 possibilities:

- Best is to the ACL's, e.g. using `setfacl -d -R -m u::rwX,g::rwX,o::rwX <cache_dir>`
- Altenatively, `perm = 'other'` can be set in `LMAKE/config.py`. This is slightly less performant as additional calls to `chmod` are necessary in that case.

### Coherence

Some file systems, such as NFS, lack coherence.
In that case, precautions must be taken to ensure coherence.

This can be achieved by setting the `file_sync` variable in `LMAKE/config.py` to:

| Value     | Recommanded for  | Default | Comment                                                                        |
|-----------|------------------|---------|--------------------------------------------------------------------------------|
| `'none'`  | local disk, CEPH |         | no precaution, file system is coherent                                         |
| `'dir'`   | NFS              | X       | enclosing dir (recursively) is open before any read and closed after any write |
| `'sync'`  |                  |         | `fsync` is called after any modification                                       |
