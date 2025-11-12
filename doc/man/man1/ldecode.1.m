Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023-2025 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

Title(ldecode,retrieve the original content encoded with C(lencode) within a OpenLmake job)
.SH SYNOPSIS
B(ldecode) I(association_file) I(context) I(code)

.SH DESCRIPTION
.LP
B(ldecode) may be used to ask for a value (typically rather large) associated with a short code.
This must have been generated using the command C(lencode) with the same association_file and context.
The value corresponding to I(code) is output on stdout.
.LP
I(association_file) (symbolic links are followed) may be either a source file within repo or a dir (ending with B('/')) that lies within a source dir.
In the latter case, such a dir must lie within a source dir and must contain a file I(LMAKE/config.py) containing definitions for :
Item(B(file_sync)) one of B(none), B(dir) (default) or B(sync) for choosing the method to ensure proper consistent operations.
Item(B(perm))      one of B(none), B(group) or B(other) which specifies who is given permission to access this shared dir.
.LP
It is also an error if I(code) cannot be found with the accompanying I(context).
.LP
Usage and use cases are more extensively documented the full OpenLmake documentation.

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
	C(lencode) and B(ldecode) are useful tools when the flow implies file names whose names are impractical.
	This is a way to transform long file names into much shorter ones by keeping an association file to retrieve long info from short codes.
Item((3))
	Using this functionality may imply C(git) conflicts on the association file when several users independently create associations in their repos.
	This is fully dealt with and the only thing left to the user is to accept the resolution of the conflict B(without any action).

Footer
