Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023-2025 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

Title(ldircache_repair,repair a OpenLmake repo)
.SH SYNOPSIS
B(ldircache_repair) [I(-n)|I(--dry-run)] I(dir)

.SH DESCRIPTION
.LP
B(ldircache_repair) is meant to repair a dir-based cache (a cache whose tag is I(dir)).
Its argument specifies the dir to repair.
.LP
This may be usedful either because you experience incoherent behaviors and this is less agressive than setting up a fresh dir.
.LP
Also, if for some management reason you want to dismiss some entries, you can remove any part of the cache and run this command to restore a coherent state.
The structure of the cache dir is fairly simple: all the data linked to a job is under a dir named after the job name.
.LP
When running, B(ldircache_repair) generates a trace of its activity.
These may be:
.LP
Item(rm)          Any file not necessary for running the cache is removed.
Item(rmdir)       Any empty dir, possibly resulting from the I(rm) actions, are removed.
Item(erase entry) Any incomplete or otherwise incoherent cache entry is removed.
Item(rebuild lru) The book-keeping information to ensure proper LRU evection is rebuilt.
If the LRU data is missing, the corresponding entry will be deemed to have been accessed and will be preferred candidates for eviction.

.SH OPTIONS
.LP
Item(B(-n),B(--dry-run)) Report actions to carry out but do not actually perform them.

ClientGeneralities()

.SH EXAMPLES
.LP
V(ldircache_repair)

.SH FILES
CommonFiles

Footer
