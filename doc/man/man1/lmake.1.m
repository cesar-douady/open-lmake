Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023-2026 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

Header(lmake,a reliable & scalable tool to execute a full flow)

.SH DESCRIPTION
.LP
The B_(lmake) utility determines automatically which pieces of a flow need to be executed and launch them.
This is based in the historical I_(make) program initially written by Stuart Feldman in 1976 with notable differences :
Bullet
B_(lmake) is reliable.
It ensures that necessary jobs are run in adequate order so that the asked files are in the same state as if all derived files were removed and all jobs were run.
In particular, this includes automatic discovery of actual deps.
Bullet
B_(lmake) is scalable.
Millions of files can be derived easily (job execution may require heavy resources, but B_(lmake) will stay light).
Bullet
The makefile is called I_(Lmakefile.py) and is written in python3.
Bullet
Rule matching is based of regular expressions, not just a single wildcard (% with I_(make)).
Bullet
Recipies can be written in C_(bash) or C_(python).
Bullet
Several B_(lmake) commands can be run simultaneously in the same dir.
Jobs needed by several of them will be shared.
Simlarly sources files can be edited without risks while B_(lmake) is running.
Derived files will be generated according to the state of the source files at the time the command was launched.
If this is not possible because an old version of a source file need to be accessed after it has been modified, an error will be generated.
Rerunning the same B_(lmake) command will take into account the new content of such source files.
Bullet
File states are based on content (using a checksum) rather than dates.
This makes it possible to avoid running a dependent job if a file has been reconstructed identically to its previous content.
.LP
To reach these goals, B_(lmake) maintains a state in the I_(LMAKE) dir, instrument jobs (to catch all file accesses) during their execution
and launches a daemon (called B_(lmake_server)) that can be shared between several concurrent invocations of B_(lmake).
.LP
B_(lmake) generates an output line for each significant event :
Bullet
When a job starts if it is long (duration that qualifies a job as long is configurable). This start line is deemed of secondary importance.
Bullet
When a job terminates.  Such lines are followed by the content of stderr if any and status is known.
Typically the job status is not known at the end of execution when a new dep was discovered that turned out to be out-of-date.
In that case, the dep is rebuilt, and if the new content is identical to the old one, B_(lmake) can decide that the job was run with up-to-date deps.
Bullet
When the job status is finally known, if it was not known when it terminated.
Such lines are followed by the content of stderr if any.
.LP
During execution, if launched from a terminal, B_(lmake) also generates a progression status in the title bar.
This progression status line contains :
Bullet The number of executed jobs (split between useful, rerun and hit)
Bullet The number of queued jobs, i.e. jobs that are waiting for resources to be run
Bullet The number of waiting jobs, i.e. jbos that are waiting for deps to be run
.LP
At the end of the execution, if the asked targets are not successfully generated, a summary is generated reminding the errors (with a configurable max, 20 by default)
and the stderr of the first error. Intermediate targets are deemed of secondary importance.
.LP
Before processing arguments, B_(lmake) prepends the content of B_($LMAKE_ARGS), separated by spaces.
This eases the management of user preferences.
For example, a user may like to systematically pass the I_(--archive) and I_(--keep-tmp) options, in which case they can set B_(LMAKE_ARGS)=B_(-a -t).

ClientGeneralities(color)

ClientOptions(job,color)

SpecificOptions

Item(B_(-a),B_(--archive))
Ensure all intermediate files are up-to-date, in addition to the asked targets.
This is useful for example if you want to archive a fully built repo.

Item(B_(-b) I_(value),B_(--backend)=I_(value))
Pass value to backend (cf. backend documentation for each backend).
This is used for example to pass a partition or specificities to the slurm backend for a particular command.
Note that backend only impacts resources and scheduling, not the content of the targets, so specifying such an option does not hurt repeatability.

Item(B_(-c) I_(method),B_(--cache-method)=I_(method))
This options specifies how to manage the cache if any is mentioned in a rule.
The default value is I_(plain).
.LP
Values can be:
Bullet I_(none)     : the cache is not accessed at all.
Bullet I_(download) : job results are downloaded from the cache but the cache is not updated when it misses an entry.
Bullet I_(upload)   : job results are not downloaded from the cache, but new results are uploaded and if an entry already exists, coherence is checked.
Bullet I_(plain)    : job results are downloaded from the cache in case of hit, and cache is updated in case of miss.

Item(B_(-d),B_(--essential-deps))
When checking if a job is up-to-date, non-essential deps are ignored.
Note that other criteria (e.g. job is in error and B_(-e) option is given) are still checked.

Item(B_(-D),B_(--no-deps))
When checking if a job is up-to-date, deps are ignored.
Note that other criteria (e.g. job is in error and B_(-e) option is given) are still checked.

Item(B_(-e),B_(--forget-old-errors))
Assume previous errors (before this command) are transient.
Contrarily to the B_(lforget -e) command, this only concerns this execution, not subsequent ones.

Item(B_(-E) I_(value),B_(--ete)=I_(value))
Pass value as the estimated time of the command.
Format is the same as reported job execution times : a succession of numbers (with optional dot) followed by B_(d) (days), h (hours), m (minutes) or B_(s) (seconds), e.g. B_(2h30m).
This value is used for job scheduling when backend supports it (at least local backend does).
ETE is an aeronautic term meaning B_(Estimated Time Enroute).

Item(B_(-j) jobs,B_(--jobs)=I_(jobs))
When this option is used, B_(lmake) will limit the overall number of simultaneous jobs to I_(jobs) per backend.
If several B_(lmake) commands run simultaneously, a job cannot be launched on behalf of a given command if the number of running jobs is not less than its associated I_(jobs).

Item(B_(-o),B_(--live-out))
Normally, B_(lmake) does not output the stdout of jobs (such stdout is accessible with the B_(lshow -o) command).
However, sometimes it is practical to have the output while jobs are running.
Generating such output for all jobs would produce an intermixed flow of characters of all jobs running in parallel making such an output unreadable.
When this option is used, only the jobs directly producing the asked targets have their output generated on the output of B_(lmake).
Because most of the time there is a single target, this ensures that there is a single job generating its output, avoiding the intermixing problem.

Item(B_(-N) I_(nice_val),B_(--nice)=I_(nice_val))
Apply the specified nice value to all jobs.

Item(B_(-m) I_(count),B_(--max-runs)=I_(count))
Ask B_(lmake) to limit number of runs for any job to this number.
This constraint must be enforced together with the B_(max_runs) rule attribute, i.e. the min of these 2 constraints is used.
This is useful to observe a job while it is supposed to rerun.
.LP
Contrarily to the B_(--max-submits) options, caches accesses are not counted when counting runs.

Item(B_(-M) I_(count),B_(--max-submits)=I_(count))
Ask B_(lmake) to limit number of submits for any job to this number.
This constraint must be enforced together with the B_(max_submits) rule attribute, i.e. the min of these 2 constraints is used.
This is useful to observe a job while it is supposed to rerun.
.LP
Contrarily to the B_(--max-runs) options, caches accesses are counted when counting submits.

Item(B_(-r) I_(count),B_(--retry-on-error)=I_(count))
Ask B_(lmake) to retry jobs in case of error.
This is useful for unattended execution (e.g. nightly regressions) when system reliability is not enough to guarantee correct execution at the desired level.
.LP
Contrarily to B_(-e), this concerns all jobs.
Previous errors are counted as 1 trial.
Hence, B_(-r) encompasses B_(-e), but retries more jobs in error.

Item(B_(-I),B_(--no-incremental))
With this option, jobs that had existing incremental targets during their last run are not trusted and they are rerun by first erasing all targets.
Also, cache entry, if any is specified, are not downloaded if they had been built with existing targets.

Item(B_(-l),B_(--local))
With this option, jobs are launched locally (i.e. using the I_(local) backend) instead of the backend mentioned in the rule.
Note that if 2 B_(lmake) commands with different values for this option are running simultaneously, in case a job is necessary for both, it may be launched locally or remotely.
The originally targetted backend is in charge of mapping required resources mentioned in the rule to local resources understandable by the local backend.

Item(B_(-s),B_(--source-ok))
Normally, B_(lmake) refuses to launch a job that may overwrite a source.
With this option, the user instructs B_(lmake) that this is allowed.

Item(B_(-t),B_(--keep-tmp))
Normally, B_(lmake) washes the temporary dir allocated to a job at the end of job execution.
With this option, the user instructs B_(lmake) to keep the temporary dirs, as if the I_(keep_tmp) attribute was set for all rules.
The kept temporary dir can be retreived with B_(lshow -i).

Item(B_(-v),B_(--verbose))
Enable the generation of some execution information from backend.
This is not done systematicly as this may incur a performance hit.
These information are available by using B_(lshow -i).

.SH OUTPUT
.LP
While B_(lmake) runs, it outputs a log.
This log is also recorded in I_(LMAKE/outputs/<start date>) with the following differences:
Bullet It is not colored.
Bullet Reported files are relative to the root of the repo, not to the current working dir where the B_(lmake) command has been launched.
.LP
The log contains a line, possibly followed by attached information when the following events occur :
Bullet A job is started, if the job duration is longer than the I_(start_delay) attribute of the rule.
Bullet A job is completed.
Bullet A job status is known, while it was not known when it completed.
Bullet A source file has been seen as modified.
Bullet A frozen file or a target of a frozen job is needed.
.LP
Once the build process is complete, a summary is generated with :
Bullet The frozen files and jobs that we necessary to carry out the build.
Bullet The jobs in error (the first of them is accompanied with its stderr).

.SH "EXIT STATUS"
.LP
B_(lmake) exits with a status of zero if the asked targets could be built or were already up-to-date.
Else it exits with a non-zero status:
.LP
Item(B_(1))  some targets could not be built
Item(B_(2))  internal error, should not occur
Item(B_(3))  I_(Lmakefile.py) could not be read or contained an error
Item(B_(4))  server could not be started
Item(B_(5))  internal repo state was inconsistent
Item(B_(6))  repo need to be cleaned, e.g. with B_(git clean -ffdx)
Item(B_(7))  adequate permissions were missing, typically write access
Item(B_(8))  server crashed, should not occur
Item(B_(9))  repo need to be steady (no on-going lmake running)
Item(B_(10)) some syscall failed
Item(B_(11)) bad usage : command line options and arguments coul not be parsed
Item(B_(12)) bad repo version, repo need to be cleaned, e.g. with B_(git clean -ffdx)

.SH ENVIRONMENT
.LP
The content of B_($LMAKE_ARGS) is prepended to command line arguments.
.LP
The content of B_($LMAKE_VIDEO) is processed as if provided with the B_(--video) option.
.LP
Unless explicitly asked in I_(Lmakefile.py), the environment is mostly ignored when B_(lmake) is run, i.e. it is not passed to the jobs.
The goal is to improve repeatability by protecting jobs from the variability environment variables may cause.
In particular :
Bullet B_($HOME) is redirected to the root of the repo.
	This protects the job from all specificities stored in I_(.xxxrc) files in the home dir.
Bullet B_($LMAKE_ARGS), although used by B_(lmake), is not passed to jobs.
Bullet B_($PATH) is reset to the default path for the system, plus the OpenLmake bin dir.
Bullet B_($PYTHONPATH) is set to the OpenLmake lib dir.
Bullet Unless set to empty, B_($TMPDIR) is redirected to an isolated, empty dir which is cleaned up at the end of each job execution.
	This way, the job can freely use this dir and need not take care of clean-up.
.LP
Moreover, a few variables are set during job execution :
Bullet B_($JOB_ID) is set to an integer specific of a job.
	It does not change between executions, but may be different in different repo, even if strictly identical.
Bullet B_($SMALL_ID) is set to a as small as possible integer such that a different value is set for jobs running concurrently.
Bullet B_($SEQUENCE_ID) is set to a different value each time a job is run, they are never recycled.

.SH EXAMPLES
.LP
V_(lmake a_file)
.LP
V_(vi a_file) # a_file is guaranteed up-to-date.

.SH FILES
CommonFiles

Footer
