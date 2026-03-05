Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023-2026 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

Header(lmark,mark jobs and files with specific attributes)

.SH DESCRIPTION
.LP
There are 2 possible marks :
Item(B_(freeze))
This mark prevents jobs from being run.
Instead its targets (as built during the last run) behave as sources.
If no job generated a file provided in arguments, this file behaves as a source.
Item(B_(no-trigger))
This mark prevents OpenLmake to trigger builds of dependent jobs after modifications of mentioned files.
However, this applies only if job was last run with success.

.LP
Jobs and files so marked and that have been used are repeated in the summary.
This precaution is taken because such presence goes against repeatability and should be suppressed before commiting the repo.

.LP
Frozen jobs are useful to run a flow from A to `B'.
To do that you type B_(lmark -af A) followed by B_(lmake `B').

.LP
No trigger files are useful when working on a foundation file (called I_(utils.h) hereinafter).
It is common to work on I_(utils.h) to add new features.
During such developments, it is practical to run a test suite that checks the new feature.
But to run this test suite, you probably need a lot a derived files that depend on I_(utils.h) without using the new feature
(e.g. I_(a.o), I_(b.o), I_(c.o) which are not the focus of your development).
It is then a waste of time and resources to rebuild these derived files for each tiny modification.
An easy way is :
Bullet Mark I_(utils.h) with B_(no-trigger).
Bullet Write the new test that uses this new feature.
Bullet Modify I_(utils.h) to implement the new feature.
Bullet Run your test.
Files not using the new feature will likely be successfully built and will not be uselessly rebuilt.
File using the new feature will likely fail and will be usefully rebuilt.
If no error is generated, it is easy to run C_(rm) or C_(lforget) to force rebuild.
Bullet Loop on edit/test until feature is ok.
Bullet When you are satisfied with the new feature, suppress the no-trigger mark and rerun your test suite to ensure repeatability.

ClientGeneralities()

ClientOptions(job)

SubCommands
Item(B_(-a),B_(--add))    mark mentioned jobs or files.
Item(B_(-d),B_(--delete)) remove mark from mentioned jobs or files.
Item(B_(-l),B_(--list))   list marked files. This is a global sub-command and not file/job must be provided.
Item(B_(-c),B_(--clear))  delete all marks. This is a global sub-command and not file/job must be provided.

SpecificOptions
Item(B_(-f),B_(--freeze))     mark is freeze.
Item(B_(-t),B_(--no-trigger)) mark is no-trigger.

.SH "EXIT STATUS"
.LP
B_(lmark) exits with a status of zero if the asked targets could be marked.
Else it exits with a non-zero status:
.LP
Item(B_(1))  some targets could not be marked
Item(B_(2))  internal error, should not occur
Item(B_(4))  server could not be started
Item(B_(7))  adequate permissions were missing, typically write access
Item(B_(8))  server crashed, should not occur
Item(B_(10)) some syscall failed
Item(B_(11)) bad usage : command line options and arguments coul not be parsed
Item(B_(12)) bad repo version, repo need to be cleaned, e.g. with B_(git clean -ffdx)

.SH EXAMPLES
.LP
V_(lmark -fa a_dep)
.LP
V_(lmake depend_on_a_dep) # a_dep not remade
.LP
V_(lmake -fc)
.LP
V_(lmake depend_on_a_dep) # a_dep remade if necessary

.SH FILES
CommonFiles

.SH NOTES
.LP
Where C_(lforget) is used to instruct OpenLmake to run jobs that it considers up-to-date, B_(lmark) is used the opposite way, to instruct OpenLmake not to run jobs it considers out-of-date.

Footer
