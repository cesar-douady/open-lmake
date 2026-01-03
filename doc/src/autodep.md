<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2026 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Autodep

Autodep is a mechanism through which jobs are spied to automatically detect disk accesses.
From this information open-lmake can determine if accesses were within the constraints provided by the rule and can list all deps.

## Spying methods

There exists two classes of spying methods.
Not all methods are supported on all systems, though.

### Spying methods based on `libc` calls

This consists in spying all calls the the `libc`.
Several mechanisms can be used to do so.

All of them consist in diverting the calls to the `libc` that access files (typically `open`, but there are about a hundred of them) to a wrapper code
that records the access before handling over to the real `libc`.
They differ in the methods used to divert these calls to the autodep code.

This class of methods is fast as there is no need to switch context at each access.
Moreover, the accessed file is first scanned to see if it is a system file (such as in `/usr`), in which case we can escape very early from the recording mechanism.
And in practice, such accesses to system files are by far the most common case.

One of the major drawbacks is that it requires the `libc` to be dynamically linked. While `libc` static linking is very uncommon, it does happen.

### `$LD_AUDIT`

Modern Linux dynamic linkers implement an auditing mechanism.
This works by providing hooks at dynamic link edition time (by setting the environment variable `$LD_AUDIT`) when a file is loaded, when a symbol is searched, when a reference is bound, etc.
In our case, we trap all symbol look up into the `libc` and interesting calls (i.e. those that access files) are diverted at that time.

However, some linkers do not seem to honor this auditing API.
For example, programs compiled by the rust compiler (including `rustc` itself) could not be made working.

Such auditing code is marginally intrusive in the user code as, while lying in the same address space, it is in a different segment.
For example it has its own `errno` global variable.

If available, this is the default method.

### `$LD_PRELOAD`

This method consists in pre-loading our spying library before the `libc`.
Because it is loaded before and contains the same symbols as the `libc`, these calls from the user application are diverted to our code.

this is a little bit more intrusive (e.g. the `errno` variable is shared) and this is the default method if `$LD_AUDIT` is not available.

### `$LD_PRELOAD` with `jemalloc`

The use of `jemalloc` creates a chicken and egg problem at start up.
The reason is that the spying code requires the use of `malloc` at start up, and `jemalloc` (which is called in lieu of `malloc`) accesses a configuration file at start up.
A special implementation has been devised to handle this case, but is too fragile and complex to make it the default `$LD_PRELOAD` method.

### Spying methods based on system calls

The principle is to use `ptrace` (the system call used by the `strace` utility) to spy user code activity.

This is almost non-intrusive.
In one case, we have seen a commercial tool reading `/proc/self/status` to detect such a `ptrace`ing process, and it stopped, thinking it was being reverse engineered.
Curiously, it did not detect `$LD_PRELOAD`...

The major drawback is performance wise: the impact is more significant as there is a context switch at each system call.
`BPF` is used, if available, to decrease the number of useless context switches, but it does not allow to filter out on filename, so it is impossible to have an early ignore of system files.

## What to do with accesses

There are 2 questions to solve :

- Determine the `cwd`. Because accesses may be relative to it (and usually are), the spying code must have a precise view of the `cwd`.
  This requires to intercept `chdir` although no access is to be reported.
- Symbolic link processing.
  Open-lmake lies in the physical world (and there is no way it can do anything else) and must be aware of any symbolic link traversal.
  This includes the ones on the dir path.
  So the spying code includes a functionality that resembles to `realpath`, listing all traversed links.

Lying in the physical world means that symbolic links are handled like plain data files, except that there is a special bit that says it is a symbolic link.
Its content is its target.
For example, after the code sequence:

```
cd a
cat b
```

where `b` is symbolic link to `c`, 2 deps are recorded:

- `a/b` (a symbolic link), as if it is modified, job must be rerun.
- `a/c` (a plain data file), same reason.

Generally speaking, read a file makes it a dep, writing to it makes it a target.
Of course, reading a file that has been written doe not make it a dep any more.

## How to report accesses

When a job is run, a wrapper (called `job_exec`) is launched that launches the user process.

`job_exec` has several responsibilities, among which :

- Prepare the user environment for the user code (environment variables, cwd, namespace if necessary, etc.).
- Receive the accesses made by the user code (through a pipe if possible, else a socket) and record them.
- Determine what is a dep, what is a target etc.
- Report a job digest to the server (the central process managing the dep DAG).

The major idea is that when an access is reported by the user code (in the case of `libc` call spying), there is no reply from `job_exec` back to the user code, so no round trip delay is incurred.

## Deps order is kept as well

Remember that when a job is run, its deps list is **approximative**.
It is the one of the previous run, which had different file contents.
For example, a `.c` may have changed, including a `#include` directive.
In case there are 2 deps `d1` and `d2`, and `d1` was just discovered, it may be out-of date and the job ran with a bad content for `d1`.

Most of the time, this is harmless, but sometimes, it may happen that `d2` is not necessary any more (because old `d1` content had `#include "d2"` and new one does not).
In that case, this job must be rerun with the new content of `d1`, even if `d2` is in error, as `d2` might disappear as a dep.

This may only occurs if `d2` was accessed **after** `d1` was accessed. If `d2` was accessed before `d1`, it is safe to say the job cannot run because `d2` is in error: it will never disappear.
