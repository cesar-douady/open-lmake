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
I(association_file) (symbolic links are followed) may be either a source file within repo or a dir (ending with B('/')) that lies within a source dir.
.LP
Associations are usually created using `encode` but not necessarily (they can be created by hand).
.LP
Usage and use cases are more extensively documented the full OpenLmake documentation.

.SH EXAMPLES
.LP
V(touch my_codec_file) # contains an association table code <==> value with each context
.LP
V(git add my_codec_file) # my_codec_file must be a source
.LP
V(lencode my_codec_file my_context 3 <<EOF)
.LP
V(first line)
.LP
V(second line)
.LP
V(EOF)
.LP
V(==>)
.LP
V(3ab)
.LP
V(ldecode my_codec_file my_context 3ab)
.LP
V(==>)
.LP
V(first line)
.LP
V(second line)

.SH "EXIT STATUS"
.LP
B(lencode) exits with a status of zero if the code could be decoded.
Else it exits with a non-zero status:
.LP
Item(B(1))  the code was not found with given file and context
Item(B(2))  internal error, should not occur
Item(B(11)) bad usage : command line options and arguments coul not be parsed

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
