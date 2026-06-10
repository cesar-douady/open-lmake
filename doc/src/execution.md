<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2026 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Execution

## Handling an access

At first glance, recognizing a target from a dep when a job runs seems pretty easy when the accesses to the disk can be traced: reading a file is a dep, writing to it is a target.
And this is what is done informally, but there are a lot of corner cases.

The specification described below has been carefully designed to allow open-lmake to run adequate jobs to reach a stable state from any starting point.
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

This is what stable state means: the content of `foo` is independent of the history and only depends on the rules and the content of sources, both being managed through `git` in this example.

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

- A system call that reads dir `foo`, e.g. [getdents](https://man7.org/linux/man-pages/man2/getdents.2.html).
- A libc call that reads dir `foo`, e.g. [readdir](https://man7.org/linux/man-pages/man3/readdir.3.html) or [glob](https://man7.org/linux/man-pages/man3/glob.3.html)
  (in which its pattern argument requires reading `foo`).
- Under the condition that these actions are not preceded by a call to `lmake.target('foo',ignore=True)` or the execution of `ltarget -I foo`.
- Also under the condition that neither `lmake.target('foo',incremental=True)` was called nor `ltarget -i foo` executed.
- Also under the condition that `foo` does not match a `targets` or `side_targets` entry with the `ignore` or `incremental` flags set.
- Also under the condition that `foo` lies in the repo (i.e. under the dir containing `Lmakefile.py` but not in its `LMAKE/lmake` sub-dir) or is the repo root dir.

Although dirs do not exist for open-lmake, reading dir `foo` is an error unless:

- [Set the `readdir_ok` flag as a rule attribute](unit_tests/wine.html#:~:text=readdir%5Fok%20%3D%20True).
- [Passing the `readdir_ok` flag](unit_tests/mandelbrot.html#:~:text=side%5Fdeps%20%3D%20%7B%20%27DIR%27%20%3A%20%28%20%27mandelbrot%2F%7B%2A%3A%2E%2A%7D%27%20%2C%20%27readdir%5Fok%27%20%29%20%7D)
  in the `targets`, `side_targets` or `side_deps` entry.
- [Calling `lmake.depend('foo',readdir_ok=True)`](unit_tests/pyc.html#:~:text=lmake%2Edepend%28%27%2E%27%2Creaddir%5Fok%3DTrue%29%20%23%20this%20rule%20is%20a%20Rule%2C%20not%20a%20PyRule%2C%20on%20purpose%20%28so%20pyc%20files%20are%20not%20handled%29%2C%20so%20we%20must%20explicitly%20allow%20reading%20%27%2E%27)
  or executing `ldepend -D foo`.
- Calling `lmake.target('foo',readdir_ok=True)` or executing `ltarget -D foo`.

Note that the `lmake.PyRule` base class sets the the `readdir_ok` flag on dirs mentioned in `sys.path` when executing import in python3.
This is because python3 optimizes imports by pre-reading these dirs.

Such restrictions ensure the reliability of job execution as the content of a dir is mostly unpredictable as it depends on the past history:
files may or may not have been already built, or previously built files that are now non-buildable may still exist.

Ideally, listing a dir would lead to all buildable files (or sub-dirs), but this is not doable in the generic case as such list may be infinite.
So open-lmake reverts to letting the user deal with this question, using an opt-in approach so the user cannot miss it.

Note that if such a dir is marked as `incremental`, the user already has the responsibility of handling its past history and there is no need for an additional flag.

### mounts and chroot

As soon as autodep feature is on:

- Doing any kind of [`mount`](https://man7.org/linux/man-pages/man2/mount.2.html) to a directory or a file which is tracked by the autodep engine is forbidden by default.
- Doing any [`chroot`](https://man7.org/linux/man-pages/man2/chroot.2.html) is forbidden by default.

This is because open-lmake cannot track accesses as reported files will have their mounted names rather than their real underlying names.

Setting the `mount_chroot_ok` rule attribute allow both.
Although separately such calls cannot be reliable, doing both in a sound way can restore consistency if names are transported to a chrooted environment through bind mounts.

### Being a target

A file may be a target from the begining of the job execution, or it may become a target during job execution.
In the latter case, it is not a target until the point where it becomes one.
A file cannot stop being a target: once it has become a target, this is until the end of the job execution.

A file is a target from the begining of the job execution if it matches a `targets` or `side_targets` entry.

A file becomes a target when :

- It is written to (with the meaning mentioned above)..
- `lmake.target` or `ltarget` is called unless the flag `source_ok` is finally set (i.e. including if set independently).

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
This is an error unless either:

- The file is finally unlinked (or was never created).
- Its `source_ok` flag has been set and its `incremental` one is not.  
  This allows rules that alter sources to choose between handling any previous content without impact on final value (`incremental`)
  or compute value based on previous one (in which case it must be a dep).
  In this latter case, the job will rerun if such dep-target is actually modified.

The third case is when a target was not declared as such.
`foo` can be declared as target by:

- Matching a `targets` or `side_targets` entry.
- Calling `lmake.target('foo')` (unless `allow=False` is passed).
- Executing `ltarget foo` in which the `-a` option is not passed.

A target that is not declared is an error.

### Processing a target

Targets are normally erased before the start of the job execution, unless they are sources or flagged as `incremental`.
In case a target is also a dep, it is automatically flagged as `incremental`, whether it is an error or not.

If a job is run when a not `incremental` and not source target exists, it is deemed unreliable and is rerun.

### Best effort

Open-lmake tries to minimize the execution of jobs, but may sometimes miss a point and execute a job superfluously.
This may include erasing a file that has no associated production rule.
Unless a file is a dep of no job, open-lmake may rebuild it at any time, even when not strictly necessary.

In the case open-lmake determines that a file may have actually been written manually outside its control, it prevents overwriting a potentially valuable user-generated content.
In that case, open-lmake quarantines the file under the [LMAKE/quarantine](meta_data.html#:~:text=LMAKE%2Fquarantine) dir with its original name.
This quarantine mechanism, which is not necessary for open-lmake processing but is a facility for the user, is best effort.
There are cases where open-lmake cannot anticipate such an overwrite (for example if a job writes to a target while it dit not during the previous run,
open-lmake could not anticipate this would occur).

A typical case is when source files are auto-generated.
The user may mistakenly fix the generated file, thinking they are fixing a source.
In that case, open-lmake will detect this case and regenerate this file so as to ensure reproducibility, thus potentially overwriting valuable content.

## tmp dir

The physical dir is:

- If open-lmake is supposed to keep this dir after job execution, it is a dir under `LMAKE/tmp`, determined by open-lmake (its precise value is reported by `lshow -i`).
- Else if `$TMPDIR` is specified in the environment of the job and is not empty, it is used. Note that it needs not be unique as open-lmake will create a unique sub-dir within it.
- Else, a dir determined by open-lmake within the `LMAKE` dir.

Unless open-lmake is instructed to keep this dir, it is erased at the end of the job execution.

## Status line

Before, after and sometimes during job execution, a status line is written to the console with an associated step as follows:

| Step                 | Description                                                                                                                                              |
|----------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------|
| `bad_cache_download` | job targets could not be downloaded from cache (no impact on repo, job is run, but cache may have to be repaired)                                        |
| `bad_dep`            | an explanation is provided                                                                                                                               |
| `bad_side_dep`       | an explanation is provided                                                                                                                               |
| `bad_side_target`    | an explanation is provided                                                                                                                               |
| `bad_target`         | an explanation is provided                                                                                                                               |
| `changed`            | a source file has been observed to be modified                                                                                                           |
| `completed`          | job completed before being killed after a ^C (but was not analyzed, so it may or may not have to be rerun and may or may not be in error)                |
| `continue`           | used to interleave stdout from different jobs when the `-o` `lmake` option is used                                                                       |
| `continue`           | ^C was hit but job continues because it is necessary for another `lmake` command running in parallel                                                     |
| `dep_error`          | job cannot start because some of its deps are in error                                                                                                   |
| `early_rerun`        | a dep discovered during dynamic attribute evaluation is not up-to-date                                                                                   |
| `expand`             | a codec file was expanded to an internal format optimized for performance                                                                                |
| `missing_static`     | job cannot start because some of its static deps are not buildable                                                                                       |
| `deps_not_available` | an explanation is provided with the message                                                                                                              |
| `done`               | job has completed ok, open-lmake can proceed with dependent jobs                                                                                         |
| `double_start`       | job has started twice (typically slurm may restart a job in some error cases), for information only, this is smoothly handled                            |
| `hit_done`           | same as `done` but targets were downloaded from cache rather than computed during job execution                                                          |
| `hit_rerun`          | same as `may_rerun` but deps were acquired from cache rather than from job execution                                                                     |
| `hit_steady`         | same as `hit_done` and all targets have been observed to be identical to the last time job was executed                                                  |
| `killed`             | job was killed after a ^C                                                                                                                                |
| `lost`               | a system error (or an open-lmake bug) crashed job baby sitting                                                                                           |
| `lost_error`         | a severe system error leading open-lmake to think problem is not recovable or max retry after lost count is reached                                      |
| `may_rerun`          | new deps have been discovered and job has to rerun unless such deps turn out to be ok during this execution                                              |
| `missing`            | a source file is missing                                                                                                                                 |
| `new`                | a source file is needed for the first time                                                                                                               |
| `no_cache_dismiss`   | open-lmake needed to dismiss data previously uploaded to cache and could not do so (no impact on repo but cache may have to be repaired)                 |
| `no_cache_upload`    | open-lmake needed to upload data to cache and could not do so (no impact on repo but cache may have to be repaired)                                      |
| `no_dynamic`         | some dynamic attributes could not be computed                                                                                                            |
| `reformat`           | a codec file was reformatted to recover a canonical format                                                                                               |
| `rerun`              | an event forces open-lmake to rerun the job (e.g. a non-incremental target existed before job execution or a dep has been modified during job execution) |
| `retry`              | job starts execution after being retried because previous execution was lost                                                                             |
| `run_loop`           | max configured number of job execution has been reached                                                                                                  |
| `start`              | job starts execution (may be skipped if job is short, as specified in `lmake.config.console.start_delay`)                                                |
| `started`            | job was already started because of another `lmake` command running in parallel                                                                           |
| `steady`             | same as `done` and all targets have been observed to be identical to the last time job was executed                                                      |
| `steady`             | a source file has been observed to have a new date but content did not change                                                                            |
| `submit_loop`        | max configured number of job submission has been reached                                                                                                 |
| `unlink`             | a file was needed, existed, but no rule apply to generate it, so its official state is to be inexistent                                                  |
| `update`             | a codec file was updated from an internal format optimized for performance                                                                               |
