Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023-2026 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

Header(ldecode,retrieve the original content encoded with C(lencode) within a OpenLmake job)

.SH DESCRIPTION
.LP
B(ldecode) may be used to ask for a value (typically rather large, up to a few kB) associated with a short code (a few characters, typically 3 to 5).
This must have been generated using the command C(lencode,1) with the same table and context.
The value corresponding to I(code) is output on stdout.
.LP
Usage and use cases are more extensively documented in the full OpenLmake documentation.

.SH OPTIONS
.LP
Item(B(-t) I(table),B(--table)=I(table))
I(table) may be:
Item(a key) found in B(lmake.config.codecs) in which case it is a local source file or an external dir.
Item(a local source file) (symbolic links are followed) recording the association table.
Item(an external dir)
.LP
In the former case, such a dir must lie within a source dir and must contain a file I(LMAKE/config.py) containing definitions for:
Item(B(file_sync)) one of B(none), B(dir) (default) or B(sync) for choosing the method to ensure proper consistent operations.
Item(B(perm))      one of B(none), B(group) or B(other) which specifies who is given permission to access this shared dir.
.LP
It is also an error if I(code) cannot be found with the accompanying I(context).

.LP
Item(B(-x) I(context),B(--context)=I(context)) specifies the context in which to find the value associated with passed code

.LP
Item(B(-c) I(code),B(--code)=I(code)) specifies the code to search within passed context in the passed table

.SH "EXIT STATUS"
.LP
B(ldecode) exits with a status of zero if the value could be encoded.
Else it exits with a non-zero status:
.LP
Item(B(2))  internal error, should not occur
Item(B(11)) bad usage : command line options and arguments coul not be parsed

.SH EXAMPLES
.LP
See C(lencode,1).

.SH NOTES
Item((1))
	The same functionality is provided with the B(lmake.decode) python function.
Item((2))
	C(lencode,1) and B(ldecode,1) are useful tools when the flow implies files whose names are impractical.
	This is a way to transform long filenames into much shorter ones by keeping an association table to retrieve long info from short codes.
Item((3))
	Using this functionality may imply C(git) conflicts on the association table (when a local source file) file when several users independently create associations in their repos.
	This is fully dealt with and the only thing left to the user is to accept the resolution of the conflict B(without any action).

Footer
