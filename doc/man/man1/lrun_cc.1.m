Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

Title(lrun_cc,run a C/C++ compiler protecting include directories)
.SH SYNOPSYS
B(lrun_cc) [-m I(marker)] I(compiler) I(args)...

.SH DESCRIPTION
.LP
B(lrun_cc) runs the program I(compiler) with any given arguments I(args)... .
I(args) are parsed for include or library directories and calls mkdir on these directories and C(ldepend) on I(marker) files within these directories.

The goal is to ensure that these directories always exist as this is a requirement of most C/C++ compiler to try to read include files within them and hence trigger dependencies.

Footer
