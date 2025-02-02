Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023-2025 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

Header(lcheck_deps,ensure deps collected so far in a OpenLmake job are up-to-date)

.SH DESCRIPTION
.LP
B(lcheck_deps) ensures that all deps accessed so far are up-to-date.
If this not the case, the job is killed, generating a rerun report, the deps are rebuilt and the job is rerun.
.LP
This is useful before starting a heavy computation. Typically, deps are computed and accessed before the heavy sequence and calling B(check_deps) allows to avoid
running a heavy computation with bad inputs. It is not a semantic problem as OpenLmake will realize the situation, rebuild the deps and rerun the job, but it may be a performance problem.
.LP
Imagine for exemple that you have to compile I(heavy.c) that includes a file I(generated.h) generated by the command I(generator).
Imagine then that you type B(lmake generated.h), you look at it, find that the file is syntactically correct but contains a bug.
You then modify I(generator) and because you are confident in your modification, you type B(lmake heavy.o).
.LP
OpenLMake will run the compilation of I(heavy.o), which lasts 10 minutes and discover that you need I(generated.h), which is out-of-date.
It will then rebuild I(generated.h) and rerun the compilation to I(heavy.o), another 10 minutes.
.LP
Suppose now that your compilation script separates the preprocessor (say 10 secondes) phase from the compilation (10 minutes) phase and call B(lcheck_deps) inbetween.
In that case, the first run will stop after preprocessing as B(lcheck_deps) will kill the job at that moment and the overall time will be 10 minutes 10 secondes instead of 20 minutes.

.SH OPTIONS
.LP
Item(B(-v),B(--verbose)) wait for server answer rather than letting job go speculatively while it is interrogated.
return code will be 1 if at least one dep is in error.
This is necessary, even without checking return code, to ensure that after this call, the directories of previous deps actually exist if such deps are not read (such as with B(lmake.depend)).

.SH NOTES
.LP
This is a (performance) problem only during the first run of I(heavy.o).
On subsequent runs, in particular during a typical edit-compile-debug loop, OpenLmake will know the dependencies and will launch jobs in the proper order.
But during the first run, it has no knowledge that I(heavy.c) actually includes I(generated.h).
.LP
Most often, when I(generated.h) is out-of-date, it is syntactically incorrect (for example it does not exist),
so the first run fails quite early (without spending its heavy optimization time).
.LP
In the case where I(generated.h) is rebuilt identically to its previous content, there will be no second run without B(lcheck_deps) call,
so B(lcheck_deps) has a (minor) adverse effect leading to an overall time of 10 minutes 10 seconds instead of 10 minutes.
.LP
Despite these remarks, there are case where B(lcheck_deps) is very welcome.

Footer
