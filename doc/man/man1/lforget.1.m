Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023-2026 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

Header(lforget,forget about the state of a file or job)

.SH DESCRIPTION
.LP
B_(lforget) is used to forget about some of the history.
In its first form, subsequent C_(lmake) commands will forget about the build history of the mentioned targets.
As a consequence, these targets will appear out-of-date.

This is exceptionally useful in situations where B_(lmake)'s hypotheses are broken :
Bullet A file outside the repo is modified. Because B_(lmake) does not track modifications outside the repo, it canot rerun jobs that must be rerun, this must be done manually.
Bullet An error is transient. Another option is to launch B_(lmake -e) which asks to consider jobs in error as out-of-date.

ClientGeneralities()

ClientOptions(job)

SpecificOptions

Item(B_(-d),B_(--deps))
In addition to being out-of-date, job will forget about hidden deps.

Item(B_(-r),B_(--resources))
This is a global option and no targets must be specified.
Mark jobs that have been successfully built with old resources as out-of-date.
This is useful in scenarios such as the following one :
.RS
	Bullet You have run jobs J1 and J2. J1 completed successfully but J2 lacked some memory and ended in error.
	Bullet Then you have modified the allocated memory, increasing J2's memory and decreasing J1's memory because you think it is better balanced this way.
	Bullet Then you remade both jobs. J2 reran because it was in error and now completes successfully. J1 did not rerun because it was ok and modifying some resources would not change the result.
	Bullet However, it could be that now J1 does not have enough memory any more. It is not a problem in itself because its content is correct, but it may not be reproducible.
	Bullet You want to make sure your repo is fully reproducible.
	Bullet In that case, you run B_(lforget -r). J1 will rerun because it was not run with the newer resources, as if its command was modified. J2 will not because it has already been run since then.
.RE

Item(B_(-t),B_(--targets))
In addition to being out-of-date, job will forget about star targets.

.SH "EXIT STATUS"
.LP
B_(lforget) exits with a status of zero if the asked targets could be forgotten.
Else it exits with a non-zero status:
.LP
Item(B_(1))  some targets could not be forgotten
Item(B_(2))  internal error, should not occur
Item(B_(3))  the B_(--resources) option was used and I_(Lmakefile.py) could not be read or contained an error
Item(B_(4))  server could not be started
Item(B_(5))  the B_(--resources) option was used and internal repo state was inconsistent
Item(B_(6))  the B_(--resources) option was used and repo need to be cleaned, e.g. with B_(git clean -ffdx)
Item(B_(7))  adequate permissions were missing, typically write access
Item(B_(8))  server crashed, should not occur
Item(B_(9))  the B_(--resources) option was used and repo need to be steady (no on-going lmake running)
Item(B_(10)) some syscall failed
Item(B_(11)) bad usage : command line options and arguments coul not be parsed
Item(B_(12)) bad repo version, repo need to be cleaned, e.g. with B_(git clean -ffdx)

.SH EXAMPLES
.LP
V_(lforget a_file)
.LP
V_(lmake a_file) # job is always run

.SH FILES
CommonFiles

.SH NOTES
.LP
Where C_(lmark) is used to instruct OpenLmake not to run jobs that it considers out-of-date, B_(lforget) is used the opposite way, to instruct OpenLmake to run jobs it considers up-to-date.

Footer
