<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Execution

## Handling an access

At first glance, recognizing a target from a dep when a job runs seems pretty easy when the accesses to the disk can be traced : reading a file is a dep, writing to it is a target.
And this is what is done informally, but there are a lot of corner cases.

The specification devised hereinafter has been carefully thought to allow open-lmake to run adequate jobs to reach a stable state from any starting point.
More specifically, think of the following sequence:

```bash
git clean -ffdx
lmake foo
git pull
lmake foo
```

The second `lmake foo` command is supposed to do the minimum work to reach the same content of `foo` as would be obtained with the sequence:

```bash
git pull
git clean -ffdx
lmake foo
```

This what stable state means : the content of `foo` is independent of the history and only depends on the rules and the content of sources, both being managed through `git` in this example.

In this specification, dirs are ignored (i.e. the presence or content of a dir has no impact) and symbolic links are similar to regular files whose content is the link itself.

### Reading and writing files

The first point is to precisely know what reading and writing mean.

Writing to file `foo` means:

- A system call that writes or initiate writing to `foo`, e.g. `open("foo",O_WRONLY|O_TRUNC)` or `symlink(...,"foo")`, assuming the `autodep` rule attribute is not set to `'none'`.
- Unlinking `foo`, e.g. `unlink("foo")`, is also deemed to be writing to it.
- A call to `lmake.target('foo',write=True)`.
- The execution of `ltarget -W foo`.
- Under the condition that these actions are not preceded by a call to `lmake.target('foo',ignore=True)` or the execution of `ltarget -I foo`.
- Also under the condition that `foo` does not match a `targets` or `side_targets` entry with the `Ignore` flag set.
- Also under the condition that `foo` lies in the repo (i.e. under the dir containing `Lmakefile.py` but not in its `LMAKE/lmake` sub-dir).

Reading file `foo` means :

- A system call that reads or initiate reading `foo`, e.g. `open("foo",O_RDONLY)`, `readlink("foo",...)` or `stat("foo",...)`,
  assuming the `autodep` rule attribute is not set to `'none'`.
- Unless the `config.link_support` attribute is set to `'none'`,
  any access (reading or writing) to `foo` which follows symlinks is an implicit `readlink`.
- Unless the `config.link_support` attribute is set to `'file'` or `'none'`,
  any access (reading or writing) to `foo`, whether it follows symlinks or not, is an implicit `readlink` of all dirs leading to it.
- Note that some system calls can be both a read and a write, e.g. `open("foo", O_RDWR)` but also `rename("foo",...)`.
  In that case, the read occurs before the write.
- A call to `lmake.depend('foo',read=True)`.
- The execution of `ldepend -R foo`.
- Under the condition that these actions are not preceded by a call to `lmake.depend('foo',ignore=True)` or the execution of `ldepend -I foo`.
- Also under the condition that `foo` is not listed in `deps` or matches a `side_deps` entry, with the `Ignore` flag set.
- Also under the condition that `foo` lies in the repo (i.e. under the dir containing `Lmakefile.py` but not in its `LMAKE/lmake` sub-dir) or in a source dir.

### Reading a dir

A dir `foo` is read when files it contains are listed, which occur when:

- A system call that reads dir `foo`, e.g. `getdents`.
- A libc call that reads dir `foo`, e.g. `readdir` or `glob` (in which its pattern argument requires reading `foo`).
- Under the condition that these actions are not preceded by a call to `lmake.target('foo',ignore=True)` or the execution of `ltarget -I foo`.
- Also under the condition that neither `lmake.target('foo',incremental=True)` was called nor `ltarget -i foo` executed.
- Also under the condition that `foo` does not match a `targets` or `side_targets` entry with the `Ignore` or `Incremental` flags set.
- Also under the condition that `foo` lies in the repo (i.e. under the dir containing `Lmakefile.py` but not in its `LMAKE/lmake` sub-dir) or is the repo root dir.

Although dirs do not exist for open-lmake, reading dir `foo` is an error unless the `ReaddirOk` attribute was set on the rule or the `ReaddirOk` flag is set, which can be done by:

- Passing the `ReaddirOk` flag in the `targets`, `side_targets` or `side_deps` entry.
- Calling `lmake.depend('foo',readdir_ok=True)` or executing `ldepend -D foo`.
- Calling `lmake.target('foo',readdir_ok=True)` or executing `ltarget -D foo`.

Note that the `lmake.PyRule` base class sets the the `ReaddirOk` flag on dirs mentioned in `sys.path` when executing python3.
This is because python3 optimizes imports by pre-reading these dirs.

Such restrictions ensure the reliability of job execution as the content of a dir is mostly unpredictable as it depends on the past history:
files may or may not have been already built, or previously built files that are now non-buildable may still exist.

Ideally, listing a dir would lead to all buildable files (or sub-dirs), but this is not doable in the generic case as such list may be infinite.
So open-lmake reverts to letting the user deal with this question, using an opt-in approach so the user cannot miss it.

Note that if such a dir is marked as `incremental`, the user already has the responsibility of handling its past history and there is no need for an additional flag.

### Being a target

A file may be a target from the begining of the job execution, or it may become a target during job execution.
In the latter case, it is not a target until the point where it becomes one.
A file cannot stop being a target: once it has become a target, this is until the end of the job execution.

A file is a target from the begining of the job execution if it matches a `targets` or `side_targets` entry.

A file becomes a target when it is written to (with the meaning mentioned above) or when `lmake.target` or `ltarget` is called.

### Being a dep

A file may be a dep from the begining of the job execution, or it may become a dep during job execution.

A file cannot stop being a dep : once it has become a dep, this is until the end of the job execution.

A file is a dep from the begining of the job execution if it listed as a `deps` in the rule.

A file becomes a dep when it is read (with the meaning mentioned above) while not a target at that time.

### Errors

Some cases lead to errors, independently of the user script.

The first case is when there is clash between static declarations.
`targets`, `side_targets`, `side_deps` entries may or may not contain star stems.
In the latter case, and including the static deps listed in `deps`, they are static entries.
It is an error if the same file is listed several times as a static entry.

The second case is when a file is both a dep and a target.
You may have noticed that the definition above does not preclude this case, mostly because a file may start its life as a dep and become a target.
This is an error unless the file is finally unlinked (or was never created).

The third case is when a target was not declared as such.
`foo` can be declared as target by:

- matching a `targets` or `side_targets` entry.
- calling `lmake.target('foo',allow=True)` (which is the default value for the `allow` arg).
- executing `ltarget foo` in which the `-a` option is not passed.

A target that is not declared is an error.

### Processing a target

Targets are normally erased before the start of the job execution, unless they are sources or flagged as `incremental`.
In case a target is also a dep, it is automatically flagged as `incremental`, whether it is an error or not.

If a job is run when a not `incremental` and not source target exists, it is deemed unreliable and is rerun.

### Best effort

Open-lmake tries to minimize the execution of jobs, but may sometimes miss a point and execute a job superfluously.
This may include erasing a file that has no associated production rule.
Unless a file is a dep of no job, open-lmake may rebuild it at any time, even when not strictly necessary.

In the case open-lmake determines that a file may have actually been written manually outside its control, it fears to overwrite a user-generated content.
In that case, open-lmake quarantines the file under the `LMAKE/quarantine` dir with its original name.
This quarantine mechanism, which is not necessary for open-lmake processing but is a facility for the user, is best effort.
There are cases where open-lmake cannot anticipate such an overwrite.

## tmp dir

The physical dir is:

- If `$TMPDIR` is set to empty, there is no tmp dir.
- If open-lmake is supposed to keep this dir after job execution, it is a dir under `LMAKE/tmp`, determined by open-lmake (its precise value is reported by `lshow -i`).
- Else if `$TMPDIR` is specified in the environment of the job, it is used. Note that it need not be unique as open-lmake will create a unique sub-dir within it.
- Else, a dir determined by open-lmake lying in the `LMAKE` dir.


Unless open-lmake is instructed to keep this dir, it is erased at the end of the job execution.

At execution time:

- If `$TMPDIR` is set to empty, it is suppressed from the environment and if the job uses the default tmp dir (usually `/tmp`), an error is generated.
- Else `$TMPDIR` is set so that the job can use it to access the tmp dir.
