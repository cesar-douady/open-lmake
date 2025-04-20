<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Encoding and decoding

In some situations with heavily parameterized generated files, file names can become very long.
Think of the mere compilation of a C++ file `foo.c`.
You may want to specify:

- the optimization level through a `-O` argument
- Whether debug checks are enable through the definition of `NDEBUG`
- a trace level through the definition of a macro such as `TRACE_LEVEL`
- whether and how to instrument with `-fsanitize`
- whether some internal data are 32 or 64 bits
- whether to use a reference algorithm or an agressively optimized one used in production.
- ...

You may want to be able to generate any combination so as, for example, compare the output of any 2 of them for validation purpose.
You easily end up with an object file with a name such as `foo.O3.NDEBUG.TRACE_LEVEL=1.sanitize=address.32.o`.
Already 50 characters or so.
In a real projects, file names can easily be 200, 300, 400 characters long.

As long as the file name, with adequate shorthands such as using `TL` instead of `TRACE_LEVEL` fits within a few hundreds of characters, the situation is heavy but manageable.
But if you need, say 3000 characters to specify a file, then it becomes completely impractical.

When the configuration can be devised in advance, in a stable way, an efficient alternative is to create a file to contain it, which becomes a configuration name,
and just specify the configuration name in the generated file.

In the example above, you may have a file `test.opts` that contains options for testing and `prod.opts` that contains options for production.
then, your object file is simply named `foo.test.o` or `foo.prod.o`.

When it is not, the situation is more complex and you need to automatically generate these configuration files with reasonably short names.
A practical and stable way to generate short names is to compute a checksum on the parameters.
You then need a way to retrieve the original parameters from the checksum to generate the generated file (the `.o` file in our example).
In doing so, you must account for:

- robustness    : because such checksums are subject to the birthday paradox, you need either to deal with collisions are provide enough margin (roughly doubling the size) to avoid them.
- repeatability : your system must not prevent you from being able to repeat a scenario that was generated some days, weeks, months earlier.
- merging       : when you invent a name, think that some colleagues working on the same project may also invent names, and they may collide.
  Tools such as `git` are there to help you in this process, but your scheme must be git friendly.
- performance   : you must have a scheme that support as many code/value associations as necessary for your case, without spending most of its time searching for value when given a code.
- communication : ideally, you may want to signal a bug to a colleague by just telling him "build that target, and you see the bug".
  If the target refers to a code, he may need some further steps to create the code/value association, which goes against communication.

One way to deal with this case is to create a central database, with the following pros and cons:

- robustness    : collisions can easily be dealt with.
- repeatability : this is a probleme. When dealing with collisions, some codes change, which change old repository because the database is not itself versioned. This is a serious problem.
- merging       : no merging.
- perfomance    : accessing the data in a performant way is easy. Detecting modifications so that open-lmake can take sound decisions may be more challenging.
- communication : excellent, the database is shared
- installation  : you need a server, configure clients to connect to it, etc. it is some work
- maintainance  : as any central services, you may inadvertently enter wrong data, you need a way to administer it as it has the potential to block the whole team.

The `lencode`/`ldecode` commands (or the `lmake.encode`/`lmake.decode` fonctions) are there to address this question.

The principle of operation is the following:

- There are a certain number of files storing code/value associations. These are sources seen from open-lmake, i.e. they are normally managed by `git`.
- To keep the number of such files to a reasonably low level (say low compared to the overal number of sources), there are contexts, mostly used as a subdivision of files
- So, a file provides a certain number of tables (the contexts), each table associating some codes with some values
- These tables are stored in files as lines containing triplet : context, code, value
- When reading, `lencode`/`ldecode` are very flexible. The files may contain garbage lines, duplicates, collisions, they are all ignored.
  When 2 values are associated with the same code by 2 different lines, a new code is generated by lengthening one of them with further digites of the checksum computed on the value.
  When 2 codes are associated with the same value by 2 different lines, only one code is retained, the shorter of the 2 (or any if of equal length).
- When writing, `lencode`/`ldecode` are very rigid. File is generated sorted, with no garbage lines, nor duplicates, or collisions.
- When open-lmake starts and read a file, it write it back in its canonical form.
- When open-lmake runs, that `lencode` is used and generate new codes on the fly, additional lines are merely appended to the file.

This has the following properties:

- Information is under git. No further server, central database, management, configuration etc.
- repeatability is excellent. As long as you do not merge, your are insensitive to external activities.
  When merging, the probability of collision depends on the length of the used codes, which is under user control.
  Moreover, the length increasing automatically with collisions maintain the number of such collision to a reasonably low level, even in fully automatic mode.
- Merging is very easy : actually one need not even merge. The simple collision file generated by `git` can be used as is. This makes this tool very `git` friendly.
- Robustness is perfect : collisions are detected and dealt with.
- Coherence is perfect : seen from open-lmake, each association is managed as a source.
  If anything changes (i.e. a new value is associated with an old code or a new code is associated with an old value), the adequate jobs are rerun.
- Performance is very good as the content of the file is cached in a performance friendly format by open-lmake. And update to the file is done by a simple append.
  However, the file is sorted at every `lmake` command, making the content more rigid and the merge process easier.
- Associations files can be editing by hand, so that human friendly codes may be associated to some heavily used values.
  `lencode` will only generate codes from checksums, but will handle any code generated externally (manually or otherwise).
  In case of collision and when open-lmake must suppress one of 2 codes, externally generated codes are given preference as they believed to be more readable.
  If 2 externally generated codes collide, a numerical suffix is appended or incremented to solve the collision.

