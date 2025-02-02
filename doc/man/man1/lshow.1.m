Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023-2025 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

Header(lshow,show various information about jobs in the OpenLmake build system)

.SH DESCRIPTION
.LP
Unless the I(--job) option is given, arguments are files and information are provided about the official jobs generating these as targets, if any.
Else, information about the job that actually produced the job (a.k.a the polluting job) are provided.

ClientGeneralities(color)

ClientOptions(color)

SubCommands

Item(B(-b),B(--bom))
Output the list of all source files necessary to build the arguments.
If I(--verbose) option is provided, some intermediate files are shown in gray, enough to justify why all listed source files.

Item(B(-c),B(--cmd))
Output the B(cmd) used to execute the job. If dynamic, it shows it as specialized for this job.

Item(B(-d),B(--deps))
Output the list of all the deps of the jobs.
Unless I(--verbose), only existing deps are shown.
.IP
If I(--quiet), the raw list of deps is output, with no header nor decoration.
.IP
Unless I(--quiet), each line is composed of 5 fields separated by spaces :
	.RS
	Bullet Flags : each flag is either a letter if set, or a B(-) if not :
		.RS
		Item(B(E))
		dep is essential, i.e. dep is show in a future graphical tool.
		Item(B(c))
		dep is critical
		Item(B(e))
		ignore errors on dep, i.e. job can be run even if this dep is built in error.
		Item(B(r))
		dep is required, i.e. job is in error if dep is not buildable. For static deps, job is not even tried if dep is not buildable (and another rule is used if possible).
		Item(B(S))
		dep is static.
		.RE
	Bullet Accesses : each kind of accesses is either a letter if done, or a - if not :
		.RS
		Item(B(L))
		dep has been accessed as a symbolic link.
		Item(B(R))
		dep has been accessed as a regular file.
		Item(B(T))
		dep has been accessed with a stat-like system call (i.e. its inode has been accessed).
		.RE
	Bullet Key : the key if the dep is static, else blank.
	Bullet Ascii art showing parallel deps (deps coming from a single call to B(ldepend) (1) or B(lmake.depend) are considered parallel).
	Bullet Name of the dep.
	.RE
.IP
If a dep does not exist, it is deemed secondary information and is shown in gray.

Item(B(-D),B(--inv-deps))
Show jobs that depend on targets.

Item(B(-E),B(--env))
Show the environment used to run the script

Item(B(-i),B(--info))
Show various self-reading info about jobs, such as reason to be launched, why it was needed, execution time, host that executed it, ...

Item(B(-r),B(--running))
Show the list of jobs currently running to build the arguments.
If I(--verbose), some waiting jobs are shown in gray, enough to justify why all running jobs are running.
Queued jobs are shown in blue, actively running jobs are uncolored.

Item(B(-e),B(--stderr))
Show the stderr of the jobs.
Unless I(--quiet), output is preceded by a description of the job which it relates to.

Item(B(-o),B(--stdout))
Show the stdout of the jobs.
Unless I(--quiet), output is preceded by a description of the job which it relates to.

Item(B(-t),B(--targets))
Show the targets of the jobs.
Unless I(--verbose), only existing targets are shown.
.IP
If I(--quiet), the raw list of targets is output, with no header nor decoration.
.IP
Unless I(--quiet), each line is composed of 3 fields  separated by spaces :
	.RS
	Bullet Info : composed of 1 characters : B(W) if target was written, B(U) if target was unlinked, else B(-).
	Bullet Flags : each flag is either a letter if set, or a B(-) if not :
		.RS
		Item(B(E))
		target is essential, i.e. target is show in a future graphical tool.
		Item(B(i))
		target is incremental, i.e. it is not unlinked before job execution and read accesses are ignored
		Item(B(p))
		target is phony, i.e. being non existing a considered as a particular value for that file
		Item(B(u))
		target is not uniquified if several links point to it before job execution (only meaningful for incremental targets).
		Item(B(w))
		no warning is emitted if target is either uniquified or unlinked while produced by another job.
		Item(B(S))
		target is static.
		Item(B(T))
		target is official, i.e. job can be triggered when another one depend on this target.
		.RE
	Bullet The name of the target.
	.RE
.IP
If a target does not exist, it is deemed secondary information and is shown in gray.

Item(B(-T),B(--inv-targets))
Show jobs that produce targets either officially (listed in B(targets) attribute) or not (listed in B(side_targets) attribute or not)
Unless I(--quiet), output is preceded by a description of the job which it relates to.

Item(B(-u),B(--trace))
Show an execution trace of the jobs.
Accesses generating no new dep are not shown.
The main purpose is to provide an explanation for each dep and target.
Unless I(--quiet), output is preceded by a description of the job which it relates to.

SpecificOptions
Item(B(-p),B(--porcelaine))
In porcelaine mode, information shown with I(--info) is provided as an easy-to-parse python B(dict).
Also,reported files are relative to the root of the repository, not the current workind directory.

.SH ENVIRONMENT
.LP
The content of B($LMAKE_VIDEO) is processed as if provided with the B(--video) option.

.SH FILES
CommonFiles

Footer
