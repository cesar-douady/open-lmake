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
.LP
Each dep is associated with an access pattern.
Accesses are of 3 kinds, regular, link and stat:
Bullet
	Regular means that the file was accessed using C(open,2) or similar, i.e. the job is sensitive to the file content if it is a regular file, but not to the target in case it is a symbolic link.
Bullet
	Link means that the file was accessed using C(readlink,2) or similar, i.e. the job is sensitive to the target if it is a symbolic link, but not to the content in case it is a regular file.
Bullet
	Stat means that the file meta-data were accessed, i.e. the job is sensitive to file existence and type, but not to the content or its target.
.LP
If a file have none of these accesses, changing it will not trigger a rebuild, but it is still a dep as in case it is in error, this will prevent the job from being run.
Making such dinstinctions is most useful for the automatic processing of symbolic links.
For example, if file I(a/b) is opened for reading, and it turns out to be a symbolic link to I(c), OpenLmake will set a dep to I(a/b) as a link, and to I(a/c)
as a link (in case it is itself a link) and regular (as it is opened).
.LP
By default, passed deps are associated with no access, but are required to be buildable and produced without error.
To simulate a plain access, you need to pass the B(--read) option to associate accesses and the B(--no-required) to allow it not to exist.
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
	Write lines composed of the checksum and the name separated by a space for each required dep.
	.RS
	The checksum is :
	Item(I(unknown)) the checksum could not be computed, typically because the file is a special file (such as a device for example)
	Item(I(none))    the file does not exist
	Item(I(empty-R)) the file is an empty non-executable regular file
	Item(I(<checksum>-R)) the file is a regular file , <checksum> is 16-digit hexadecimal number computed after its content (the exe permission is taken into account when computing checksum)
	Item(I(<checksum>-L)) the file is a symbolic link, <checksum> is 16-digit hexadecimal number computed on the link (not the content of the target of the link)
	.RE
Item(B(-R),B(--read))         Report an actual read. Default is to only alter flags.
Item(B(-c),B(--critical))     Create critical deps (cf. note (5)).
Item(B(-E),B(--essential))    Passed deps will appear in the flow shown with a graphical tool.
Item(B(-e),B(--ignore-error)) Ignore the error status of the passed deps.
Item(B(-r),B(--no-required))  Accept that deps be not buildable, as for a normal read access (in such a case, the read may fail, but OpenLmake is ok).
Item(B(-I),B(--ignore))       Deps are ignored altogether, even if further accessed (but previous accesses are kept).
Default is to optimize dep check as much as possible.

.SH NOTES
Item((1))
	The same functionality is provided with the B(lmake.depend) python function.
Item((2))
	Flags can be associated to deps on a regexpr (matching on dep name) basis by using the B(side_deps) rule attribute.
Item((3))
	If B(cat a b) is executed, OpenLmake sees 2 C(open,2) system calls, to I(a) then to I(b), exactly the same sequence that if one did B(cat $(cat a)) and I(a) contained I(b).
	.IP
	Suppose now that I(b) is an error. This is a reason for your job to be in error.
	But if I(a) is modified, in the former case, this cannot solve your error while in the latter case, it may if the new content of I(a) points to a file that may successfully be built.
	Because OpenLmake cannot distinguish between the 2 cases, upon a modification of I(a), the job will be rerun in the hope that I(b) is not accessed any more.
	Parallel deps prevents this trial.
Item((4))
	If a series of files are read in a loop and the loop is written in such a way as to stop on the first error
	and if the series of file does not depend on the actual content of said files,
	then it is preferable to pre-access (using B(ldepend)) all files before starting the loop.
	The reason is that without this precaution, deps will be discovered one by one and may be built serially instead of all of them in parallel.
Item((5))
	If a series of dep is directly derived from the content of a file, it may be wise to declare it as B(critical).
	When a critical dep is modified, OpenLmake forgets about deps reported after it.
	.IP
	Usually, when a file is modified, this has no influence on the list of files that are accessed after it,
	and OpenLmake anticipates this by building these deps speculatively.
	But in some situations, it is almost certain that there will be an influence and it is preferable not to anticipate.
	this is what critical deps are made for: in case of modifications, following deps are not built speculatively.

Footer
