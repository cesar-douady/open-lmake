Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023-2025 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

Title(lencode,encode a (long) value into a (short) code for retrieval with C(ldecode) within a OpenLmake job)
.SH SYNOPSIS
B(lencode) I(association_file) I(context) [I(min_length)]

.SH DESCRIPTION
.LP
B(lencode) may be used to create (or retrieve if already present) an association between a value (typically rather large) and a short code.
This association must be done before C(ldecode) is called with the code (with same I(assocation_file) and I(context)) to retrieve the corresponding value.
.LP
The value must be passed to I(stdin).
The generated (or retrieved) code corresponding to the value is output on I(stdout).
.LP
If generated, the code is generated after a checksum computed on the passed value in hexadecimal, with a length at least I(min_length) (default is 1), but may be longer in case of conflict.
.LP
It is an error if I(association_file) is not a source (symbolic links are followed, though).
.LP
Usage and use cases are more extensively documented the full OpenLmake documentation.

.SH NOTES
Item((1))
	The same functionality is provided with the B(lmake.encode) python function.
Item((2))
	B(lencode) and C(ldecode) are useful tools when the flow implies file names whose names are impractical.
	This is a way to transform long file names into much shorter ones by keeping an association file to retrieve long info from short codes.
Item((3))
	Using this functionality may imply C(git) conflicts on the association file when several users independently create associations in their repos.
	This is fully dealt with and the only thing left to the user is to accept the resolution of the conflict B(without any action).

Footer
