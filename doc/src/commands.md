<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Commands

Open-lmake is a package containing several commands.

The full documentation of these commands can also be obtained by running `man <command>`.

## command execution

Most commands (`ldebug`, `lforget`, `lmake`, `lmark` and `lshow`) do not execute directly but instead connect to a server, or launch one if none already run.

The reason is that although several of these commands can run at the same time (including several times the same one, in particular several `lmake`), they all must run in the same process to
stay coherent.

Among these commands, all of them except `lmake` run mostly instantaneously. So the serverr mostly exist to be able to run any of these commands while one or several instances of `lmake` are
already running.

The (unique) server is created automatically when necessary and dies as soon as no more needed.
So under normal situations, one does not have to even be aware of the existence of such a server.

Although the server has been carefully coded to have a very low start overhead, it may happen in rare circumstances, though, that pre-launching a server (`<installation dir>/_bin/lmakeserver`)
leads to improved performances by avoiding to relaunch a server for each command.
In such cases, the server must be run with no argument.

However, if, under a particular cicumstance, the server must be killed, best is to use signal 1 (SIGHUP) or 2 (SIGINT) as this will force the server to smoothly kill all running jobs.
Other signals are not managed and will lead to the server dying abruptly, potentially leaving a lot of running jobs.
This has no semantic impact as these jobs will be considered out-of-date and will rerun, but may incur a waste of resources.

## commands to control build execution

These commands are meant to be run by the user outside jobs.
They are:

| Command                                              | Short description                                                  |
|------------------------------------------------------|--------------------------------------------------------------------|
| [`lautodep`](man/man1/lautodep.html)                 | run a script in an execution environmeent while recording accesses |
| [`lcollect`](man/man1/lcollect.html)                 | remove obsolete files and dirs                                     |
| [`ldebug`](man/man1/ldebug.html)                     | run a job in a debug environement                                  |
| [`ldircache_repair`](man/man1/ldircache_repair.html) | repair a broken repo                                               |
| [`lforget`](man/man1/lforget.html)                   | forget history of a job                                            |
| [`lmake`](man/man1/lmake.html)                       | run necessary jobs to ensure a target is up-to-date                |
| [`lmark`](man/man1/lmark.html)                       | mark a job to alter its behavior w.r.t. `lmake`                    |
| [`lrepair`](man/man1/lrepair.html)                   | repair a broken repo                                               |
| [`lshow`](man/man1/lshow.html)                       | show various informations of a job                                 |
| [`xxhsum`](man/man1/xxhsum.html)                     | compute a checksum on a file                                       |

## commands to interact with open-lmake from within jobs

These commands are meant to be run from within a job.
They are:

| Command                                    | Short description                                                         |
|--------------------------------------------|---------------------------------------------------------------------------|
| [`lcheck_deps`](man/man1/lcheck_deps.html) | check currently seen deps are all up-to-date and kill job if not the case |
| [`ldecode`](man/man1/ldecode.html)         | retrieve value associated with a code                                     |
| [`ldepend`](man/man1/ldepend.html)         | generate deps                                                             |
| [`lencode`](man/man1/lencode.html)         | retrieve/generate a code associated with a value                          |
| [`lrun_cc`](man/man1/lrun_cc.html)         | run a compilation, ensuring include dirs and lib dirs exist               |
| [`ltarget`](man/man1/ltarget.html)         | generate targets                                                          |
