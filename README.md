<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Why open-lmake

Open-lmake is a generic and [fearless](doc/fearless.md) build system.
It is like `make`, except that it is practical, versatile, scalable and reliable.

It is reliable : never type `make clean` ever again.  
It is reproducible : never complain "does not work on my machine" ever again.

It is unique in that:

- Dependencies are **automatically detected** through command spying (at libc or system call level).
- Rules are selected through full regular expressions allowing a complete decoupling between the flow (as described in the config file) and the project.

Also:

- Performance is excellent despite these apparently heavy features. Visit [benchmarks](doc/benchmark.md).
- Config file (called `Lmakefile.py`) is plain Python (no DSL!).
- `slurm` and `SGE` support.
- Fully traceable. You can retrieve logs, know why a job was run, who asked for it, when it was run, etc.

For a quick comparison with well-known tools (`make`, `ninja`, `bazel`, `CMake`) , visit [quick comparison](doc/quick_comparison.md).

According to [Build Systems à la carte](https://dl.acm.org/doi/pdf/10.1145/3236774),
open-lmake is correct, monadic, restarting, self-tracking, keeps verifying traces, implements parallel execution, does early cutoff and implements dynamically allocated keys.
Optionally, it can keep constructive traces and be a cloud build system.

Its major goal is reliability and repeatability, stealing ideas from [Building System Rules and Algorithms](https://gittup.org/tup/build_system_rules_and_algorithms.pdf).
However our conclusions about implementation are different.
To understand why, visit [why sticking to alpha algorithms](doc/why_stick_to_alpha_algo.md).

If you are not sure you need open-lmake, visit [who needs open-lmake](doc/who_needs_open-lmake.md).

The full documentation is [here](https://cesar-douady.github.io/open-lmake/).

# Installation

To install open-lmake under ubuntu 22.04 or ubuntu 24.04, you can use a pre-compiled package:

```
sudo add-apt-repository ppa:cdouady/open-lmake
sudo apt update
sudo apt install open-lmake
```

Or refer to [full installation instructions](doc/install.md).

# First steps

You have man pages for all commands directly accessible if installed with the debian package or if `$MANPATH` is set as explained above.

However, the simplest way is to give it a try:

- You can copy [examples/hello\_world.dir](examples/hello_world.dir) at some place (you can ignore `tok` and `tok.err` files if present).
- It is important to copy the example in a fresh directory because the script modifies the sources to mimic a session.
- In particular, if you want to restart, re-copy the example to a fresh directory.
- `cd` into it.
- Run `./run`
- Inspect `Lmakefile.py` and `run`, they are abundantly commented.

Once you have understood what is going on with `hello_world`, you can repeat these steps with the second example [examples/cc.dir](examples/cc.dir).

# Brief overview

Open-lmake does the following:

- [Automatic dependency tracking](doc/src/autodep.md):
	- System activity is tracked to detect file accesses (reads & writes), as with [rattle](https://github.com/ndmitchell/rattle).
	- Dependencies are automatically added upon read.
	- No need for any kind of pre-analysis.
	- Even dependencies to non-existing files are kept (e.g. tried files in include path when compiling C/C++, or tried modules in PYTHONPATH when importing with python, etc.).
- Targets need not be explicitly listed in advance:
	- Target matching is reg-expr based
	- Not only the very poor single `%` of make.
	- Several stems (variable parts) are possible.
	- Dependencies are generated using f-strings (or even any python function)
- Rules may have several targets:
	- Usually target list is known before job is executed.
	- But target reg-expr is also supported for rules such as `unzip` that generate a bunch of targets whose precise list depends on input content.
- Makefile (called `Lmakefile.py`) is based on Python3.6 (& upward):
	- No reason to invent a new language (no DSL).
	- Each rule is a class.
	- Can leverage loops, conditions, inheritance, ... full Python power.
	- Job scripts can be either shell scripts or Python functions.
	- Very readable.
- Handles parallelism & remote execution
	- Either locally or by submitting jobs to slurm or SGE.
- Inter-repository cache (experimental):
	- A job run in a repository can be made available to other repositories owned by other users.
	- Jobs can be executed in containers in which the repository root directory appears identical in all actual repositories, allowing to transport results even when they contain absolute paths.
- Extremely fast:
	- Everything is cached, in particular dependencies so that the discovery process is fully run once, then dependencies are adjusted at each run.
	- Up-to-date analysis is content based rather than date based, so that when a target is remade identically to its previous content, dependent jobs are not rebuilt.
	- Strings are interned, their values are only used to import (e.g. when tracking dependencies) and export (e.g. for reports or job execution).
	- Can launch about 1000 jobs per second if connected to an adequate slurm-based farm and about 300 on the local host.
	- Globally, performances are about the same as `ninja` while providing much better guarantees.
- Extremely memory efficient:
	- Keeping the state of all dependencies is inherently very expensive.
	- Thorough efforts have been made to keep this book keeping-minimal.
	- Typically, an existing dependency occupies 16 bytes, a non-existing one only 4 bytes, so there can be 100's of millions of them.
	- Globally, memory efficiency is in the same ball park as `ninja` while providing much better guarantees.
- Generally speaking 1.000.000 files/jobs can be handled with no burden.
- Oriented towards reproducibility:
	- Coupled with the source control system (usually `git`) and refuses to rely on untracked files.
	- Masks out the user environment (e.g. $PATH) to provide the one described by the configuration.
- Opiniated:
	- Default flow is robust and reliable.
	- If you have good reasons, many escapes hatches are available.
- and more...

# Developers

If you want understand, read the code, edit it, implement a new feature or fix a bug, please refer to the [developers](doc/developers.md) file first.
