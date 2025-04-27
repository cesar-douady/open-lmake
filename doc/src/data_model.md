<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Data model

Open-lmake manages 2 kinds of objects: files and jobs.

The reason they are different objects is that jobs may have several targets, so there is no way to identify a file and the job that generates it.

## Files

### names

Files are identified by their canonical name, as seen from the root of the repo.
For example, all these code snippets will access the same file `a/b` (assume no symbolic links for now):

```bash
cat a/b
cat a//b
cat ./a/b
cat a/./b
cat /repo/a/b # assume actual repo is /repo
cat c/../a/b  # assume c is a dir
cd a ; cat b
```

When spying the job (or when `ldepend`) is called, all these accesses will be converted to `a/b`.

Although targets are necessarily inside the repo, deps may be outside as some source dirs may declared outside the repo.
For such deps, their name is:

- the absolute path if the source dir is declared absolute
- the relative path if the source dir is declared relative

### Symbolic links

Open-lmake manages the physical view of the repo.
This means that symbolic links are genuine files, to the same extent as a regular file and their content is their target.
This means if `a` is a symbolic link to `b`, the content of `a` is `b`, not the content of `b`.

If `a` is a symbolic link to `b`, the code snippet `cat a` accesses 2 files:

- `a`
- `b`

This is what is expected : if either `a` or `b` is modified, the stdout of `cat a` may be modified.

### Dirs

Open-lmake manages a flat repo.
This means that `/` is an ordinary character.

As far as open-lmake is concerned, there is no difference between `a` being a dir and `a` not existing.

However, because dirs do exist on disk, it is impossible for `a` and `a/b` to exist simultaneously (i.e. exist as regular or symbolic link).
As a consequence, there is an implicit rule (Uphill) that prevents `a/b` from being buildable if `a` is buildable.

Also, because dirs cannot be made up-to-date, scripts reading dirs can hardly be made reliable and repeatable.
Such constructs are strongly discouraged:

- use of `glob.glob` in python
- use of wildcard in bash

## Jobs

Jobs are identified by their rule and stems (excluding star-stems).

They have a list of targets and a list of deps.
