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

The root dir of the (sub)-repo.

### `top_repo_root`

The root dir of the top-level repo.

### `version`

This variable holds the native version of open-lmake.
It is a tuple formed with a `str` (the major version) and a `int` (the minor version).

Upon new releases of open-lmake, the major version is a tag of the form YY.MM providing the year and month of publication if it is not backward compatible.
Else the minor version is increased if the interface is modified (i.e. new features are supported).
Hence, the check is that major versions must match equal and the actual minor version must be at least the expected minor version.

### `user_environ`

When reading `Lmakefile.py`, the environment is reset to a standard environment and this variable holds a copy of `os.environ` before it was reset.

This ensures that the environment cannot be used unless explicitly asked.

Variable values actually used in `Lmakefile.py` are considered as deps for this process and it is rerun if the actual environment is modified in subsequent `lmake` commands.

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

This functions ensures that all dirs listed in arguments such as `-I` or `-L` exist reliably.

`marker` is the name of a marker file which is created in include dirs to guarantee there existence.

`stdin` is the text ot send as the stdin of `cmd_line`.

### `depend( *deps , follow_symlinks=False , verbose=False , read=False , critical=False , essential=False , ignore=False , ignore_error=False , readdir_ok=False , required=True , regexpr=False , no_star=True )`

Declare `deps` as parallel deps (i.e. no order exist between them).

If `follow_symlinks`, `deps` that are symbolic links are followed (and a dep is set on links themselves, independently of the passed flags that apply for the target the links).

Each dep is associated with an access pattern.
Accesses are of 3 kinds, regular, link and stat:

- Regular means that the file was accessed using C(open,2) or similar, i.e. the job is sensitive to the file content if it is a regular file, but not to the target in case it is a symbolic link.
- Link means that the file was accessed using C(readlink,2) or similar, i.e. the job is sensitive to the target if it is a symbolic link, but not to the content in case it is a regular file.
- Stat means that the file meta-data were accessed, i.e. the job is sensitive to file existence and type, but not to the content or its target.

If a file have none of these accesses, changing it will not trigger a rebuild, but it is still a dep as in case it is in error, this will prevent the job from being run.
Making such dinstinctions is most useful for the automatic processing of symbolic links.
For example, if file `a/b` is opened for reading, and it turns out to be a symbolic link to `c`, open-lmake will set a dep to `a/b` as a link, and to `a/c`
as a link (in case it is itself a link) and regular (as it is opened).

By default, passed deps are associated with no access, but are required to be buildable and produced without error unless `readdir_ok` is true.
To simulate a plain access, you need to pass `read=True` to associate accesses and `required=False` to allow it not to exist.

If `verbose`, return a `dict` with one entry par dep where:

- The key is the dep name.
- The value is a `dict` composed of:
  - `ok`:       `True` if the dep is built with no error, `False` if the dep is built in error, `None` if the was not built.
  - `checksum`: The checksum computed after the dep (unless `ok` is `None`) (cf. *xxhsum(1)*).
  - `rule`:     The rule name of the job that has generated the dep one exists.
  - `special`:  The special nature (e.g. `src`).
  - `stems`:    A `dict` mapping stem names to stem values if the rule is a plain rule.

If `read`, report an actual read of `deps`. Default is just to alter associated flags.

If `regexpr`, pass flags to all deps matching `deps` interpreted as regexprs, much like the `side_deps` rule attribute.
However, the `ignore` flag only applies to deps following this call.

For `critical`, `essential`, `ignore`, `ignore_error`, `readdir_ok`, `required` and `no_star`, set the corresponding [flag](rules.html#deps) on all `deps`:

- If `critical`,     create critical deps (cf. note (5)).
- If `essential`,    passed deps will appear in the flow shown with a graphical tool.
- If `ignore_error`, ignore the error status of the passed deps.
- If `readdir_ok`,   `readdir` (3) can be called on passed deps without error even if not `ignore`'ed nor `incremental`.
- If not `required`, accept that deps be not buildable, as for a normal read access (in such a case, the read may fail, but open-lmake is ok).
- If not `no_star`,  accept regexpr based flags (e.g. calls to this function with `regexpr=True`).
- If `ignore`,       deps are ignored altogether, even if further accessed (but previous accesses are kept).

Flags accumulate and are never reset.

Notes:

- (1):
	The same functionality is provided with the `ldepend` executable.
- (2):
	Flags can be associated to deps on a regexpr (matching on dep name) basis by using the `side_deps` rule attribute.
- (3):
	If `cat a b` is executed, open-lmake sees 2 `open` system calls, to `a` then to `b`, exactly the same sequence that if one did `cat $(cat a)` and `a` contained `b`.  
	Suppose now that `b` is an error. This is a reason for your job to be in error.
	But if `a` is modifed, in the former case, this cannot solve the error while in the latter case, it may if the new content of `a` points to a file that may successfully be built.
	Because open-lmake cannot distinguish between the 2 cases, upon a modification of `a`, the job will be rerun in the hope that `b` is not accessed any more.
	Parallel deps prevents this trial.
- (4):
	If a series of files are read in a loop and the loop is written in such a way as to stop on the first error
	and if the series of file does not depend on the actual content of said files,
	then it is preferable to pre-access (using B(ldepend)) all files before starting the loop.
	The reason is that without this precaution, deps will be discovered one by one and may be built serially instead of all of them in parallel.
- (5):
	If a series of dep is directly derived from the content of a file, it may be wise to declare it as `critical`.
	When a critical dep is modified, open-lmake forgets about deps reported after it.  
	Usually, when a file is modified, this has no influence on the list of files that are accessed after it,
	and open-lmake anticipates this by building these deps speculatively.
	But in some situations, it is almost certain that there will be an influence and it is preferable not to anticipate.
	this is what critical deps are made for: in case of modifications, following deps are not built speculatively.

### `target( *targets , write=False , regexpr=False , allow=True , essential=False , ignore=False , incremental=False , no_warning=False , source_ok=False , critical=False , ignore_error=False , readdir_ok=False , required=False , no_star=True )`

Declare `targets` as targets and alter associated flags.
Note that the `allow` argument default value is `True`.

Also, calling this function does not make `targets` official targets of the job, i.e. `targets` are side targets.
The official job of a target is the one selected if needing its content, it must be known before any job is run.

If `write`, report that `targets` were written to.

If `regexpr`, pass flags to all targets matching `targets` interpreted as regexprs, much like the `side_targets` rule attribute.
However, the `ignore` flag only applies to targets following this call.

For `allow`, `essential`, `ignore`, `incremental`, `no_warning`, `source_ok` and `no_star`, set the corresponding [flag](rules.html#targets) on all `targets`:
- If `essential`,   show when generating user oriented graphs.
- If `incremental`, `targets` are not unlinked before job execution and read accesses to them are ignored.
- If `no_warning`,  no warning is emitted if `targets` are either uniquified or unlinked while generated by another job.
- If `ignore`,      from now on, ignore all reads and writes to `targets`.
- If not `allow`,   do not make `targets` valid targets.
- If not `no_star`, accept regexpr based flags (e.g. calls to this function with `regexpr=True`).
- If `source_ok`,   accept that `targets` be sources. Else, writing to a source is an error.

In case passed targets turn out to be deps, the deps flags are also available: `critical`, `ignore_error`, `readdir_ok` and `required`:
- If `critical`,     create critical deps (cf. note (5) of `depend`).
- If `ignore_error`, ignore the error status of the passed deps.
- If `readdir_ok`,   `readdir` (3) can be called on passed deps without error even if not `ignore`'ed nor `incremental`.
- If `not required`, accept that deps be not buildable, as for a normal read access (in such a case, the read may fail, but open-lmake is ok).

Flags accumulate and are never reset.

### `check_deps(sync=False)`

Ensure that all previously seen deps are up-to-date.
Job will be killed in case some deps are not up-to-date.

If `sync`, wait for server reply. Return value is False if at least a dep is in error.
This is necessary, even without checking return value, to ensure that after this call,
the dirs of previous deps actually exist if such deps are not read (such as with lmake.depend).

**CAVEAT**

If used in conjonction with the `kill_sigs` attribute with a handler to manage the listed signal(s) (typically by calling `signal.signal(...)` and without `sync=True`,
and if a process is launched shortly after (typically by calling `subprocess.run` or `os.system`),
it may be that said process does not see the signal.
This is due to a race condition in I(python) when said process is just starting.

This may be annoying if said process was supposed to do some clean up or if it is very long.
The solution in this case is to pass `sync=True`.
This has a small cost in the general case where deps are actually up-to-date, but provides a reliable way to kill the job as `check_deps` will still be running when the signal fires up.

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

- none                                         if file does not exist, is a dir or a special file
- empty-R                                      if file is empty
- xxxxxxxxxxxxxxxx-R (where x is a hexa digit) if file is regular and non-empty
- xxxxxxxxxxxxxxxx-L                           if file is a symbolic link

Note : this checksum is **not** crypto-robust.

Cf. *xxhsum(1)* for a description of the algorithm.

### `xxhsum(text,is_link=False)`

Return a checksum of provided text.

It is a 16-digit hex value with no suffix.

Note : the empty string lead to 0000000000000000 so as to be easily recognizable.

Note : this checksum is not the same as the checksum of a file with same content.

Note : this checksum is **not** crypto-robust.

Cf. *xxhsum(1)* for a description of the algorithm.

### `report_import(module_name=None,path=None,module_suffixes=None)`

Does necessary reporting when a module has been imported.

`module_name` is the name of the imported module.
This function only handles the last level import, so it must be called at each level in case the module lies in a package.

If not provided or empty, only does the reporting due to the path, but assumes no module is accessed.

The way such reporting is done is by reporting a dep for each local dir in the path, for each module suffix, until the module is found, locally or externally.

`path` is the path which the imported module is searched in.

If not provided or empty, it defaults to the current value of `sys.path`.

Unless used with python2, this function allows `readdir` accesses to local dirs in the path as python does such a `readdir`.

`module_suffixes` is the list of suffixes to try that may provide a module, e.g. `('.py','/__init__.py')`.

If not provided, the default is:

- for python2: `('.so','.py','/__init__.so','/__init__.py')`
- for python3: `( i+s for s in importlib.machinery.all_suffixes() for i in ('','/__init__'))`

Note:

- It may be wise to specify only the suffixes actually used locally to reduce the number of deps.
- External modules are searched with the standard suffixes, even if `module_suffixes` is provided as there is no reason for the external modules to adhere to local conventions.
