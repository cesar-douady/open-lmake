Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023-2025 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

Header(ldepend,report a dep from a OpenLmake job)

.SH DESCRIPTION
.LP
B(ldepend) may be used to pass flags to OpenLmake.
Unless specified otherwise, passed deps are required to be buildable an produced without error.
.LP
Note that :
Bullet
	Generated deps are parallel, i.e. a modification on a dep will not mask an error on another one (cf. note (3)).
Bullet
	Deps are acquired all at once (cf. note (4)).
Bullet
	Flags can be passed in (cf OPTIONS below).
	Flags accumulate and will apply even if the file is independently accessed.
Bullet
	Deps are reported even if the autodep method (the I(autodep) rule attribute) is I(none).
	This is B(the) way (or a call to B(lmake.depend)) of reporting deps in such a case (usually with I(--read)).

.SH OPTIONS
.LP
Item(B(-L),B(--follow-symlinks)) Follow the last level symbolic link, default is not to follow.
Item(B(-v),B(--verbose))
	Write lines composed of the crc and the name separated by a space for each required dep.
	.RS
	The crc is :
	Item(I(unknown)) the crc could not be computed, typically because the file is a special file (such as a device for example)
	Item(I(none))    the file does not exist
	Item(I(empty-R)) the file is an empty non-executable regular file
	Item(I(<crc>-R)) the file is a regular file , <crc> is 16-digit hexadecimal number computed after its content (the exe permission is taken into account when computing crc)
	Item(I(<crc>-L)) the file is a symbolic link, <crc> is 16-digit hexadecimal number computed on the link (not the content of the target of the link)
	.RE
Item(B(-R),B(--read))         Report an actual read. Default is to only alter flags.
Item(B(-c),B(--critical))     Create critical deps (cf. note (5)).
Item(B(-E),B(--essential))    Passed deps will appear in the flow shown with a graphical tool.
Item(B(-e),B(--ignore-error)) Ignore the error status of the passed dependencies.
Item(B(-r),B(--no-required))  Accept that deps be not buildable, as for a normal read access (in such a case, the read may fail, but OpenLmake is ok).
Item(B(-I),B(--ignore))       Deps are ignored altogether, even if further accessed (but previous accesses are kept).
Default is to optimize dependency check as much as possible.

.SH NOTES
Item((1))
	The same functionality is provided with the B(lmake.depend) Python function.
Item((2))
	Flags can be associated to deps on a regexpr (matching on dep name) basis by using the B(side_deps) rule attribute.
Item((3))
	If B(cat a b) executed, OpenLmake sees 2 C(open,2) system calls, to I(a) then to I(b), exactly the same sequence that if you did B(cat $(cat a)) and I(a) contained I(b).
	.IP
	Suppose now that I(b) is an error. This is a reason for your job to be in error.
	But if you modify I(a), in the former case, this cannot solve your error while in the later case, it may if the new content of I(a) points to a file that may successfully be built.
	Because OpenLmake cannot distinguish between the 2 cases, upon a modification of I(a), the job will be rerun in the hope that I(b) is not accessed any more.
	Parallel deps prevents this trial.
Item((4))
	If a series of files are read in a loop and the loop is written in such a way as to stop on the first error
	and if the series of file does not depend on the actual content of said files,
	then it is preferable to pre-access (using B(ldepend)) all files before starting the loop.
	The reason is that without this precaution, deps will be discovered one by one and may be built serially instead of all of them in parallel.
Item((5))
	If a series of dep is directly derived from the content of a file, it may be wise to declare it as critical.
	When a critical dep is modified, OpenLmake forgets about deps reported after it.
	.IP
	Usually, when a file is modified, this has no influence on the list of files that are accessed after it,
	and OpenLmake anticipates this by building these deps speculatively.
	But in some situations, it is almost certain that there will be an influence and it is preferable not to anticipate.
	this is what critical deps are made for : in case of modifications, following deps are not built speculatively.

Footer
