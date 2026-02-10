Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023-2026 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

Header(lshow,show various information about jobs in the OpenLmake build system)

.SH DESCRIPTION
.LP
Unless the I(--job) option is given, arguments are files and information are provided about the official jobs generating these as targets, if any.
Else, information about the job that actually produced the job (a.k.a the polluting job) are provided.

ClientGeneralities(color)

ClientOptions(job,color)

SubCommands

Item(B(-b),B(--bom))
Output the list of all source files necessary to build the arguments.
If I(--verbose) option is provided, intermediate files are shown in gray.
.IP
If I(--porcelaine), the output is generated as B(set) (if not I(--verbose)) or as a B(tuple) (if I(--verbose)).
In the latter case, it is guaranteed that deps are going downards, i.e. dependents appear before their deps.
If arguments are files, the output is not a B(dict) and is the list necessary to generate all the arguments.

Item(B(-c),B(--cmd))
Output the B(cmd) used to execute the job. If dynamic, it shows it as specialized for this job.
Output is always generated with a single tab prefix for each line, so as to maintain internal alignment.
.IP
If I(--porcelaine), the output is generated as a B(str) (not indented).

Item(B(-d),B(--deps))
Output the list of all the deps of the jobs.
Unless I(--verbose), only existing deps are shown.
.IP
If I(--quiet), the raw list of deps is output, with no header nor decoration.
.IP
Unless I(--quiet), each line is composed of 5 or 6 fields separated by spaces :
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
		Note that this is implied when accessing a file while following symbolic links (which is the usual case).
		Note also that this flag is positioned with C(stat,2) as link size is accessible from the returned struct.
		Item(B(R))
		dep has been accessed as a regular file.
		Note that this flag is positioned with C(stat,2) as file size is accessible from the returned struct.
		Item(B(T))
		dep has been sensed for existence.
		Note that this is only semantically meaningful in absence of other accesses.
		As a rule of thumb, this flag is positioned with C(stat,2) but not with C(open,2) nor C(readlink,2).
		Item(B(E))
		dep status has been sensed, typically with a call to B(depend(verbose=True)) or B(ldepend --verbose).
		.RE
	Bullet Checksum : the checksum of the dep if I(--verbose), i.e. the checksum that must match the one of the underlying file or the job will be rerun.
	Bullet Key : the key if the dep is static, else blank.
	Bullet Ascii art showing parallel deps (deps coming from a single call to C(ldepend) or B(lmake.depend) are considered parallel).
	Bullet Name of the dep.
	.RE
.IP
If a dep does not exist, it is deemed secondary information and is shown in gray.
.IP
If I(--porcelaine) and args are files, the output is, for each file, a B(dict) indexed by jobs and whose values are as follows.
Jobs are represented as a tuple composed of the rule name, the job name and a comment.
The keys are all jobs potentially producing given file.
If arg is a job, the output is a mentioned below.
.IP
The value is a B(tuple) of sequential deps groups, each deps group being a B(set) of parallel deps.
Each dep is represented as a B(tuple) composed of dep flags, access flags, key and name.

Item(B(-D),B(--inv-deps))
Show jobs that depend on args.
This sub-command is only valid for file args.
.IP
If I(--verbose), obsolete jobs are also listed in gray.
.IP
If I(--porcelaine), the output is a B(set) of jobs, each job being represented as a B(tuple) composed of the rule name and the job name.

Item(B(-E),B(--env))
Show the environment used to run the script
.IP
If I(--porcelaine), the output is generated as as B(dict), much like B(os.environ).

Item(B(-i),B(--info))
Show various info about a job, as it last ran (unless stated otherwise):
	.RS
	Bullet B(rule)               : the rule name.
	Bullet B(job)                : the job name.
	Bullet B(ids)                : the job-id (unique for each job), the small-id (unique among jobs running simultaneously) and the seq-id (unique in a repo).
	Bullet B(required by)        : the job that last necessitated job to run.
	Bullet B(reason)             : the reason why job ran.
	Bullet B(host)               : the host on which job ran.
	Bullet B(os)                 : the os version, release and architecture on which the job ran.
	Bullet B(scheduling)         : the ETA of the lmake command, a B(-), the duration from start-of-job to end-of-lmake command along the longest dep path as estimated at run time
		(known as the pressure)  .
		Jobs are scheduled by giving higher priority to nearer ETA, then to higher pressure.
	Bullet B(cpu time)           : user+system cpu time of job, as reported by C(getrusage,2) with children.
	Bullet B(cost)               : elapsed time in job divided by the average number of jobs running in parallel.
		This value is used to compute the ETA of the lmake command.
	bullet B(used mem)           : max RSS, as reported by C(getrusage,2) with children.
	Bullet B(elapsed in job)     : elapsed time in job, excluding overhead.
	Bullet B(elapsed total)      : elapsed time in job, including overhead.
	Bullet B(end date)           : the date at which job ended.
	Bullet B(total targets size) : the sum of the sizes of all targets.
	Bullet B(compressed size)    : the sum of the sizes of all targets after compression as stored in cache.
	Bullet B(chroot_dir)         : the chroot dir in which job ran.
	Bullet B(chroot_actions)     : the chroot actions carried out to run job.
	Bullet B(lmake_root)         : the open-lmake installation dir used by the job.
	Bullet B(lmake_view)         : the name under which the lmake installation dir was seen by job.
	Bullet B(repo_view)          : the name under which the repo root dir was seen by job.
	Bullet B(physical tmp dir)   : the tmp dir on disk when viewed by job under another name.
	Bullet B(tmp dir)            : the tmp dir on disk when viewed by job under the same name.
	Bullet B(tmp_view)           : the name under which the tmp dir was seen by job.
	Bullet B(views)              : the view map as specified in the I(views) rule attribute.
	Bullet B(sub_repo)           : the sub-repo in which rule was defined.
	Bullet B(auto_mkdir)         : true if C(chdir,2) to a non-existent triggered an automatic C(mkdir,2) for the C(chdir,2) to succeed.
	Bullet B(autodep)            : the autodep method used.
	bullet B(backend)            : the backend used to launch job.
	bullet B(check_abs_paths)    : true if absolute paths inside the repo are checked (generate and error if found in targets).
	Bullet B(mount_chroot_ok)    : true if C(mount,2) and C(chroot,2) are allowed.
	Bullet B(readdir_ok)         : true if C(readdir,3) is allowed on local not B(ignore)d nor B(incremental) dirs.
	Bullet B(timeout)            : the timeout after which job would have/has been killed.
	Bullet B(use_script)         : true if a script was used to launch job (rather than directly using the I(-c) option to the interpreter).
	Bullet B(checksum)           : the checksum of the target if I(--verbose) and not I(--job).
	Bullet B(run status)         : whether job could be run last time the need arose (note: if not ok it is later than when job last ran).
		Possible values are:
		.RS
		Item(I(ok))             job could run.
		Item(I(dep_err))        job could not run because a dep was in error.
		Item(I(missing_static)) job could not run because a static dep was missing.
		Item(I(err))            job could not run because an error was detected before it started.
		.RE
	Bullet B(status)   : job status, as used by OpenLmake.
		Possible values are:
		.RS
		Item(I(new))            job was never run
		Item(I(early_chk_deps)) dep check failed before job actually started
		Item(I(early_err))      job was not started because of error
		Item(I(early_lost))     job was lost before starting, retry
		Item(I(early_lost_err)) job was lost before starting, do not retry
		Item(I(late_lost))      job was lost after having started, retry
		Item(I(late_lost_err))  job was lost after having started, do not retry
		Item(I(killed))         job was killed
		Item(I(chk_deps))       dep check failed
		Item(I(cache_match))    cache just reported deps, not result
		Item(I(bad_target))     target was not correctly initialized or simultaneously written by another job
		Item(I(ok))             job execution ended successfully
		Item(I(submit_loop))    job needs to rerun but was already submitted too many times
		Item(I(err))            job execution ended in error
		.RE
	Bullet B(rc)               : the return code of the job.
		Possible values are:
		.RS
		Item(I(ok))                            exited with code 0 and stderr was empty or allowed to be non-empty.
		Item(I(ok (with non-empty stderr)))    exited with code 0 and stderr was non-empty nor allowed to be non-empty.
		Item(I(exit <n>))                      exited with code <n> (non-zero).
		Item(I(exit <n> (could be signal<s>))) exited with code <n> which is possibly generated by the shell in response to a process killed by signal <s>.
		Item(I(signal <s>))                    killed with signal <s>.
		.RE
	Bullet B(start message)      : a message emitted at job start time.
	Bullet B(message)            : a message emitted at job end time.
	Bullet B(resources)          : the job resources.
		In some cases, the atual allocated resources is different from the required resources, in which case both values are shown.
	.RE

	.IP
	All information are not necessarily shown:
	.RS
	Bullet If the job is running, information that are only available after job end are not shown.
	Bullet Non-pertinent information (e.g. compressed size when targets have not been compressed) are not shown.
	Bullet 0, false or other values carrying no information are not shown.
	.RE

	.IP
	If I(--porcelaine), the output contains the same information generated as a B(dict).

Item(B(-r),B(--running))
Show the list of jobs currently running to build the arguments.
If I(--verbose), some waiting jobs are shown in gray, prefixed by key B(W), enough to justify why all running jobs are running.
Queued jobs are shown in blue, prefixed with key B(Q), actively running jobs are uncolored, prefixed with key B(R).
.IP
If I(--porcelaine), whether args are a job or files, the output is a B(set) (if not I(--verbose)) or a B(tuple) (if I(--verbose)) of jobs.
Each job is represented as a B(tuple) composed of the key (B('W'), B('Q') or B('R')), the rule name and the job name.

Item(B(-e),B(--stderr))
Show the stderr of the jobs.
Unless I(--quiet), output is preceded by a description of the job which it relates to.
Output is always generated with a single tab prefix for each line, so as to maintain internal alignment.
.IP
If I(--porcelaine), the output is generated as a B(tuple) composed of a message generated by lmake at start time, a message generated by lmake at en time and the stderr of the job.
All three items are B(str) (not indented).

Item(B(-o),B(--stdout))
Show the stdout of the jobs.
Unless I(--quiet), output is preceded by a description of the job which it relates to.
.IP
If I(--porcelaine), the output is generated as a B(str).

Item(B(-t),B(--targets))
Show the targets of the jobs.
Unless I(--verbose), only existing targets are shown.
.IP
If I(--quiet), the raw list of targets is output, with no header nor decoration.
.IP
Unless I(--quiet), each line is composed of 4 or 5 fields  separated by spaces :
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
	Bullet the checksum of the target if I(--verbose).
	Bullet the key under which the target is known, if any.
	Bullet The name of the target.
	.RE
.IP
If a target does not exist, it is deemed secondary information and is shown in gray.
.IP
If I(--porcelaine), the output is generated as a B(set) of targets,
each target being represented as a B(tuple) composed the info (B('W'), B('U') or B('-')), the target flags, the key and the target name.

Item(B(-T),B(--inv-targets))
Show jobs that have given file as a target (either official or not).
This sub-command is only valid for file args.
.IP
If I(--verbose), obsolete jobs are also listed in gray.
.IP
If I(--porcelaine), the output is a B(set) of jobs, each job being represented as a B(tuple) composed of the rule name and the job name.

Item(B(-u),B(--trace))
Show an execution trace of the jobs.
Each entry is compose of a date, a tag and a file (or other info, depending on tag).
The tag is either a keyword, of a keyword with various attributes in ().
Accesses generating no new dep are not shown.
.IP
The main purpose is to provide an explanation for each dep and target.
.IP
If I(--porcelaine), the output is generated as a B(tuple) of entries, each entry being represented as a tuple composed of a date, a tag and a file (as described above), each being a B(str).

SpecificOptions
Item(B(-p),B(--porcelaine))
In porcelaine mode, information is provided as an easy-to-parse python object.
Also,reported files are relative to the root of the repo, not the current workind dir.
.IP
If argument is a job, the output is as described for each sub-command.
unless mentioned otherwise, if arguments are files, the output is a B(dict) whose keys are the arguments and values are as described above.

.SH "EXIT STATUS"
.LP
B(lshow) exits with a status of zero if the asked targets could be shown.
Else it exits with a non-zero status:
.LP
Item(B(1))  some targets could not be shown
Item(B(2))  internal error, should not occur
Item(B(4))  server could not be started
Item(B(5))  internal repo state was inconsistent
Item(B(7))  adequate permissions were missing, typically write access
Item(B(8))  server crashed, should not occur
Item(B(10)) some syscall failed
Item(B(11)) bad usage : command line options and arguments coul not be parsed
Item(B(12)) bad repo version, repo need to be cleaned, e.g. with B(git clean -ffdx)

.SH EXAMPLES
.LP
V(lmake a_file)
.LP
V(lshow -i a_file)

.SH ENVIRONMENT
.LP
The content of B($LMAKE_VIDEO) is processed as if provided with the I(--video) option.

.SH FILES
CommonFiles

Footer
