Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

Title(find_cc_ld_library_path,compute an adequate value for $LD_LIBRARY_PATH)
.SH SYNOPSYS
B(find_ld_library_path) I(compiler)

.SH DESCRIPTION
.LP
When compiling with a customized C/C++ compiler, the generated executable must run with a related B($LD_LIBRARY_PATH) value to have access to the builtin libraries.
This command computes an adequate value and writes it to its stdout.

.SH OUTPUT
.LP
The value to use in B($LD_LIBRARY_PATH).

Footer
