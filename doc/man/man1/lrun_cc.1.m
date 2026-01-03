Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023-2026 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

Title(lrun_cc,run a C/C++ compiler protecting include dirs)
.SH SYNOPSIS
B(lrun_cc) [-m I(marker)] I(compiler) I(args)...

.SH "EXIT STATUS"
.LP
B(lrun_cc) exits with the status of the C(ldepend) the files described above, C(lcheck_deps) or the passed command it launches,
as soon as one of such command exits with a non-zero exit code, in that order.

.SH DESCRIPTION
.LP
B(lrun_cc) runs the program I(compiler) with any given arguments I(args)... .
I(args) are parsed for include or library dirs and calls mkdir on these dirs and C(ldepend) on I(marker) files within these dirs.

The goal is to ensure that these dirs always exist as this is a requirement of most C/C++ compiler to try to read include files within them and hence trigger deps.

.SH EXAMPLES
.LP
V(lrun_cc -mmrkr gcc -Ia -Ib -Ic -c -o prog.o prog.c)

Footer
