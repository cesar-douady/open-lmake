<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Cache

Several cache mechanisms will be implemented but for now, ony one exists.

## DirCache

This cache is based on a shared dir and requires no running daemon.

It must be initialized with a file `LMAKE/size` containing the overall size the cache is allowed to occupy.
The value may end with a unit suffix in `k`, `M`, `G`, `T` (powers of 1024).
For example `LMAKE/size` can contain `1.5T`.

### Permissions

For all users accessing the cache:

- The root dir of the cache and its `LMAKE` directory must have read/write access (including if only download is done to maintain the LRU state).
- The `LMAKE/size` must have read access.

If the group to use for access permission is not the default group of the users:

- the root of the cache must have this group, e.g. with `chgrp -hR <group> <cache_dir>`.
- its setgid bit must be set, e.g. with `chmod g+s <cache_dir> <cache_dir>/LMAKE` (this allows the group to propagate as sub-dirs are created)
- the operation above may have to be done recursively if the cache dir is already populated

To allow the group to have read/write access to all created dirs and files, there are 2 possibilities:

- Best is to the ACL's, e.g. using `setfacl -d -R -m u::rwX,g::rwX,o::- <cache_dir>`
- Altenatively, `lmake.config.caches.<tag>.perm = 'group'` can be set. This is slightly less performant as additional calls to `chmod` are necessary in that case.

Similarly, to allow the cache to be used by any user:

- Best is to the ACL's, e.g. using `setfacl -d -R -m u::rwX,g::rwX,o::rwX <cache_dir>`
- Altenatively, `lmake.config.caches.<tag>.perm = 'other'` can be set. This is slightly less performant as additional calls to `chmod` are necessary in that case.
