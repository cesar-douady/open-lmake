<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Commands

Open-lmake is a package containing several commands.

The full documentation of these commands can also be obtained by running `man <command>`.

## commands to control build execution

These commands are meant to be run by the user outside jobs.
They are:

| Command                              | Short description                                                  |
|--------------------------------------|--------------------------------------------------------------------|
| [`lautodep`](man/man1/lautodep.html) | run a script in an execution environmeent while recording accesses |
| [`ldebug`](man/man1/ldebug.html)     | run a job in a debug environement                                  |
| [`lforget`](man/man1/lforget.html)   | forget history of a job                                            |
| [`lmake`](man/man1/lmake.html)       | run necessary jobs to ensure a target is up-to-date                |
| [`lmark`](man/man1/lmark.html)       | mark a job to alter its behavior w.r.t. `lmake`                    |
| [`lrepair`](man/man1/lrepair.html)   | repair a broken repo                                               |
| [`lshow`](man/man1/lshow.html)       | show various informations of a job                                 |
| [`xxhsum`](man/man1/xxhsum.html)     | compute a checksum on a file                                       |

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
