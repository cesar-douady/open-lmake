Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023-2026 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

Title(lrun_cc,run a C/C++ compiler protecting include dirs)
.SH SYNOPSIS
B_(lrun_cc) [-m I_(marker)] I_(compiler) I_(args)...

.SH "EXIT STATUS"
.LP
B_(lrun_cc) exits with the status of the C_(ldepend) the files described above, C_(lcheck_deps) or the passed command it launches,
as soon as one of such command exits with a non-zero exit code, in that order.

.SH DESCRIPTION
.LP
B_(lrun_cc) runs the program I_(compiler) with any given arguments I_(args)... .
I_(args) are parsed for include or library dirs and calls mkdir on these dirs and C_(ldepend) on I_(marker) files within these dirs.

The goal is to ensure that these dirs always exist as this is a requirement of most C/C++ compiler to try to read include files within them and hence trigger deps.

.SH EXAMPLES
.LP
V_(lrun_cc -mmrkr gcc -Ia -Ib -Ic -c -o prog.o prog.c)

Footer
