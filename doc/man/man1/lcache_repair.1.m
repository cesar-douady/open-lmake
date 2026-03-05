Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023-2026 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

Title(lcache_repair,repair a open-lmake cache)
.SH SYNOPSIS
B_(lcache_repair) [I_(-n)|I_(--dry-run)] [I_(-f)|I_(--force)] I_(dir)

.SH DESCRIPTION
.LP
B_(lcache_repair) is meant to repair a cache.
Its argument specifies the dir to repair.
.LP
This may be usedful because you experience incoherent behaviors and this is less agressive than setting up a fresh dir.
.LP
Also, if for some management reason you want to dismiss some entries, you can remove any part of the cache and run this command to restore a coherent state.
The structure of the cache dir is fairly simple: all the data linked to a job is under a dir named after the job name.
.LP
When running, B_(lcache_repair) first prints what it is about to do before actually proceeding with the actions.

.SH "EXIT STATUS"
.LP
B_(lcache_repair) exits with a status of zero if the cache was successfully repaired.
Else it exits with a non-zero status:
.LP
Item(B_(2))  internal error, should not occur
Item(B_(7))  adequate permissions were missing, typically write access
Item(B_(10)) some syscall failed
Item(B_(11)) bad usage : command line options and arguments coul not be parsed
Item(B_(12)) bad cache version, cache needs to be cleaned

.SH OPTIONS
.LP
Item(B_(-n),B_(--dry-run)) Report actions to carry out but do not actually perform them.
Item(B_(-f),B_(--force)) Dont ask for user confirmation before performing the actions.

ClientGeneralities()

.SH EXAMPLES
.LP
V_(lcache_repair /path/to/cache)

.SH FILES
CommonFiles

Footer
