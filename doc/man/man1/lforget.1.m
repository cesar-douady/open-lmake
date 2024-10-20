Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

Header(lforget,forget about the state of a file or job)

.SH DESCRIPTION
.LP
B(lforget) is used to forget about some of the history.
In its first form, subsequent C(lmake) commands will forget about the build history of the mentioned targets.
As a consequence, these targets will appear out-of-date.

This is exceptionally useful in situations where B(lmake)'s hypotheses are broken :
Bullet A file outside the repository is modified. Because B(lmake) does not track modifications outside the repository, it canot rerun jobs that must be rerun, this must be done manually.
Bullet An error is transient. Another option is to launch B(lmake -e) which asks to consider jobs in error as out-of-date.

ClientGeneralities()

ClientOptions()

SpecificOptions

Item(B(-d),B(--deps))
In addition to being out-of-date, job will forget about hidden deps.

Item(B(-e),B(--error))
This is a global option and no targets must be specified.
Mark all jobs in error as out-of-date.
.IP
This is useful when you have seen transient errors.
Just rerunning jobs in error will wash these.
If you need finer control, use more specific options with associated targets.

Item(B(-r),B(--resources))
This is a global option and no targets must be specified.
Mark jobs that have been successfully built with old resources as out-of-date.
This is useful in scenarios such as the following one :
.RS
	Bullet You have run jobs J1 and J2. J1 completed successfully but J2 lacked some memory and ended in error.
	Bullet Then you have modified the allocated memory, increasing J2's memory and decreasing J1's memory because you think it is better balanced this way.
	Bullet Then you remade both jobs. J2 reran because it was in error and now completes successfully. J1 did not rerun because it was ok and modifying some resources would not change the result.
	Bullet However, it could be that now J1 does not have enough memory any more. It is not a problem in itself because its content is correct, but it may not be reproducible.
	Bullet You want to make sure your repository is fully reproducible.
	Bullet In that case, you run B(lforget -r). J1 will rerun because it was not run with the newer resources, as if its command was modified. J2 will not because it has already been run since then.
.RE

Item(B(-t),B(--targets))
In addition to being out-of-date, job will forget about star targets.


.SH FILES
CommonFiles

.SH NOTES
.LP
Where C(lmark) is used to instruct OpenLmake not to run jobs that it considers out-of-date, B(lforget) is used the opposite way, to instruct OpenLmake to run jobs it considers up-to-date.

Footer
