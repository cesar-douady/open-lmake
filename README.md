# Purpose

Open-lmake is a generic, fearless build system.
It is like `make`, except that it is practical, versatile, scalable and reliable.

According to [Build Systems à la carte](https://dl.acm.org/doi/pdf/10.1145/3236774),
open-lmake is correct, monadic, restarting, self-tracking, keeps verifying traces, implements parallel execution, does early cutoff and implements dynamically allocated keys.
Optionally, it can keep constructive traces and be a cloud build system.

Its major goal is reliability and repeatability, stealing ideas from [Building System Rules and Algorithms](https://gittup.org/tup/build_system_rules_and_algorithms.pdf).
However our conclusions about implementation are different.
To understand why, visit [why sticking to alpha algorithms](doc/why_stick_to_alpha_algo.md).

It is unique in that:

- Dependencies are automatically detected through command spying (at libc or system call level).
- Rules are selected through full regular expressions allowing a complete decoupling between the flow (as described in the config file) and the project.
- A job can have a dynamically determined list of targets (as long as they all match a given regular expression).

Also:

- Performances are excellent despite these apparently heavy features.
- The config file (called `Lmakefile.py`) is plain Python.
- Jobs can optionally be launched through slurm or sge.
- It is fully traceable. You can retrieve logs, know why a job was run, who asked for it, when it was run, etc.

If you are not sure you need open-lmake, visit [who needs open-lmake](doc/who_needs_open-lmake.md).

For a quick comparison with well-known tools, visit [quick comparison](doc/quick_comparison.md).

# Brief overview

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
	- Very readable.
- Open-lmake is oriented towards repeatability:
	- It tracks the source control system (usually `git`) and refuses to rely on data that are not managed by the source control system.
	- It masks out the user environment to provide the one described by the configuration.
- Open-lmake is pragmatic:
	- default is a fully robust and reliable flow.
	- but you may have good reasons, not appearent to open-lmake to violate some of its principles.
	- you can opt in to do so, open-lmake is your slave, not the other way around.
- and more...

# First steps

you can find a reference documentation in `doc/lmake.html`.

you have man pages for all commands directly accessible if installed with the debian package or if `$MANPATH` is set as explained above.

However, the simplest way is to give it a try:

- you can copy `examples/hello_world.dir` at some place (you can ignore `tok` and `tok.err` files if present).
- it is important to copy the example in a fresh directory because the script modifies the sources to mimic a session
- in particular, if you want to restart, re-copy the example to a fresh directory.
- `cd` into it
- run `./run.py`
- inspect `Lmakefile.py` and `run.py`, they are abundantly commented

Once you have understood what is going on with `hello_world`, you can repeat these steps with the second example `cc` (`examples/cc.dir`).

# Installation

To install open-lmake, as a binary package or by compiling sources, please refer to [installation instructions](doc/install.md).

# Developers

If you want understand, read the code, edit it, implement a new feature or fix a bug, please refer to the [developers](doc/developers.md) file.
