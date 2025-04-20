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
If I(--verbose) option is provided, intermediate files are shown in gray.

If B(--porcelaine), the output is generated as B(set) (if not B(--verbose)) or as a B(tuple) (if B(--verbose)).
In the latter case, it is guaranteed that deps are going downards, i.e. dependents appear before their dependencies.
If arguments are files, the output is not a B(dict) and is the list necessary to generate all the arguments.

Item(B(-c),B(--cmd))
Output the B(cmd) used to execute the job. If dynamic, it shows it as specialized for this job.
Output is always generated with a single tab prefix for each line, so as to maintain internal alignment.

If B(--porcelaine), the output is generated as a B(str) (not indented).

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

If B(--porcelaine) and args are files, the output is, for each file, a B(dict) indexed by jobs and whose values are as follows.
Jobs are represented as a tuple composed of the rule name, the job name and a comment.
The keys are all jobs potentially producing given file.
If arg is a job, the output is a mentioned below.

The value is a B(tuple) of sequential deps groups, each deps group being a tuple of parallel deps.
Each dep is represented as a B(tuple) composed of dep flags, access flags, key and name.

Item(B(-D),B(--inv-deps))
Show jobs that depend on args.
This sub-command is only valid for file args.

If B(--verbose), obsolete jobs are also listed in gray.

If B(--porcelaine), the output is a B(set) of jobs, each job being represented as a B(tuple) composed of the rule name and the job name.

Item(B(-E),B(--env))
Show the environment used to run the script

If B(--porcelaine), the output is generated as as B(dict), much like B(os.environ).

Item(B(-i),B(--info))
Show various self-reading info about jobs, such as reason to be launched, why it was needed, execution time, host that executed it, ...

If B(--porcelaine), the output contains the same information generated as a B(dict).

Item(B(-r),B(--running))
Show the list of jobs currently running to build the arguments.
If I(--verbose), some waiting jobs are shown in gray, prefixed by key B(W), enough to justify why all running jobs are running.
Queued jobs are shown in blue, prefixed with key B(Q), actively running jobs are uncolored, prefixed with key B(R).

If B(--porcelaine), whether args are a job or files, the output is a B(set) (if not B(--verbose)) or a B(tuple) (if B(--verbose)) of jobs.
Each job is represented as a B(tuple) composed of the key (B('W'), B('Q') or B('R')), the rule name and the job name.

Item(B(-e),B(--stderr))
Show the stderr of the jobs.
Unless I(--quiet), output is preceded by a description of the job which it relates to.
Output is always generated with a single tab prefix for each line, so as to maintain internal alignment.

If B(--porcelaine), the output is generated as a B(tuple) composed of a message generated by lmake at start time, a message generated by lmake at en time and the stderr of the job.
All three items are B(str) (not indented).

Item(B(-o),B(--stdout))
Show the stdout of the jobs.
Unless I(--quiet), output is preceded by a description of the job which it relates to.

If B(--porcelaine), the output is generated as a B(str).

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

If B(--porcelaine), the output is generated as a B(set) of targets,
each target being represented as a B(tuple) composed the info (B('W'), B('U') or B('-')), the target flags, the key and the target name.

Item(B(-T),B(--inv-targets))
Show jobs that have given file as a target (either official or not).
This sub-command is only valid for file args.

If B(--verbose), obsolete jobs are also listed in gray.

If B(--porcelaine), the output is a B(set) of jobs, each job being represented as a B(tuple) composed of the rule name and the job name.

Item(B(-u),B(--trace))
Show an execution trace of the jobs.
Each entry is compose of a date, a tag and a file (or other info, depending on tag).
The tag is either a keyword, of a keyword with various attributes in ().
Accesses generating no new dep are not shown.

The main purpose is to provide an explanation for each dep and target.

If B(--porcelaine), the output is generated as a B(tuple) of entries, each entry being represented as a tuple composed of a date, a tag and a file (as described above), each being a B(str).

SpecificOptions
Item(B(-p),B(--porcelaine))
In porcelaine mode, information is provided as an easy-to-parse python object.
Also,reported files are relative to the root of the repository, not the current workind directory.

If argument is a job, the output is as described for each sub-command.
unless mentioned otherwise, if arguments are files, the output is a B(dict) whose keys are the arguments and values are as described above.

.SH ENVIRONMENT
.LP
The content of B($LMAKE_VIDEO) is processed as if provided with the B(--video) option.

.SH FILES
CommonFiles

Footer
