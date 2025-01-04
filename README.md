# Purpose

Open-lmake is a generic, fearless build system.
It is like `make`, except that it is practical, versatile, scalable and reliable.

According to [this study](https://dl.acm.org/doi/pdf/10.1145/3236774),
open-lmake is correct, monadic, restarting, self-tracking, keeps verifying traces, implements parallel execution, does early cutoff and implements dynamically allocated keys.
Optionally, it can keep constructive traces and be a cloud build system.

## who needs open-lmake?

If you experience any of the following time-wasting situations:

- Your computer executes some stuff you are sure or almost sure (say 99%) is useless.
- You use a single computer although you have access to a compute farm.
- You have not parallelized what can be done because it would take longer to write a sound parallelizing script.
- You have written scripts while you felt that a sound organization of your workspace would have avoided such a need (e.g. preparing a clean tmp directory and cleaning it at the end).
- You have written ad-hoc scripts to handle rather generic situations (e.g. detecting a file changed to save work in case it did not).
- You navigate through your fancy workspace and each step is like a criminal investigation because you don't have the necessary forward & backward pointers (w.r.t your flow).
- What works in your colleague & friend's repository does not work in yours or vice versa.
- What worked last week is broken today.
- You forgot how to use this script you wrote last week because since then, your mind was overloaded with 1000 other stuff.
- You need to use this complex tool (e.g. a CAD tool) about which only the specialist has the know-how, and unluckily, he is on vacation this week.
- You have to wait while your repository is busy because your flow is running (e.g. compilation is on-going, unit tests are running, ...)
  and you know if you edit source files at the same time, nothing good will happen.

Then you need a tool to help you.

Such a tool may be a continuous integration tool or a build system.
Open-lmake is such a tool that will provide apt solutions to all the above-mentioned points.

If your development is mainstream (such as writing an app in C, Python or Java) and you are seeking a fast on-boarding tool, you may find some other tools adequate.
These may be for example `Cmake`, `meson` or `PyBuilder`. They will save you from writing common case rules, call the compiler with the right options etc.

In other cases (e.g. you use CAD tools, you write embedded code, the complexity of your flow comes from testing & evaluating KPI's rather than compiling, etc.), you need a more generic tool.
Among them, you may consider, with the described limitations (only major ones are listed):

- `make`
	- Unreliable          : for example it will fail to rebuild a target if the recipe is modified.
	- Fancy configuration : it is mostly impossible to write an easily readable makefile as soon as your flow is anything else than a straightforward compile-link one.
	- Dependencies        : managing dependencies is a nightmare, and even with dedicated helpers, it is always partial, fragile and complex.
	  This is so true that most makefiles do not handle parallelism because of hidden dependencies (i.e. dependencies that are not explicit in the makefile).
	- Not scalable        : it will poorly perform above ~100 rules and 10k files
	- Too rigid           : the only genericity is through the use of a single wildcard (`%`). This is far from enough when the flow contains anything but the simplest compile-link cases.
- `ninja`
	- advertises itself as a back-end only tool. It lacks a front-end tool to provide a user friendly tool.
	- the most common front-ends, `cmake` and `meson`, lack genericity.
- `bazel`
	- The flow is not complete in the sense that it is not meant to be fully maintained by the user.
	  As per its documentation: "It is common for BUILD files to be generated or edited by tools".
	- Dependencies must be manually handled with care. Again, the documentation states:
		+ BUILD file writers must explicitly declare all of the actual direct dependencies for every rule to the build system, and no more.
		+ Failure to observe this principle causes undefined behavior: the build may fail, but worse, the build may depend on some prior operations,
		  or upon transitive declared dependencies the target happens to have.
		  Bazel checks for missing dependencies and report errors, but it's not possible for this checking to be complete in all cases.
	- This guarantee is virtually impossible to be provided by a user, leading to incomplete dependency graph and unreliable results.
- `snakemake`
	- lacks the management of implicit dependencies (cf. `make` above).
	- relies on dates for up-to-date analysis, which is much less efficient than relying on checksums in various situations.

Or you can use open-lmake which, thanks to the elements described below, addresses all above-mentioned limitations.

## Brief overview

Open-lmake does the following:

- Automatic dependency tracking:
	- System activity is tracked to detect file accesses (reads & writes), as with rattle.
	- Dependencies are automatically added upon read.
	- No need for any kind of pre-analysis.
	- Even dependencies to non-existing files are kept (e.g. tried files in include path when compiling C/C++, or tried modules in PYTHONPATH when importing with python, etc.).
- Target matching is reg-expr based:
	- Rather than the very poor `%` of make.
	- There can be several stems.
	- Dependencies are generated using f-strings (or even any python function), very flexible.
	- As a consequence, targets need not be listed in advance.
- Rules may have several targets:
	- Usually target list is known before job is executed.
	- But target reg-expr is also supported for rules such as `tar -x` that generate a bunch of targets whose precise list depends on input content.
- Handle parallelism & remote execution
	- Either locally or by submitting jobs to slurm or SGE.
- Inter-repository cache:
	- a job run in a repository can be made available to other repositories owned by other users.
	- jobs can be executed in containers in which the repository root directory appears identical in all actual repositories, allowing to transport results even when they contain absolute paths.
- It is extremely fast:
	- Everything is cached, in particular dependencies so that the discovery process is fully run once, then dependencies are adjusted at each run.
	- Up-to-date analysis is based on checksums rather than on date, so that when a target is remade identically to its previous content, dependent jobs are not remade.
	- All the internal engine is based on id's, strings are only used to import (e.g. when tracking dependencies) and export (e.g. for reports or job execution).
	- It can launch about 1000 jobs per second if connected to an adequate slurm-based farm.
- It is extremely memory efficient:
	- Keeping the state of all dependencies is inherently very expensive.
	- Thorough efforts have been made to keep this book keeping minimal.
	- Typically, an existing dependency occupies 16 bytes, a non-existing one only 4 bytes, so there can be 100's of millions of them.
- Generally speaking 1.000.000 targets can be handled with no burden.
- Makefile (called `Lmakefile.py`) is based on Python3.6 (& upward):
	- No reason to invent a new language.
	- Each rule is a class.
	- Can leverage loops, conditions, inheritance, ... all Python power.
	- Job scripts can be either shell scripts or Python functions.
	- Very readable, no awkward automatic variables such as with `make`
- Open-lmake is oriented towards reproducibility:
	- It tracks the source control system (usually `git`) and refuses to rely on data that are not tracked.
	- It masks out the user environment to provide the one described by the configuration.
	- You can override these rules as an opt in.
- and more...

# First steps

you can find a reference documentation in `doc/lmake.html` (`/usr/share/doc/open-lmake/html/lmake.html` if installed with the Debian package)

you have man pages for all commands directly accessible if installed with the debian package or if `$MANPATH` is set as explained above

However, the simplest way is to give it a try:

- you can copy `examples/hello_world.dir`(`/usr/share/doc/open-lmake/examples/hello_word.dir` if installed with the Debian package) at some place (you can ignore `tok` and `tok.err` files if present).
- it is important to copy the example in a fresh directory because the script modifies the sources to mimic a session
- in particular, if you want to restart, re-copy the example to a fresh directory.
- `cd` into it
- run `./run.py`
- inspect `Lmakefile.py` and `run.py`, they are abundantly commented

Once you have understood what is going on with `hello_world`, you can repeat these steps with the second example `cc` (`examples/cc.dir`).

# Installation

To install open-lmake, please refer to the [INSTALL](INSTALL.md) file.

# Developers

If you want understand, read the code, edit it, implement a new feature or fix a bug, please refer to the [DEVELOPERS](DEVELOPERS.md) file.
