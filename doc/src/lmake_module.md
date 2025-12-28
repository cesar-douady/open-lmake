<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# The `lmake` module

## For use in `Lmakefile.py`

### [`autodeps`](unit_tests/valgrind.html#:~:text=if%20%27ptrace%27%20not%20in%20lmake%2Eautodeps%20%3A)

The `tuple` of implemented autodep methods.
`'ld_preload'` is always present.

### [`backends`](unit_tests/wide.html#:~:text=if%20%27slurm%27%20in%20lmake%2Ebackends%20and%20osp%2Eexists%28%27%2Fetc%2Fslurm%2Fslurm%2Econf%27%29%20%3A)

The `tuple` of implemented backends.
`'local'` is always present.

### `check_version( major , minor=0 )`

This function is used to check that the expected version is compatible with the actual version.

This function must be called immediately after importing the `lmake` module as in the future, it may adapt itself to the required version when this function is called.
For example, some default values may be modified and if they are used before this function is called, a wrong (native) value may be provided instead of the correct (adjusted to required version) one.

### `repo_root`

The root dir of the (sub)-repo.

### `top_repo_root`

The root dir of the top-level repo.

### [`user_environ`](unit_tests/wine.html#:~:text=environ%5Fresources%20%3D%20%7B%20%27DISPLAY%27%20%3A%20lmake%2Euser%5Fenviron%5B%27DISPLAY%27%5D%20%7D%20%23%20use%20current%20display)

When reading `Lmakefile.py`, the environment is reset to a standard environment and this variable holds a copy of `os.environ` before it was reset.

This ensures that the environment cannot be used unless explicitly asked.

Variable values actually used in `Lmakefile.py` are considered as deps for this process and it is rerun if the actual environment is modified in subsequent `lmake` commands.

### `version`

This variable holds the native version of open-lmake.
It is a tuple formed with a `str` (the major version) and a `int` (the minor version).

Upon new releases of open-lmake, the major version is a tag of the form YY.MM providing the year and month of publication if it is not backward compatible.
Else the minor version is increased if the interface is modified (i.e. new features are supported).
Hence, the check is that major versions must match equal and the actual minor version must be at least the expected minor version.

### `class pdict`

This class is a dict in which attribute accesses are mapped to item accesses.
It is very practical to handle configurations.

## For use in `cmd()`

### `class Autodep`

A context manager that encapsulates `set_autodep`.

```python
with Autodep(active) :
	<some code>
```

executes `<some code>` with autodep active set as instructed.

### [`check_deps(delay=0,sync=False)`](unit_tests/codec.html#:~:text=lmake%2Echeck%5Fdeps%28%29)

Ensure that all previously seen deps are up-to-date and written targets are note pre-existing (only pertinent for star-targets).
Job will be killed in case condition above is not met.

This means that rerun reasons are checked.
This can be usefully called before heavy computation so that such heavy computation is run only once.

If not null, the previous check is delayed by `delay` seconds. unless `sync`, function returns immediately, though (cf. note (2).

If `sync`, wait for server reply. Return value is `True` if all deps are ok (no error), `False` otherwise.
This is necessary, even without checking return value, to ensure that after this call,
the dirs of previous deps actually exist if such deps are not read (e.g. when using `lmake.depend` without `read=True`).

**CAVEAT**

If used in conjonction with the `kill_sigs` attribute with a handler to manage the listed signal(s) (typically by calling `signal.signal(...)` and without `sync=True`,
and if a process is launched shortly after (typically by calling `subprocess.run` or `os.system`),
it may be that said process does not see the signal.
This is due to a race condition in I(python) when said process is just starting.

This may be annoying if said process was supposed to do some clean up or if it is very long.
The solution in this case is to pass `sync=True`.
This has a small cost in the general case where deps are actually up-to-date, but provides a reliable way to kill the job as `check_deps` will still be running when the signal fires up.

Notes:

- (1):
	The same functionality is provided with the `lcheck_deps` executable.
- (2):
	The `delay` argument is useful in situations such as compilation where all input files are read upfront before spending time doing the actual job.
	Because there is no way to insert a call to `check_deps` after having read the files but before carrying out compilation, an alternative consists in executing it with a delay upfront.
	If `delay` is long enough for all files to be read but substantially smaller than the compilation time,
	time can be save by avoiding full compilation with bad inputs when deps are being discovered.

### [`cp_target_tree( from_dir , to_dir , regexpr=None )`](unit_tests/target_tree.html#:~:text=lmake%2Ecp%5Ftarget%5Ftree%28%27dut%27%2C%27dut2%27%2Cregexpr%3D%27dut%2Fa%2E%27%29)

Copy generated targets that are inside `from_dir` and match `regexpr` to `to_dir`.

Links are copied as is, without effort to make them point to the same place.

### [`decode( file , ctx , code )`](unit_tests/codec.html#:~:text=lmake%2Edecode%28%27codec%5Ffile%27%2C%27ctx%27%2Ccode%29%29)

If a val is associated to `code` within file `file` and context `ctx`, return it.
Else an exception is raised.

`file` (symbolic links are followed) may be either a source file within repo or a dir (ending with `'/'`).
In the latter case, such a dir must lie within a source dir and must contain a file `LMAKE/config.py` containing definitions for :

- `file_sync` : one of `none`, `dir` (default) or `sync` for choosing the method to ensure proper consistent operations.
- `perm`      : one of `none`, `group` or `other` which specifies who is given permission to access this shared dir.

Cf. [encode/decode](codec.html).

Associations are usually created using `encode` but not necessarily (they can be created by hand).

### [`depend( *deps , follow_symlinks=False , direct=False , verbose=False , read=False , critical=False , essential=False , ignore=False , ignore_error=False , readdir_ok=False , required=True , regexpr=False , no_star=True )`](unit_tests/critical.html#:~:text=lmake%2Edepend%28%27src1%27%2C%27src2%27%20%2Cread%3DTrue%2Ccritical%3DTrue%29)

Declare `deps` as parallel deps (i.e. no order exist between them).

If `follow_symlinks`, `deps` that are symbolic links are followed (and a dep is set on links themselves, independently of the passed flags that apply for the target the links).

Each dep is associated with an access pattern.
Accesses are of 3 kinds, regular, link and stat:

- Regular means that the file was accessed using `open` or similar, i.e. the job is sensitive to the file content if it is a regular file, but not to the target in case it is a symbolic link.
- Link means that the file was accessed using `readlink` or similar, i.e. the job is sensitive to the target if it is a symbolic link, but not to the content in case it is a regular file.
- Stat means that the file meta-data were accessed, i.e. the job is sensitive to file existence and type, but not to the content or its target.

If a file have none of these accesses, changing it will not trigger a rebuild, but it is still a dep as in case it is in error, this will prevent the job from being run.
Making such dinstinctions is most useful for the automatic processing of symbolic links.
For example, if file `a/b` is opened for reading, and it turns out to be a symbolic link to `c`, open-lmake will set a dep to `a/b` as a link, and to `a/c`
as a link (in case it is itself a link) and regular (as it is opened).

By default, passed deps are associated with no access, but are required to be buildable and produced without error unless `readdir_ok` is true.
To simulate a plain access, you need to pass `read=True` to associate accesses and `required=False` to allow it not to exist.

If `direct`, dep are built before function returns (cf. note (6)).

If `verbose`, return a `dict` with one entry par dep where:

- The key is the dep name.
- The value is a `dict` composed of:
  - `ok`:       `True` if the dep is built with no error, `False` if the dep is built in error, `None` if the was not built.
  - `checksum`: The checksum computed after the dep (unless `ok` is `None`) (cf. *xxhsum(1)*).

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

During job execution, flags accumulate and are never reset.

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
	then it is preferable to pre-access (using this function) all files before starting the loop.
	The reason is that without this precaution, deps will be discovered one by one and may be built serially instead of all of them in parallel.
- (5):
	If a series of dep is directly derived from the content of a file, it may be wise to declare it as `critical`.
	When a critical dep is modified, open-lmake forgets about deps reported after it.  
	Usually, when a file is modified, this has no influence on the list of files that are accessed after it,
	and open-lmake anticipates this by building these deps speculatively.
	But in some situations, it is almost certain that there will be an influence and it is preferable not to anticipate.
	this is what critical deps are made for: in case of modifications, following deps are not built speculatively.
- (6):
	Using direct deps is not recommanded for general use as it suffers 2 drawbacks:

	- successive calls leads to serial jobs (as each job is analyzed once the previous has completed)
	- this may generate dead-lock if the calling job holds resources (which is usually the case) as such resources are kept while the job is waiting

	This flag is meant in exceptional situations such as a dichotomy search in which a dep is necessary at each step of the dichotomy.
	In that case, using the `direct` flag reduces the number of reruns, which can occur for each step otherwise.
	In that case, it is most probably wise to use the `critical` flag simultaneously.

### [`encode( file , ctx , val , min_length=1 )`](unit_tests/codec.html#:~:text=code%20%3D%20lmake%2Eencode%28%20f%27%7Bos%2Egetcwd%28%29%7D%2Fcodec%5Ffile%27%20%2C%20%27ctx%27%20%2C%20File%2B%27%5Fpy%5Cn%27%20%2C%203%20%29)

If a code is associated to `val` within file `file` and context `ctx`, return it.
Else a code is created, of length at least `min_length`, is associated to `val` and is return.

`file` (symbolic links are followed) may be either a source file within repo or a dir (ending with `'/'`).
In the latter case, such a dir must lie within a source dir and must contain a file `LMAKE/config.py` containing definitions for :

- `file_sync` : one of `none`, `dir` (default) or `sync` for choosing the method to ensure proper consistent operations.
- `perm`      : one of `none`, `group` or `other` which specifies who is given permission to access this shared dir.

Cf. [encode/decode](codec.html).

### `get_autodep()`

Returns whether autodep is currently active or not.

By default, autodep is active.

### [`list_deps( dir=None , regexpr=None )`](unit_tests/list.html#:~:text=lmake%2Elist%5Fdeps%20%28regexpr%3D%27%5Bhw%5D%2E%2A%27%29)

Returns a `tuple` of currently accessed deps.

If `dir` is not `None`, oonly deps lying in the `dir` dir are listed.
And if `regexpr` is not `None`, only if the to-be-reported file (as explained below) matches `regexpr`.

If the cwd lies outside the repo, listed files are absolute.
Else they are relative unless a dep is in an absolute source dir.

The order of the listed deps is the chronological order.

### `list_root(dir)`

Returns a `dir` as used as prefix in `list_deps` and `list_targets`.
This is useful to filter their results.

### [`list_targets( dir=None , regexpr=None )`](unit_tests/list.html#:~:text=lmake%2Elist%5Ftargets%28%29)

Returns a `tuple` of currently generated targets.

If `dir` is not `None`, oonly deps lying in the `dir` dir are listed.
And if `regexpr` is not `None`, only if the to-be-reported file (as explained below) matches `regexpr`.

If the cwd lies outside the repo, listed files are absolute.
Else they are relative.

The order of the listed targets is the chronological order.

### [`mv_target_tree( from_dir , to_dir , regexpr=None )`](unit_tests/target_tree.html#:~:text=lmake%2Emv%5Ftarget%5Ftree%28%27dut2%27%2C%27dut%27%29)

Move generated targets that are inside `from_dir` and match `regexpr` to `to_dir`.
Enclosing dirs becoming empty are removed as well if they are `from_dir` or below it.

Links are copied as is, without effort to make them point to the same place.

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

### [`rm_target_tree( dir , regexpr=None )`](unit_tests/target_tree.html#:~:text=lmake%2Erm%5Ftarget%5Ftree%28%27dut%27%2C%27dut%2Fa%2E%27%29)

Remove generated targets that are inside `dir` and match `regexpr`.
Enclosing dirs becoming empty are removed as well if they are `dir` or below it.

### [`run_cc( *cmd_line , marker='...' , stdin=None )`](examples/cc.dir/Lmakefile.html#:~:text=lmake%2Erun%5Fcc%28%20%27gcc%27%20%2C%20%27%2DMMD%27%20%2C%20%27%2DMF%27%20%2C%20tf%20%2C%20flags%20%2C%20%27%2Dc%27%20%2C%20%27%2Do%27%20%2C%20OBJ%20%2C%20SRC%20%29)

This functions ensures that all dirs listed in arguments such as `-I` or `-L` exist reliably.

`marker` is the name of a marker file which is created in include dirs to guarantee there existence.

`stdin` is the text ot send as the stdin of `cmd_line`.

### `set_autodep(active)`

Set the state of autodep.

### [`target( *targets , follow_symlinks=False , write=False , regexpr=False , allow=True , essential=False , ignore=False , incremental=False , no_warning=False , source_ok=False , critical=False , ignore_error=False , readdir_ok=False , required=False , no_star=True )`](unit_tests/target.html#:~:text=lmake%2Etarget%28%27side%2Epy%27%2Cwrite%3DTrue%2Csource%5Fok%3DTrue%29)

Declare `targets` as targets and alter associated flags.
Note that the `allow` argument default value is `True`.

If `follow_symlinks`, `targets` that are symbolic links are followed (and a dep is set on links themselves, independently of the passed flags that apply for the target the links).

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

Notes:

- (1):
	The same functionality is provided with the `ltarget` executable.

### `xxhsum(text)`

Return a checksum of provided text.

It is a 16-digit hex value with no suffix as a `str`.

Note: this checksum is not the same as the checksum of a file with same content.

Note: this checksum is **not** crypto-robust.

Cf. *xxhsum(1)* for a description of the algorithm.

### [`xxhsum_file(file)`](unit_tests/xxh.html#:~:text=crc%5Fpy%20%3D%20lmake%2Exxhsum%5Ffile%28%27Lmakefile%2Epy%27%29)

Return a checksum of provided file.

The checksum is a `str` containing:

- none                                         if file does not exist, is a dir or a special file
- empty-R                                      if file is empty
- xxxxxxxxxxxxxxxx-R (where x is a hexa digit) if file is regular and non-empty
- xxxxxxxxxxxxxxxx-L                           if file is a symbolic link

Note : this checksum is **not** crypto-robust.

Cf. [xxhsum(1)](man/man1/xxhsum.html) for a description of the algorithm.
