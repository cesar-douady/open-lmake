<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Cache

Several cache mechanisms will be implemented but for now, ony one exists.

## DirCache

This cache is based on a shared dir and requires no running daemon.

It must be initialized with a file `LMAKE/config.py` containing definitions for :

- `file_sync` (optional) as the method to use to ensure consistent access to the file system containing the cache.
It may be `'none'`, `'dir'` (Default) or `'sync'` (cf. [file_sync](config.html)).
May be overridden by `Lmakefile.lmake.config.caches.<this cache>.file_sync if defined.
- `perm` (optional) as the permissions to use when creating files in the cache, overriding current umask.
It may be `'none'` (default), `'group'` (share cache within user group) or `'other'` (shared cache with everybody)
May be overridden by `Lmakefile.lmake.config.caches.<this cache>.perm if defined.
- `size` (required) as the overall size the cache is allowed to occupy.
It can be an `int` (in bytes) or a `str` in which case value may end with a unit suffix in `k`, `M`, `G`, `T` (powers of 1024).
May not be overridden by `Lmakefile.lmake.config.caches.<this cache>.size if defined.

For example `LMAKE/config.py` can contain `perm='group' ; size=1.5T'`.

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

This can be achieved by setting the `file_sync` variable in `LMAKE/config.py` to :

| Value     | Recommanded for  | Default | Comment                                                                        |
|-----------|------------------|---------|--------------------------------------------------------------------------------|
| `'none'`  | local disk, CEPH |         | no precaution, file system is coherent                                         |
| `'dir'`   | NFS              | X       | enclosing dir (recursively) is open/closed before any read and after any write |
| `'sync'`  |                  |         | `fsync` is called after any modification                                       |
