Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023-2025 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

Title(lmake_repair,repair a OpenLmake repo)
.SH SYNOPSIS
B(lmake_repair)

.SH DESCRIPTION
.LP
B(lmake_repair) is meant to repair internal OpenLmake book-keeping in case of catastrophic crash, either because of system failure or a bug in OpenLmake.
Most of the time, OpenLmake does not require to resort to heavy reparations as its internal book-keeping is fairly robust, but in some rare cases, this may be necessary.
In such a case, OpenLmake will suggest the use of B(lmake_repair).
.LP
In case of incoherent behavior, the user may decide to run B(lmake_repair).
.LP
This process is pretty long, the goal being to avoid having to restart from a fresh repo, which may incur a severe cost in terms of compute power.
.LP
Once B(lmake_repair) is done, it generates some suggestions about what to do with the freshly repaired repo, including step back and forget about the repair.
.LP
While repairing B(lmake_repair) generates a file I(LMAKE/repaired_jobs) that contains the list of successfully repaired jobs.

ClientGeneralities()

.SH "EXIT STATUS"
.LP
B(lmake_repair) exits with a status of zero if the repo could be successfully repaired.
Else it exits with a non-zero status:
.LP
Item(B(2))  internal error, should not occur
Item(B(3))  I(Lmakefile.py) could not be read or contained an error
Item(B(7))  adequate permissions were missing, typically write access
Item(B(10)) some syscall failed
Item(B(11)) bad usage : command line options and arguments coul not be parsed
Item(B(12)) bad repo version, repo need to be cleaned, e.g. with B(git clean -ffdx)

.SH EXAMPLES
.LP
V(lmake_repair)

.SH FILES
CommonFiles

Footer
