Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023-2026 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

Header(lshow,show various information about jobs in the OpenLmake build system)

.SH DESCRIPTION
.LP
Unless the I_(--job) option is given, arguments are files and information are provided about the official jobs generating these as targets, if any.
Else, information about the job that actually produced the job (a.k.a the polluting job) are provided.

ClientGeneralities(color)

ClientOptions(job,color)

SubCommands

Item(B_(-b),B_(--bom))
Output the list of all source files necessary to build the arguments.
Unless I_(--quiet), intermediate files are shown in gray.
If I_(--verbose), non-existent files are shown in gray.
Existent files are prefixed with B_(+), non-existent files are prefixed with B_(!).
.LP
Files are mentioned only once.
.IP
If I_(--porcelaine), the output is generated as an easy to parse B_(dict).
Files are represented by their name as key.
If a file is intermediate, the value is the dict of files necessary to generate it.
Else, the value is B_(True) if file exists or  B_(False) if it does not exist.

Item(B_(-c),B_(--cmd))
Output the B_(cmd) used to execute the job. If dynamic, it shows it as specialized for this job.
Output is always generated with a single tab prefix for each line, so as to maintain internal alignment.
.IP
If I_(--porcelaine), the output is generated as a B_(str) (not indented).

Item(B_(-d),B_(--deps))
Output the list of all the deps of the jobs.
Unless I_(--verbose), only existing deps are shown.
.IP
If I_(--quiet), the raw list of deps is output, with no header nor decoration.
.IP
Unless I_(--quiet), each line is composed of 5 or 6 fields separated by spaces :
	.RS
	Bullet Flags : each flag is either a letter if set, or a B_(-) if not :
		.RS
		Item(B_(E))
		dep is essential, i.e. dep is show in a future graphical tool.
		Item(B_(c))
		dep is critical
		Item(B_(e))
		ignore errors on dep, i.e. job can be run even if this dep is built in error.
		Item(B_(r))
		dep is required, i.e. job is in error if dep is not buildable. For static deps, job is not even tried if dep is not buildable (and another rule is used if possible).
		Item(B_(S))
		dep is static.
		.RE
	Bullet Accesses : each kind of accesses is either a letter if done, or a - if not :
		.RS
		Item(B_(L))
		dep has been accessed as a symbolic link.
		Note that this is implied when accessing a file while following symbolic links (which is the usual case).
		Note also that this flag is positioned with C_(stat,2) as link size is accessible from the returned struct.
		Item(B_(R))
		dep has been accessed as a regular file.
		Note that this flag is positioned with C_(stat,2) as file size is accessible from the returned struct.
		Item(B_(T))
		dep has been sensed for existence.
		Note that this is only semantically meaningful in absence of other accesses.
		As a rule of thumb, this flag is positioned with C_(stat,2) but not with C_(open,2) nor C_(readlink,2).
		Item(B_(E))
		dep status has been sensed, typically with a call to B_(depend(verbose=True)) or B_(ldepend --verbose).
		.RE
	Bullet Checksum          : if I_(--verbose), the checksum of the dep i.e. the checksum that must match the one of the underlying file or the job will be rerun.
	Bullet Checksum validity : if I_(--verbose), the checksum is followed with a B_(*) if it has not been computed on the file actually present on disk.
	Bullet Key               : the key if the dep is static, else blank.
	Bullet Ascii art showing parallel deps (deps coming from a single call to C_(ldepend) or B_(lmake.depend) are considered parallel).
	Bullet Name of the dep.
	.RE
.IP
If a dep does not exist, it is deemed secondary information and is shown in gray.
.IP
If I_(--porcelaine) and args are files, the output is, for each file, a B_(dict) indexed by jobs and whose values are as follows.
Jobs are represented as a tuple composed of the rule name, the job name and a comment.
The keys are all jobs potentially producing given file.
If arg is a job, the output is a mentioned below.
.IP
The value is a B_(tuple) of sequential deps groups, each deps group being a B_(set) of parallel deps.
Each dep is represented as a B_(tuple) composed of dep flags, access flags, key and name.

Item(B_(-D),B_(--inv-deps))
Show jobs that depend on args.
This sub-command is only valid for file args.
.IP
If I_(--verbose), obsolete jobs are also listed in gray.
.IP
If I_(--porcelaine), the output is a B_(set) of jobs, each job being represented as a B_(tuple) composed of the rule name and the job name.

Item(B_(-E),B_(--env))
Show the environment used to run the script
.IP
If I_(--porcelaine), the output is generated as as B_(dict), much like B_(os.environ).

Item(B_(-i),B_(--info))
Show various info about a job, as it last ran (unless stated otherwise):
	.RS
	Bullet B_(rule)               : the rule name.
	Bullet B_(job)                : the job name.
	Bullet B_(ids)                : the job-id (unique for each job), the small-id (unique among jobs running simultaneously) and the seq-id (unique in a repo).
	Bullet B_(required target)    : the target that was last required.
	Bullet B_(required by)        : the job that last required the required target.
	Bullet B_(reason)             : the reason why job ran.
	Bullet B_(host)               : the host on which job ran.
	Bullet B_(os)                 : the os version, release and architecture on which the job ran.
	Bullet B_(scheduling)         : the ETA of the lmake command, a B_(-), the duration from start-of-job to end-of-lmake command along the longest dep path as estimated at run time
		(known as the pressure)  .
		Jobs are scheduled by giving higher priority to nearer ETA, then to higher pressure.
	Bullet B_(cpu time)           : user+system cpu time of job, as reported by C_(getrusage,2) with children.
	Bullet B_(cost)               : elapsed time in job divided by the average number of jobs running in parallel.
		This value is used to compute the ETA of the lmake command.
	bullet B_(used mem)           : max RSS, as reported by C_(getrusage,2) with children.
	Bullet B_(elapsed in job)     : elapsed time in job, excluding overhead.
	Bullet B_(elapsed total)      : elapsed time in job, including overhead.
	Bullet B_(end date)           : the date at which job ended.
	Bullet B_(total targets size) : the sum of the sizes of all targets.
	Bullet B_(compressed size)    : the sum of the sizes of all targets after compression as stored in cache.
	Bullet B_(chroot_dir)         : the chroot dir in which job ran.
	Bullet B_(chroot_actions)     : the chroot actions carried out to run job.
	Bullet B_(lmake_root)         : the open-lmake installation dir used by the job.
	Bullet B_(lmake_view)         : the name under which the lmake installation dir was seen by job.
	Bullet B_(repo_view)          : the name under which the repo root dir was seen by job.
	Bullet B_(physical tmp dir)   : the tmp dir on disk when viewed by job under another name.
	Bullet B_(tmp dir)            : the tmp dir on disk when viewed by job under the same name.
	Bullet B_(tmp_view)           : the name under which the tmp dir was seen by job.
	Bullet B_(views)              : the view map as specified in the I_(views) rule attribute.
	Bullet B_(sub_repo)           : the sub-repo in which rule was defined.
	Bullet B_(auto_mkdir)         : true if C_(chdir,2) to a non-existent triggered an automatic C_(mkdir,2) for the C_(chdir,2) to succeed.
	Bullet B_(autodep)            : the autodep method used.
	bullet B_(backend)            : the backend used to launch job.
	bullet B_(check_abs_paths)    : true if absolute paths inside the repo are checked (generate and error if found in targets).
	Bullet B_(mount_chroot_ok)    : true if C_(mount,2) and C_(chroot,2) are allowed.
	Bullet B_(readdir_ok)         : true if C_(readdir,3) is allowed on local not B_(ignore)d nor B_(incremental) dirs.
	Bullet B_(timeout)            : the timeout after which job would have/has been killed.
	Bullet B_(use_script)         : true if a script was used to launch job (rather than directly using the I_(-c) option to the interpreter).
	Bullet B_(checksum)           : the checksum of the target if I_(--verbose) and not I_(--job).
	Bullet B_(run status)         : whether job could be run last time the need arose (note: if not ok it is later than when job last ran).
		Possible values are:
		.RS
		Item(I_(ok))             job could run.
		Item(I_(dep_err))        job could not run because a dep was in error.
		Item(I_(missing_static)) job could not run because a static dep was missing.
		Item(I_(err))            job could not run because an error was detected before it started.
		.RE
	Bullet B_(status)   : job status, as used by OpenLmake.
		Possible values are:
		.RS
		Item(I_(new))            job was never run
		Item(I_(early_chk_deps)) dep check failed before job actually started
		Item(I_(early_err))      job was not started because of error
		Item(I_(early_lost))     job was lost before starting, retry
		Item(I_(early_lost_err)) job was lost before starting, do not retry
		Item(I_(late_lost))      job was lost after having started, retry
		Item(I_(late_lost_err))  job was lost after having started, do not retry
		Item(I_(killed))         job was killed
		Item(I_(chk_deps))       dep check failed
		Item(I_(cache_match))    cache just reported deps, not result
		Item(I_(bad_target))     target was not correctly initialized or simultaneously written by another job
		Item(I_(ok))             job execution ended successfully
		Item(I_(submit_loop))    job needs to rerun but was already submitted too many times
		Item(I_(err))            job execution ended in error
		.RE
	Bullet B_(rc)               : the return code of the job.
		Possible values are:
		.RS
		Item(I_(ok))                            exited with code 0 and stderr was empty or allowed to be non-empty.
		Item(I_(ok (with non-empty stderr)))    exited with code 0 and stderr was non-empty nor allowed to be non-empty.
		Item(I_(exit <n>))                      exited with code <n> (non-zero).
		Item(I_(exit <n> (could be signal<s>))) exited with code <n> which is possibly generated by the shell in response to a process killed by signal <s>.
		Item(I_(signal <s>))                    killed with signal <s>.
		.RE
	Bullet B_(start message)      : a message emitted at job start time.
	Bullet B_(message)            : a message emitted at job end time.
	Bullet B_(resources)          : the job resources.
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
	If I_(--porcelaine), the output contains the same information generated as a B_(dict).

Item(B_(-r),B_(--running))
Show the list of jobs currently running to build the arguments.
Wating jobs are prefixed with B_(W), queued jobs are prefixed with B_(Q) and running jobs are prefixed with (R).
Not running jobs are shown in gray.
.LP
Jobs are only mentioned only once.
.LP
If I_(--quiet), waiting jobs are not mentioned.
Unless I_(--verbose), queued jobs are not mentioned.
.IP
If I_(--porcelaine), whether args are a job or files, the output is an easy to parse B_(dict).
Each job is represented as a B_(tuple) composed of the rule name and the job name.
Jobs are shown with a B_(tuple) composed of the rule name and the job name. as key.
If a job is wating, the value is a B_(dict) of jobs it is waiting for.
Else, the value if B_(True) if job is running or B_(False) if the job is queued.

Item(B_(-e),B_(--stderr))
Show the stderr of the jobs.
Unless I_(--quiet), output is preceded by a description of the job which it relates to.
Output is always generated with a single tab prefix for each line, so as to maintain internal alignment.
.IP
If I_(--porcelaine), the output is generated as a B_(tuple) composed of a message generated by lmake at start time, a message generated by lmake at en time and the stderr of the job.
All three items are B_(str) (not indented).

Item(B_(-o),B_(--stdout))
Show the stdout of the jobs.
Unless I_(--quiet), output is preceded by a description of the job which it relates to.
.IP
If I_(--porcelaine), the output is generated as a B_(str).

Item(B_(-t),B_(--targets))
Show the targets of the jobs.
Unless I_(--verbose), only existing targets are shown.
.IP
If I_(--quiet), the raw list of targets is output, with no header nor decoration.
.IP
Unless I_(--quiet), each line is composed of 4 or 5 fields  separated by spaces :
	.RS
	Bullet Info : composed of 1 characters : B_(W) if target was written, B_(U) if target was unlinked, else B_(-).
	Bullet Flags : each flag is either a letter if set, or a B_(-) if not :
		.RS
		Item(B_(E))
		target is essential, i.e. target is show in a future graphical tool.
		Item(B_(i))
		target is incremental, i.e. it is not unlinked before job execution and read accesses are ignored
		Item(B_(p))
		target is phony, i.e. being non existing a considered as a particular value for that file
		Item(B_(u))
		target is not uniquified if several links point to it before job execution (only meaningful for incremental targets).
		Item(B_(w))
		no warning is emitted if target is either uniquified or unlinked while produced by another job.
		Item(B_(S))
		target is static.
		Item(B_(T))
		target is official, i.e. job can be triggered when another one depend on this target.
		.RE
	Bullet the checksum of the target if I_(--verbose).
	Bullet the key under which the target is known, if any.
	Bullet The name of the target.
	.RE
.IP
If a target does not exist, it is deemed secondary information and is shown in gray.
.IP
If I_(--porcelaine), the output is generated as a B_(set) of targets,
each target being represented as a B_(tuple) composed the info (B_('W'), B_('U') or B_('-')), the target flags, the key and the target name.

Item(B_(-T),B_(--inv-targets))
Show jobs that have given file as a target (either official or not).
This sub-command is only valid for file args.
.IP
If I_(--verbose), obsolete jobs are also listed in gray.
.IP
If I_(--porcelaine), the output is a B_(set) of jobs, each job being represented as a B_(tuple) composed of the rule name and the job name.

Item(B_(-u),B_(--trace))
Show an execution trace of the jobs.
Each entry is compose of a date, a tag and a file (or other info, depending on tag).
The tag is either a keyword, of a keyword with various attributes in ().
Accesses generating no new dep are not shown.
.IP
The main purpose is to provide an explanation for each dep and target.
.IP
If I_(--porcelaine), the output is generated as a B_(tuple) of entries, each entry being represented as a tuple composed of a date, a tag and a file (as described above), each being a B_(str).

SpecificOptions
Item(B_(-p),B_(--porcelaine))
In porcelaine mode, information is provided as an easy-to-parse python object.
Also,reported files are relative to the root of the repo, not the current workind dir.
.IP
If argument is a job, the output is as described for each sub-command.
unless mentioned otherwise, if arguments are files, the output is a B_(dict) whose keys are the arguments and values are as described above.

.SH "EXIT STATUS"
.LP
B_(lshow) exits with a status of zero if the asked targets could be shown.
Else it exits with a non-zero status:
.LP
Item(B_(1))  some targets could not be shown
Item(B_(2))  internal error, should not occur
Item(B_(4))  server could not be started
Item(B_(5))  internal repo state was inconsistent
Item(B_(7))  adequate permissions were missing, typically write access
Item(B_(8))  server crashed, should not occur
Item(B_(10)) some syscall failed
Item(B_(11)) bad usage : command line options and arguments coul not be parsed
Item(B_(12)) bad repo version, repo need to be cleaned, e.g. with B_(git clean -ffdx)

.SH EXAMPLES
.LP
V_(lmake a_file)
.LP
V_(lshow -i a_file)

.SH ENVIRONMENT
.LP
The content of B_($LMAKE_VIDEO) is processed as if provided with the I_(--video) option.

.SH FILES
CommonFiles

Footer
