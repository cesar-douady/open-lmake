Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023-2025 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

Header(ltarget,report targets from a OpenLmake job)

.SH DESCRIPTION
.LP
B(ltarget) may be used to pass flags to OpenLmake.
Unless specified otherwise, passed targets are allowed to be written to.
.LP
Note that :
Bullet
	Flags can be passed in (cf. OPTIONS below).
	Flags accumulate and will apply even if the file is independently accessed.
Bullet
	Targets are reported even if the autodep method (the I(autodep) rule attribute) is I(none).
	This is B(the) way (or a call to B(lmake.target)) of reporting targets in such a case (usually with I(--write)).
.LP
Symlobic links are not followed when files are interpreted.
Following symbolic links would inevitably lead to files being read (to check if they are symbolic links) before being a target, which would be an error.

.SH OPTIONS
.LP
Item(B(-W),B(--write))           Report an actual write, not only target flags. Default is to only alter flags.
Item(B(-X),B(--regexpr))         Pass flags to all targets matching regexprs passed as argument. The B(ignore) flag only applies to targets following this command.
Item(B(-x),B(--no-exclude-star)) Accept that flags are further processed according to regexpr-based requests, e.g. B(ltarget --regexpr), default is to exclude such processing.
Item(B(-E),B(--essential))       Show when generating user oriented graphs.
Item(B(-i),B(--incremental))     Targets are not unlinked before job execution and read accesses to them are ignored (including C(readdir,3)).
Item(B(-w),B(--no-warning))      No warning is emitted if targets are either uniquified or unlinked while generated by another job.
Item(B(-`I'),B(--ignore))        From now on, ignore all reads and writes to targets (including C(readdir,3)).
Item(B(-a),B(--no-allow))        Unless this option is passed, B(ltarget) makes its arguments valid targets.
Item(B(-s),B(--source-ok))       Unless this option is passed, writing to a source is an error. In that case, being simultaneously a dep and a target is ok.
.LP
In case mentioned targets turn out to be deps, the dep flags are also available:
Item(B(-c),B(--critical))     Create critical deps (cf. note (5)).
Item(B(-D),B(--readdir-ok))   Allow C(readdir,3) on passed deps even if not B(ignore)d nor B(incremental). Implies flag B(--no-required).
Item(B(-e),B(--ignore-error)) Ignore the error status of the passed deps.
Item(B(-r),B(--no-required))  Accept that deps be not buildable, as for a normal read access (in such a case, the read may fail, but OpenLmake is ok).

.SH NOTES

Footer
