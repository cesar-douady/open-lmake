<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# The `lmake` module

## For use in `Lmakefile.py`

### `backends`

The `tuple` of implemented backends.
`'local'` is always present.

### `autodeps`

The `tuple` of implemented autodep methods.
`'ld_preload'` is always present.

### `repo_root`

The root directory of the (sub)-repository.

### `top_repo_root`

The root directory of the top level repository.

### `version`

This variable holds the native version of open-lmake.
It is a tuple formed with a `str` (the major version) and a `int` (the minor version).

Upon new releases of open-lmake, the major version is a tag of the form YY.MM providing the year and month of publication if it is not backward compatible.
Else the minor version is increased if the interface is modified (i.e. new features are supported).
Hence, the check is that major versions must match equal and the actual minor version must be at least the expected minor version.

### `user_environ`

When reading `Lmakefile.py`, the environment is reset to a standard environment and this variable holds a copy of `os.environ` before it was reset.

This ensures that the environment cannot be used unless explicitly asked.

### `class pdict`

This class is a dict in which attribute accesses are mapped to item accesses.
It is very practical to handle configurations.

### `multi_strip(txt)`

This function deindents `txt` as much as possible so as to ease printing code.

### `check_version( major , minor=0 )`

This function is used to check that the expected version is compatible with the actual version.

This function must be called right after having imported the `lmake` module as in the future, it may adapt itself to the required version when this function is called.
For example, some default values may be modified and if they are used before this function is called, a wrong (native) value may be provided instead of the correct (adjusted to required version) one.

## For use in `cmd()`

### `run_cc( *cmd_line , marker='...' , stdin=None )`

This functions ensures that all directories listed in arguments such as `-I` or `-L` exist reliably.

`marker` is the name of a marker file which is created in include dirs to guarantee there existence.

`stdin` is the text ot send as the stdin of `cmd_line`.

### `depend( *deps , follow_symlinks=False , verbose=False , read=False , critical=False , essential=False , ignore=False , ignore_error=False , required=True )`

Declare `deps` as parallel deps (i.e. no order exist between them).

If `follow_symlinks` and one of the `deps` is a symbolic link, follow it.

If `verbose`, return a dict with one entry par dep where:

- the key is dep
- the value is a tuple (ok,checksum) where:
  - ok = True if the dep is built with no error
  - ok = False if the dep is built in error
  - ok = None is the dep was not built
  - checksum is computed after the dep

If `read`, pretend `deps` were read.

For `critical`, `essential`, `ignore`, `ignore_error` and `required`, set the corresponding [flag](rules.html#deps) on all `deps`.

Flags accumulate and are never reset.

### `target( *targets , write=False , allow=True , essential=False , ignore=False , incremental=False , no_warning=False , source_ok=False )`

Declare `targets` as targets.

If `write`, pretend `targets` were written.

For `allow`, `essential`, `ignore`, `incremental`, `no_warning` and `source_ok`, set the corresponding [flag](rules.html#targets) on all `targets`.

Flags accumulate and are never reset.

### `check_deps(verbose=False)`

Ensure that all previously seen deps are up-to-date.
Job will be killed in case some deps are not up-to-date.

If `verbose`, wait for server reply. Return value is False if at least a dep is in error.
This is necessary, even without checking return value, to ensure that after this call,
the directories of previous deps actually exist if such deps are not read (such as with lmake.depend).

### `get_autodep()`

Returns whether autodep is currently active or not.

By default, autodep is active.

### `set_autodep(active)`

Set the state of autodep.

### `class Autodep`

A context manager that encapsulates `set_autodep`.

```python
with Autodep(active) :
	<some code>
```

executes `<some code>` with autodep active set as instructed.

### `encode( file , ctx , val , min_length=1 )`

If a code is associated to `val` within file `file` and context `ctx`, return it.
Else a code is created, of length at least `min_length`, is associated to `val` and is return.
Cf. [encode/decode](experimental_codec.html).

`file` must be a source file.

### `decode( file , ctx , code )`

If a val is associated to `code` within file `file` and context `ctx`, return it.
Else an exception is raised.
Cf. [encode/decode](experimental_codec.html).

`file` must be a source file.

Associations are usually created using `encode` but not necessarily (they can be created by hand).

### `xxhsum_file(file)`

Return a checksum of provided file.

The checksum is :

- none                                         if file does not exist, is a directory or a special file
- empty-R                                      if file is empty
- xxxxxxxxxxxxxxxx-R (where x is a hexa digit) if file is regular and non-empty
- xxxxxxxxxxxxxxxx-L                           if file is a symbolic link

Note : this checksum is **not** crypto-robust.

Cf `man xxhsum` for a description of the algorithm.

### `xxhsum(text,is_link=False)`

Return a checksum of provided text.

It is a 16-digit hex value with no suffix.

Note : the empty string lead to 0000000000000000 so as to be easily recognizable.

Note : this checksum is not the same as the checksum of a file with same content.

Note : this checksum is **not** crypto-robust.

Cf `man xxhsum` for a description of the algorithm.
