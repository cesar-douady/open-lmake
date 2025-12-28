Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023-2025 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

Title(lcache_repair,repair a OpenLmake daemon-based cache)
.SH SYNOPSIS
B(lcache_repair) [I(-n)|I(--dry-run)] I(dir)

.SH DESCRIPTION
.LP
B(lcache_repair) is meant to repair a daemon-based cache (a cache whose tag is I(daemon)).
Its argument specifies the dir to repair.
.LP
This may be usedful either because you experience incoherent behaviors and this is less agressive than setting up a fresh dir.
.LP
Also, if for some management reason you want to dismiss some entries, you can remove any part of the cache and run this command to restore a coherent state.
The structure of the cache dir is fairly simple: all the data linked to a job is under a dir named after the job name.
.LP
When running, B(lcache_repair) generates a trace of its activity.
These may be:
.LP
Item(rm) Any file not necessary for running the cache is removed.

.SH "EXIT STATUS"
.LP
B(lcache_repair) exits with a status of zero if the cache was successfully repaired.
Else it exits with a non-zero status:
.LP
Item(B(2))  internal error, should not occur
Item(B(7))  adequate permissions were missing, typically write access
Item(B(10)) some syscall failed
Item(B(11)) bad usage : command line options and arguments coul not be parsed
Item(B(12)) bad cache version, cache needs to be cleaned

.SH OPTIONS
.LP
Item(B(-n),B(--dry-run)) Report actions to carry out but do not actually perform them.

ClientGeneralities()

.SH EXAMPLES
.LP
V(lcache_repair)

.SH FILES
CommonFiles

Footer
