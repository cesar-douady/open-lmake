<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Overview

The open-lmake build system automatically determines which pieces of a large workflow need to be remade and issues the commands to remake them.

Our examples show how to build and test C/C++ programs, as they are very common, but you can use
open-lmake with any programming language to run any phase of your CI/CD as long as these can be scripted.

Indeed, open-lmake is not limited to building programs.
You can use it to describe any task where some files must be (re)computed automatically from others whenever recomputing would lead to a different content.
Such situations include dep modifications, but also command modifications, dep list modifications,
apparition of an include file that was in the search path before an include file was actually accessed, symbolic link modifications, etc.

Symbolic links and hard links are also supported.

As far as open-lmake is concerned, repositories can be moved, archived and restored.
Tools are provided to help user achieve the same level of flexibility.

Open-lmake is designed to be scalable, robust and efficient.

By scalable, we mean that open-lmake can manage millions of files and tens of thousands of CPU hours with no difficulties,
so there is never any reason to have any kind of recursive invocation, open-lmake can handle the whole project flat.

By robust, we mean that open-lmake guarantees that if a job is not rerun, then rerunning it would lead to the same content (or a content that is equally legal).
This includes automatic capture of so called hidden deps (i.e. deps that are not explicitly stated in the rules, e.g. include files).

We also mean that open-lmake, as any software, it may have bugs.
Such bugs can lead to crashes and to pessimistic behavior (a job is rerun while it was not necessary).
But special attention has been devoted in its design to ensure that it is never optimistic (a job not being rerun while it should have been).
In case of any adverse event (lmake crashes or spurious system reboot),
open-lmake automatically recovers potentially corrupted states in a safe way to avoid having to remake the whole project because a few files are corrupted.
In extreme cases, there is a `lmake_repair` command that can recover all safe parts of a damaged repository.
Similarly the `ldir_cache_repair` command is able to recover a damaged (or manually manipulated) cache dir.

Note that open-lmake does not only recorver from its own flees, but also a lot of experience is embedded into it to work around system bugs.
This includes for example NFS peculiar notion of close-to-open consistency (which does not apply to the dir containing the file) or jobs spuriously disappearing.

By efficient, we mean that jobs are run in parallel, optionally using a batcher such as SGE or slurm, managing limited resources as declared in `Lmakefile.py`.
We also mean that open-lmake makes a lot of effort to determine if it is necessary to run a job (while always staying pessismistic).
Such effort includes checksum based modification detection rather than date based, so that if a job is rerun and produces an identical content, subsequent jobs are not rerun.
Also, open-lmake embed a build cache whereby jobs can record their results in a cache so that if the same run needs to be carried out by another user,
it may barely fetch the result from the cache rather than run the - potentially lengthy - job.

## Preparing and running lmake.

To prepare to use open-lmake, you must write a file called `Lmakefile.py` that describes the relationships between files in your workflow and provides commands for generating them.
This is analogous to the `Makefile` when using `make`.

When developping a program, typically, the executable file is built from object files, which in turn are built by compiling source files.
Then unit tests are run from the executable and input files, and the output is compared to some references.
Finally a test suite is a collection of such tests.

Once a suitable `Lmakefile.py` exists, each time you change anything in the workflow (source files, recipes, ...), this simple shell command:

```bash
lmake <my_target>
```

suffices to perform all necessary steps so that `<my_target>` is reproduced as if all steps leading to it were carried out although only necessary steps were actually carried out.
The `lmake` program maintains an internal state in the `LMAKE` dir to decide which files need to be regenerated.
For each one of those, it issues the recipes recorded in `Lmakefile.py`.
During job execution, `lmake` instruments them in order to gather which files are read and written in order to determine hidden deps and whether such actions are legal.
These information are recorded in the `LMAKE` dir.

You can provide command line arguments to `lmake` to somewhat control this process.

## Problems and Bugs

If you have problems with open-lmake or think you've found a bug,
please report it to the developers; we cannot promise to do anything but
we may well be willing to fix it.

Before reporting a bug, make sure you've actually found a real bug.
Carefully reread the documentation and see if it really says you can do
what you're trying to do.
If it's not clear whether you should be able to do something or not, report that too; it's a bug in the documentation!

Before reporting a bug or trying to fix it yourself, try to isolate it
to the smallest possible `Lmakefile.py` that reproduces the problem.
Then send us the `Lmakefile.py` and the exact results open-lmake gave you, including any error messages.
Please don't paraphrase these messages: it's best to cut and paste them into your report.
When generating this small `Lmakefile.py`, be sure to not use any non-free
or unusual tools in your recipes: you can almost always emulate what
such a tool would do with simple shell commands.
Finally, be sure to explain what you expected to occur; this will help us decide whether the problem is in the code or the documentation.

if your problem is non-deterministic, i.e. it shows up once in a while, include the entire content of the `LMAKE` dir.
This directory contains extensive execution traces meant to help developers to track down problems.
Make sure, though, to trim it from any sensitive data (with regard to your IP).

Once you have a precise problem, you can report it on [github](https://github.com/cesar-douady/open-lmake)

In addition to the information above, please be careful to include the version number of the open-lmake you are using.
You can get this information from the file `LMAKE/version`.
Be sure also to include the type of machine and operating system you are using.
One way to obtain this information is by running the command `uname -a`.
