Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

Header(ltarget,report a target from a OpenLmake job)

.SH DESCRIPTION
.LP
B(ltarget) may be used to inform OpenLmake that some files must be deemed as written as if you called C(open,2) (with the O_WRONLY flag) on each of them.
.LP
Also, there are a few differences :
Bullet
	Flags can be passed in (cf OPTIONS below).
	Flags accumulate and will apply even if the file is independently accessed.
Bullet
	Targets are reported even if the autodep method (the I(autodep) rule attribute) is I(none).
	This is B(the) way (or a call to B(lmake.target)) of reporting targets in such a case.
.LP
Symlobic links are not followed when files are interpreted.
Following symbolic links would inevitably lead to files being read (to check if they are symbolic links) before being a target, which would be an error.

.SH OPTIONS
.LP
Item(B(-W),B(--no-write))    Does not report an actual write, only target flags. Default is to report a write and alter flags.
Item(B(-E),B(--essential))   Show when generating user oriented graphs.
Item(B(-i),B(--incremental)) Target is not unlinked before job execution and read accesses to it are ignored.
Item(B(-u),B(--no-uniquify)) Target is not uniquified if several links are pointing to it. Only meaningful for incremental targets.
Item(B(-w),B(--no-warning))  No warning is emitted if target is either uniquified or unlinked while generated by another job.
Item(B(-I),B(--ignore))      From now on, ignore all reads and writes to target.
Item(B(-a),B(--no-allow))    Unless this option is passed, B(ltarget) makes its arguments valid targets (cf @pxref{targets-deps}).
Item(B(-s),B(--source-ok))   Unless this option is passed, B(ltarget) writing to a source is an error.

.SH NOTES

Footer
