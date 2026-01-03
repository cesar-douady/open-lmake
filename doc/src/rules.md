<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2026 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Rule attributes

Each attribute is characterized by a few flags:

- How inheritance is handled:
  - None: (ignore values from base classes)
  - python: (normal python processing)
  - Combine: (Combine inherited values with the currently defined one).
- The type.
- The default value.
- Whether it can be defined dynamically from job to job:
  - No
  - Simple: globals include module globals, user attributes, stems and targets, no file access allowed.
  - Full:   globals include module globals, user attributes, stems, targets, deps and resources, file accesses become deps.

When targets are allowed in dynamic values, the `targets` variable is also available as a `dict` of the targets.
Also, if `target` was used to redirect stdout, the `target` variable contains said filename.

Similarly, when deps are allowed in dynamic values, the `deps` variable is also available as the `dict` of the deps.
Also, if `dep` was used to redirect stdin, the `dep` variable contains said filename.

Note that environment variables, as defined in the rule, are not accessible while computing dynamic attributes as they are themselves dynamic.
If accessed, the environment is standard and contains:

| variable           | value                                                                       |
|--------------------|-----------------------------------------------------------------------------|
| `$HOME`            | the root of the repo                                                        |
| `$LD_LIBRARY_PATH` | a suitable value to execute python, as determined when open-lmake was built |
| `$PATH`            | the standard path as set by `/bin/bash` when called with `$PATH` unset      |
| `$PWD`             | the root of the repo                                                        |
| `$PYTHONPATH`      | open-lmake lib dir so that you can for example execute `import lmake`       |
| `$SHLVL`           | `1`                                                                         |
| `$UID`             | the user id of the calling process                                          |
| `$USER`            | the user name associated with `$UID`                                        |

When a type is mentioned as `f-str`, it means that although written as plain `str`, they are dynamically interpreted as python f-strings, as for dynamic values.
This is actually a form of dynamic value.

Note on `f-str`: typically, the necessary variables (such as stems) are not available when python reads the rule, so such attributes cannot be actual python f-strings,
but the python f-string syntax is powerful and intuitive to mention variable parts.
For example for the attribute `cmd`:

- correct: `cmd = 'gcc -o {DST} {SRC}'`
- incorrect: `cmd = f'gcc -o {DST} {SRC}'`

# Dynamic attribute execution

If the value of an attribute (other than `cmd`) is dynamic, it is interpreted within open-lmake, not as a separate process as for the execution of cmd.
This means:

- Such executions are not parallelized, this has a performance impact.
- They are executed within a single python interpreter, this implies restrictions.

Overall, these functions must be kept as simple and fast as possible, in favor of `cmd` which is meant to carry out heavy computations.

The restrictions are the following:

- The following system (or libc) calls are forbidden (trying to execute any of these results in an error):
  - changing dir (`chdir` and the like)
  - spawning processes (fork and the like)
  - exec (execve and the like)
  - modifying the disk (open for writing and the like)
- The environment variables cannot be tailored as is the case with cmd execution (there is no `environ` attribute as there is for `cmd`).
- Modifying the environment variables (via setenv and the like) is forbidden (trying to execute any of these results in an error).
- Altering imported modules is forbidden (e.g. it is forbidden to write to `sys.path`).
  - Unfortunately, this is not checked.
  - `sys.path` is made  `tuple` though, so that common calls such as `sys.path.append` will generate an error.
- `sys.path` is sampled after having read `Lmakefile.py` (while reading rules) and local dirs are filtered out. There are no means to import local modules.
- However, reading local files is ok, as long as `sys.modules` is not updated.
- There is no containers, as for `cmd` execution (e.g. no `repo_view`).
- execution is performed in the top-level root dir.
  - This means that to be used as a sub-repo, all local file accesses must be performed with the sub-repo prefix.
  - This prefix can be found in `lmake.sub_repo`, which contains `.` for the top-level repo.

## Attributes

### [`auto_mkdir`](unit_tests/chdir.html#:~:text=auto%5Fmkdir%20%3D%20step%3D%3D2)

| Inheritance | Type   | Default | Dynamic | Example |
|-------------|--------|---------|---------|---------|
| python      | `bool` | `False` | Full    | `True`  |

When this attribute has a true value, executing a `chdir` syscall (e.g. executing `cd` in bash) will create the target dir if it does not exist.

This is useful for scripts in situations such as:

- The script does `chdir a`.
- Then try to read file `b` from there.
- What is expected is to have a dep on `a/b` which may not exist initially but will be created by some other job.
- However, if dir `a` does not exist, the `chdir` call fails and the file which is open for reading is `b` instead of `a/b`.
- As a consequence, no dep is set for `a/b` and the problem will not be resolved by a further re-execution.
- Setting this attribute to true creates dir `a` on the fly when `chdir` is called so that it succeeds and the correct dep is set.

### [`autodep`](unit_tests/ptrace.html#:~:text=autodep%20%3D%20%27ptrace)

| Inheritance | Type    | Default                                       | Dynamic | Example    |
|-------------|---------|-----------------------------------------------|---------|------------|
| python      | `f-str` | `'ld_audit'` if supported else `'ld_preload'` | Full    | `'ptrace'` |

This attribute specifies the method used by [autodep](autodep.html) to discover hidden deps.

### [`backend`](unit_tests/slurm.html#:~:text=backend%20%3D%20%27slurm)

| Inheritance | Type    | Default | Dynamic | Example   |
|-------------|---------|---------|---------|-----------|
| python      | `f-str` | -       | Full    | `'slurm'` |

This attribute specifies the [backend](backends.html) to use to launch jobs.

### [`cache`](unit_tests/cache.html#:~:text=cache%20%3D%20%27my%5Fcache)

| Inheritance | Type    | Default | Dynamic | Example |
|-------------|---------|---------|---------|---------|
| python      | `f-str` | -       | Simple  |         |

This attribute specifies the cache to use for jobs executed by this rule.

When a job is executed, its results are stored in the cache.
If space is needed (all caches are constrained in size), any other entry can be replaced.
The cache replacement policy (described in its own section, in the config chapter) tries to identify entries that are likely to be useless in the future.

### [`check_abs_paths`](unit_tests/check_abs_paths.html#:~:text=Bad%28Rule%29%20%3A-,check%5Fabs%5Fpaths%20%3D%20True)

| Inheritance | Type   | Default | Dynamic | Example |
|-------------|--------|---------|---------|---------|
| python      | `bool` | `False` | Full    | `True`  |

This attribute specifies whether absolute paths towards the repo are acceptable in targets.

If true, the physical absolute path of the repo is search in targets and an error is generated if found.

This is particularly useful if the `cache` attribute is set to avoid the absolute path of a repo to appear as target in another repo.
There are 2 drawbacks though:

- Such absolute path may appear by chance in a (typically binary) target such as a jpeg file.
- There is performance cost due to searching the pattern.

If such an error occurs and modifying `cmd` to avoid generating such absolute paths is not feasible or too difficult,
it is possible to use the `repo_view` attribute so that the physical absolute path of the repo will naturally be replaced by its view.
Note however that such absolute path may come from a dep in which case it is the rule that generated it in the first place that must have the `repo_view` attribute set.

### [`cmd`](unit_tests/basics.html#:~:text=class%20CatSh,f%2Eread%28%29%2Cend%3D%27%27%29)

| Inheritance | Type     | Default     | Dynamic | Example                                                            |
|-------------|----------|-------------|---------|--------------------------------------------------------------------|
| Combined    | `f-str`  | -           | Full    | `'gcc -c -o {OBJ} {SRC}'`                                          |
| Combined    | function | -           | Full    | `def cmd() : subprocess.run(('gcc','-c','-o',OBJ,SRC,check=True))` |

#### [if it is a function](unit_tests/basics.html#:~:text=def%20cmd%28%29%20%3A)

In that case, this attribute is called to run the job (cf. [job execution](job_execution.html)).
Combined inheritance is a special case for `cmd`.

If several definitions exist along the MRO, They must all be functions and they are called successively in reverse MRO.
The first (i.e. the most basic) one must have no non-defaulted arguments and will be called with no argument.
The other ones may have arguments, all but the first having default values.
In that case, such `function`'s are called with the result of the previous one as unique argument.
Else, if a `function` has no argument, the result of the previous function is dropped.

Because jobs are executed remotely using the interpreter mentioned in the `python` attribute
and to avoid depending on the whole `Lmakefile.py` (which would force to rerun all jobs as soon as any rule is modified),
these functions and their context are serialized to be transported by value.
The serialization process may improve over time but as of today, the following applies:

- Basic objects are transported as is : `None`, `...`, `bool`, `int`, `float`, `complex`, `str`, `bytes`.
- `list`, `tuple`, `set` and `dict` are transported by transporting their content. Note that reconvergences (and a fortiori loops) are not handled.
- functions are transported as their source accompanied with their context : global accessed variables and default values for arguments.
- Imported objects (functions and `class`'es and generally all objects with a `__qualname__` attribute) are transported as an `import` statement.
- Builtin objects are transported spontaneously, without requiring any generated code.

Also, care has been taken to hide this transport by value in backtrace and during debug sessions, so that functions appear to be executed where they were originally defined.

Values are captured according to the normal python semantic, i.e. once the `Lmakefile` module is fully imported.
Care must be taken for variables whose values change during the `import` process.
This typically concerns loop indices.
To capture these at definition time and not at the end, such values must be saved somewhere.
There are mostly 2 practical possibilities:

- Declare an argument with a default value. Such default value is saved when the function is defined.
- Define a class attribute. Class attributes are saved when its definition ends, which is before a loop index.

#### [if it is a `f-str`](unit_tests/basics.html#:~:text=%27cat%20%7BFIRST%7D%20%7BSECOND%7D%27)

In that case, this attribute is executed as a shell command to run the job (cf. [job execution](job_execution.html)).
Combined inheritance is a special case for `cmd`.

While walking the MRO, if for a base class `cmd` is defined as a function and it has a `shell` attribute, the value of this attribute is used instead.
The purpose is that it is impossible to combine `str`'s and functions because they use different paradigms.
As a consequence, a base class may want to have 2 implementations, one for subclasses that use python `cmd` and another for subclasses that use shell `cmd`.
For such a base class, the solution is to define `cmd` as a function and set its `shell` attribute to the `str` version.

If several definitions exist along the MRO, They must all be `str`'s and they are run successively in reverse MRO in the same process.
So, it is possible for a first definition to define an environment variable that is used in a subsequent one.

As for other attributes that may be dynamic, `cmd` is interpreted as an f-string.

### `chroot_action`

| Inheritance | Type    | Default | Dynamic | Example       |
|-------------|---------|---------|---------|---------------|
| python      | `f-str` | `None`  | Full    | `'overwrite'` |

When entering a `chroot_dir` with [chroot(2)](https://man7.org/linux/man-pages/man2/chroot.2.html), the user and group databases become the one specified in this chroot dir.
It may not contain entries for the current user, or the network service may not be available in this environment.
In that case, some actions can be carried out by open-lmake to restore names for the user and its group using the `chroot_action` rule attribute.

This attribute specifies which action to carry out.
Supported actions are:

- `'none'` or `None`: do nothing.
- `'overwrite'`     : user name and its associated group name are transported from the native namespace, while losing entries for other users and groups.
This is light performance wise and can be used without fear for performance.

### [`chroot_dir`](unit_tests/chroot.html#:~:text=chroot%5Fdir%20%3D%20%27%7Bimage%5Froot%28Os%29%7D%27)

| Inheritance | Type    | Default | Dynamic | Example          |
|-------------|---------|---------|---------|------------------|
| python      | `f-str` | `None`  | Full    | `'/ubuntu22.04'` |

This attribute defines a dir in which jobs will `chroot` into before execution begins.
It must be an absoluted path.

Note that unless the `repo_view` is set, the repo must be visible under its original name in this chroot environment.

If `None`, `''` or `'/'`, no `chroot` is performed unless required to manage the `tmp_view` and `repo_view` attributes (in which case it is transparent).
However, if `'/'`, [namespaces](namespaces.html) are used nonetheless.

This attribute is typically used to execute the job in a different environment, such as a different OS distribution or release, leveraging a `docker` image.

### [`compression`](unit_tests/cache.html#:~:text=compression%20%3D%20z%5Flvl,-cmd%20%3D%20%27%27%27%20dir)

| Inheritance | Type                                          | Default | Dynamic | Example |
|-------------|-----------------------------------------------|---------|---------|---------|
| python      | `None` or `str` or `int` or `tuple` or `list` | None    | Full    | `1`     |

This attribute specifies the compression used when caching.

- if `None`       , no compression occurs.
- If it is a `str`, it specifies the method and can be `'zstd'` or `'zlib'`, and the level defaults to 1.
- If it is a `int`, it specifies the compression level: from `0` which means no compression to `9` which means best (and slowest) compression.
  The method defaults to `'zstd'` if supported, else to `'zlib'`.
  Note that `1` seems to be a good compromize.
- If a `tuple` or a `list`, it specifies both a method and a level (in that order).

When downloading from cache, the method and level used at upload time is honored.
If the compression method is not supported, no download occurs.

### [`dep`](unit_tests/back_ref.html#:~:text=dep%20%3D%20%27single1%2Bdep%2Bdouble%2Bsingle2%2Bdouble%2Bdep)

| Inheritance | Type                       | Default | Dynamic | Example |
|-------------|----------------------------|---------|---------|---------|
| python      | `str` or `list` or `tuple` | -       | Simple  |         |

This attribute defines an unnamed static dep.

During execution, `cmd` stdin will be redirected to this dep, else it is `/dev/null`.

### [`deps`](unit_tests/chain.html#:~:text=deps%20%3D%20%7B%20%27FIRST%27%20%3A%20%27%7BFile1%7D%27%20%2C%20%27SECOND%27%20%3A%20%27%7BFile2%7D%27%20%7D)

| Inheritance | Type   | Default | Dynamic | Example                  |
|-------------|--------|---------|---------|--------------------------|
| Combined    | `dict` | `{}`    | Simple  | `{ 'SRC' : '{File}.c' }` |

This attribute defines the static deps.
It is a `dict` which associates python identifiers to files computed from the available environment.

They are f-strings, i.e. their value follow the python f-string syntax and semantic
but they are interpreted when open-lmake tries to match the rule (the rule only matches if static deps are buildable, cf. [rule selection](rule_selection.html)).
Hence they lack the initial `f` in front of the string.

Alternatively, values can also be `list` or `tuple` whose first item is as described above, followed by flags.

The flags may be any combination of the following flags, optionally preceded by - to turn it off.
Flags may be arbitrarily nested into sub-`list`'s or sub-`tuple`'s.

| CamelCase     | snake\_case                                                         | Default | Description                                                                                                                     |
|---------------|---------------------------------------------------------------------|---------|-----------------------------------------------------------------------------------------------------|
| `Essential`   | `essential`                                                         | Yes     | This dep will be shown in a future graphic tool to show the workflow, it has no algorithmic effect. |
| `Critical`    | [`critical`](unit_tests/critical.html#:~:text=%27critical)          | No      | This dep is [critical](critical_deps.html).                                                         |
| `IgnoreError` | [`ignore_error`](unit_tests/ignore_err.html#:~:text=%27IgnoreError) | No      | This dep may be in error, job will be launched anyway.                                              |
| `ReaddirOk`   | [`readdir_ok`](unit_tests/wine.html#:~:text=%27readdir%5Fok)        | No      | This dep may be read as a dir (using `readdir` (3)) without error.                                  |
| `Required`    | `required`                                                          | No      | This dep is deemed to be read, even if not actually read by the job.                                |
| `NoStar`      | `no_star`                                                           | Yes     | Accept regexpr-based flags (e.g. from star `side_deps` or `side_targets`)                           |
| `Top`         | `top`                                        | No | Dep pattern is interpreted relative to the top-level repo, else to the local repo (cf. [subrepos](experimental_subrepos.html)). |

Flag order and dep order are not significative.

Usage notes:

- The  `critical` flag is typically used for deps that contain a list of deps, for example for a test suite.
In that case, if the list changes, speculatively building the old deps is probably not the best strategy.
- The `ignore_error` flag is also typically use for test suites to generate a colored report.
- The `-no_star` flag is used to avoid repetition, as it is generally possible to mention all the flags that must apply to a target,
possibly duplicating flags mentioned in other matching entries (`side_deps` and `side_targets`).
- The `top` flag is used in case of sub-repo, which is an experimental feature.

### [`environ`](unit_tests/env.html#:~:text=environ%20%3D%20%7B%20%27FROM%5FBACKEND%27%20%3A%20%2E%2E%2E%20%2C%20%27FROM%5FRULE%27%20%3A%20%27from%5Frule%27%20%7D)

| Inheritance | Type   | Default | Dynamic | Example                                   |
|-------------|--------|---------|---------|-------------------------------------------|
| Combined    | `dict` | ...     | Full    | `{ 'MY_TOOL_ROOT' : '/install/my_tool' }` |

This attribute defines environment variables set during job execution.

The content of this attribute is managed as part of the job command, meaning that jobs are rerun upon modification.
This is the normal behavior, other means to define environment are there to manage special situations.

The environment in which the open-lmake command is run is ignored so as to favor reproducibility, unless explicitly transported by using value from `lmake.user_environ`.
Hence, it is quite simple to copy some variables from the user environment although this practice is discouraged and should be used with much care.

Except the exception below, the value must be a `f-str`.

If resulting value is `...` (the python ellipsis), the value from the backend environment is used.
This is typically used to access some environment variables set by `slurm`.

If a value contains one of the following strings, they are replaced by their corresponding definitions:

| Key                        | Replacement                                                                   | Comment                                                                                 |
|----------------------------|-------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------|
| `$LMAKE_ROOT`              | The root dir of the open-lmake package                                     | Dont store in targets as this may require cleaning repo if open-lmake installation changes |
| `$PHYSICAL_REPO_ROOT`      | The physical dir of the subrepo                                               | Dont store in targets as this may interact with cached results                          |
| `$PHYSICAL_TMPDIR`         | The physical dir of the tmp dir                                               | Dont store in targets as this may interact with cached results                          |
| `$PHYSICAL_TOP_REPO_ROOT`  | The physical dir of the top-level repo                                        | Dont store in targets as this may interact with cached results                          |
| `$PYTHON`                  | The full absolute path to the standard python (as provided at build time)     |                                                                                         |
| `$PYTHON_LD_LIBRARY_PATH`  | The necessary `$LD_LIBRARY_PATH` to ensure standard python works as expected  |                                                                                         |
| `$PYTHON2`                 | The full absolute path to the standard python2 (as provided at build time)    |                                                                                         |
| `$PYTHON2_LD_LIBRARY_PATH` | The necessary `$LD_LIBRARY_PATH` to ensure standard python2 works as expected |                                                                                         |
| `$REPO_ROOT`               | The absolute dir of the subrepo as seen by job                                |                                                                                         |
| `$SEQUENCE_ID`             | A unique value for each job execution (at least 1)                            | This value must be semantically considered as a random value                            |
| `$SHELL`                   | `/bin/bash`                                                                   | Not really useful, by symmetry with python                                              |
| `$STD_PATH`                | The standard path as provided by bash when $PATH is not set                   | Typically used in the definintion of `$PATH`                                            |
| `$SMALL_ID`                | A unique value among simultaneously running jobs (at least 1)                 | This value must be semantically considered as a random value                            |
| `$TMPDIR`                  | The absolute dir of the tmp dir, as seen by the job                           |                                                                                         |
| `$TOP_REPO_ROOT`           | The absolute dir of the top-level repo, as seen by the job                    |                                                                                         |

By default the following environment variables are defined :

| Variable      | Defined in   | Value                                              | comment                                                   |
|---------------|--------------|----------------------------------------------------|-----------------------------------------------------------|
| `$HOME`       | Rule         | `$TOP_REPO_ROOT`                                   | See above, isolates tools startup from user specific data |
| `$HOME`       | HomelessRule | `$TMPDIR`                                          | See above, pretend tools are used for the first time      |
| `$PATH`       | Rule         | The standard path with `$LMAKE_ROOT/bin:` in front |                                                           |
| `$PYTHONPATH` | PyRule       | `$LMAKE_ROOT/lib`                                  |                                                           |

### [`environ_ancillary`](unit_tests/dyn.html#:~:text=environ%5Fancillary%20%3D%20%7B%20%27VAR%5FANCILLARY%27%20%3A%20file%5Ffunc%20%7D)

| Inheritance | Type   | Default | Dynamic | Example                 |
|-------------|--------|---------|---------|-------------------------|
| Combined    | `dict` | `{}`    | Full    | `{ 'DISPLAY' : ':10' }` |

This attribute defines environment variables set during job execution.

The content of this attribute is not managed, meaning that jobs are not rerun upon modification.

The values undertake the same substitutions as for the `environ` attribute described above.

The environment in which the open-lmake command is run is ignored so as to favor reproducibility, unless explicitly transported by using value from `lmake.user_environ`.
Hence, it is quite simple to copy some variables from the user environment although this practice is discouraged and should be used with much care.

Except the exception below, the value must be a `f-str`.

If resulting value is `...` (the python ellipsis), the value from the backend environment is used.
This is typically used to access some environment variables set by `slurm`.

By default the following environment variables are defined :

| Variable | Defined in | Value               | comment |
|----------|------------|---------------------|---------|
| `$UID`   | Rule       | the user id         |         |
| `$USER`  | Rule       | the user login name |         |

### [`environ_resources`](unit_tests/dyn.html#:~:text=environ%5Fresources%20%3D%20%7B%20%27VAR%5FRESOURCES%27%20%3A%20file%5Ffunc%20%7D)

| Inheritance | Type   | Default | Dynamic | Example                           |
|-------------|--------|---------|---------|-----------------------------------|
| Combined    | `dict` | `{}`    | Full    | `{ 'MY_TOOL_LICENCE' : '12345' }` |

This attribute defines environment variables set during job execution.

The content of this attribute is managed as resources, meaning that jobs in error are rerun upon modification, but not jobs that were successfully built.

The values undertake the same substitutions as for the `environ` attribute described above.

The environment in which the open-lmake command is run is ignored so as to favor reproducibility, unless explicitly transported by using value from `lmake.user_environ`.
Hence, it is quite simple to copy some variables from the user environment although this practice is discouraged and should be used with much care.

Except the exception below, the value must be a `f-str`.

If resulting value is `...` (the python ellipsis), the value from the backend environment is used.
This is typically used to access some environment variables set by `slurm`.

### [`force`](unit_tests/dyn.html#:~:text=force%20%3D%20True)

| Inheritance | Type   | Default | Dynamic | Example |
|-------------|--------|---------|---------|---------|
| python      | `bool` | `False` | Full    | `True`  |

When this attribute is set to a true value, jobs are always considered out-of-date and are systematically rerun if a target is needed.
It is rarely necessary.

### `job_name`

| Inheritance | Type  | Default | Dynamic | Example |
|-------------|-------|---------|---------|---------|
| python      | `str` | ...     | No      |         |

Default is the first target of the most derived `class` in the inheritance hierarchy (i.e. the MRO) having a matching target.

This attribute may exceptionally be used for cosmetic purpose.
Its syntax is the same as target name (i.e. a target with no option).

When open-lmake needs to include a job in a report, it will use this attribute.
If it contains star stems, they will be replaced by `*`'s in the report.

If defined, this attribute must have the same set of static stems (i.e. stems that do not contain `*`) as any matching target.

### `keep_tmp`

| Inheritance | Type   | Default | Dynamic | Example |
|-------------|--------|---------|---------|---------|
| python      | `bool` | `False` | Full    | `True`  |

When this attribute is set to a true value, the temporary dir is kept after job execution.
It can be retreived with `lshow -i`.

Sucessive executions of the same job overwrite the temporary dir, though, so only the content corresponding to the last execution is available.
When this attribute has a false value, the temporary dir is cleaned up at the end of the job execution.

### [`kill_sigs`](unit_tests/kill.html#:~:text=kill%5Fsigs%20%3D%20%282%2C%29%20%23%20SIGKILL%20%289%29%20is%20automatically%20added%20when%20list%20is%20exhausted)

| Inheritance | Type              | Default             | Dynamic | Example |
|-------------|-------------------|---------------------|---------|---------|
| python      | `list` or `tuple` | `(signal.SIGKILL,)` | Full    |         |

This attribute provides a list of signals to send the job when @lmake decides to kill it.

A job is killed when:

- `^C` is hit if it is not necessary for another running `lmake` command that has not received a `^C`.
- When timeout is reached.
- When `check_deps` is called and some deps are out-of-date.

The signals listed in this list are sent in turn, once every second.
Longer interval can be obtained by inserting `0`'s. `0` signals are not sent and anyway, these would have no impact if they were.

If the list is exhausted and the job is still alive, a more agressive method is used.
The process group of the job, as well as the process group of any process connected to a stream we are waiting for, are sent `SIGKILL` signals instead of just the process group of the job.
The streams we are waiting for are `stderr`, and `stdout` unless the `target` attribute is used (as opposed to the `targets` attribute)
in which case `stdout` is redirected to the the target and is not waited for.

Note: some backends, such as slurm, may have other means to manage timeouts. Both mechanisms will be usable.

### [`lmake_root`](unit_tests/chroot.html#:~:text=lmake%5Froot%20%3D%20%27%7Blmake%5Finstall%5Froot%28Os%29%7D%27)

| Inheritance | Type    | Default | Dynamic | Example             |
|-------------|---------|---------|---------|---------------------|
| python      | `f-str` | `None`  | Full    | `'/installs/lmake'` |

This attribute defines the open-lmake installation directory the job will use.
It must be an absoluted path.
If used in conjunction with `chroot_dir`, the directory is searched in the chroot dir first, then, if not found, in the native root.

If `None` or `''` the current installation is used.

This attribute is typically used in conjunction with `chroot_dir` to indicate the installation dir of a suitable open-lmake.

### [`lmake_view`](unit_tests/namespaces.html#:~:text=lmake%5Fview%20%3D%20%27%2F%27%2Blmake%5Fview)

| Inheritance | Type    | Default | Dynamic | Example    |
|-------------|---------|---------|---------|------------|
| python      | `f-str` | `None`  | Full    | `'/lmake'` |

This attribute defines a dir in which jobs will see the top-level dir of open-lmake installation.
This is done by using `mount -rbind` (cf. [namespaces](namespaces.html)) before any chroot if asked to do so.

It must be an absolute path not lying in the temporary dir.

If `None`, `''` or not specified, this dir is not mounted, else, it must be an absolute path.
If it already exists, it must be a (preferably empty) dir, else it must be a top-level name (e.g `/a` but not `/a/b`).

### `max_retries_on_lost`

| Inheritance | Type  | Default | Dynamic | Example |
|-------------|-------|---------|---------|---------|
| python      | `int` | `1`     | No      |         |

This attribute provides the number of allowed retries before giving up when a job is lost.
For example, a job may be lost because of a remote host being misconfigured, or because the job management process (called `job_exec`) was manually killed.

In that case, the job is retried, but a maximum number of retry attemps are allowed, after which the job is considered in error.

### [`max_runs`](unit_tests/stress_codec.html#:~:text=max%5Fruns%20%3D%202)

| Inheritance | Type  | Default | Dynamic | Example |
|-------------|-------|---------|---------|---------|
| python      | `int` | `0`     | No      |         |

The goal is to protect against potential loss of performances if reruns are too frequent.
Unlimited if `0`.
Contrarily to the `max_submits` attribute, cache accesses are not counted when counting runs.

### [`max_stderr_len`](unit_tests/dyn.html#:~:text=def%20max%5Fstderr%5Flen%28%29%20%3A%20if%20step%3D%3D1%20%3A%20raise%20RuntimeError%20return%20File)

| Inheritance | Type  | Default | Dynamic | Example |
|-------------|-------|---------|---------|---------|
| python      | `int` | `100`   | Full    | `1`     |

This attribute defines the maximum number of lines of stderr that will be displayed in the output of `lmake`.
The whole content of stderr stays accessible with the `lshow -e` command.

### [`max_submits`](unit_tests/misc6.html#:~:text=max%5Fsubmits%20%3D%201)

| Inheritance | Type  | Default | Dynamic | Example |
|-------------|-------|---------|---------|---------|
| python      | `int` | `10`    | No      |         |

The goal is to protect agains potential infinite loop cases.
The default value should be both comfortable (avoid hitting it in normal situations) and practical (avoid too many submissions before stopping).
Unlimited if `0`.
Contrarily to the `max_runs` attribute, cache accesses are counted when counting submits.

### [`name`](unit_tests/name.html#:~:text=name%20%3D%20f%27expand%7Bstep%7D%27)

| Inheritance | Type  | Default        | Dynamic | Example |
|-------------|-------|----------------|---------|---------|
| None        | `str` | `cls.__name__` | No      |         |

This attribute specify a name for the rule.
This name is used each time open-lmake needs to mention the rule in a message.

All rules must have a unique name.
Usually, the default value is fine, but you may need to set a name, for example:

```python
for ext in ('c','cc'):
	class Compile(Rule):
		name    = f'compile {ext}'
		targets = { 'OBJ' : '{File:.*}.o' }
		deps    = { 'SRC' : f'{{File}}.{ext}' }
		cmd     = 'gcc -c -o {OBJ} {SRC}'
```

### [`prio`](unit_tests/hide.html#:~:text=prio%20%3D%201)

| Inheritance | Type    | Default       | Dynamic | Example |
|-------------|---------|---------------|---------|---------|
| python      | `float` | `0` or `+inf` | No      | `1`     |

Default value is 0 if inheriting from `lmake.Rule`, else `+inf`.

This attribute is used to order matching priority.
Rules with higher priorities are tried first and if none of them are applicable, rules with lower priorities are then tried (cf. [rule selection](rule_selection.html)).

### [`python`](unit_tests/python2.html#:~:text=python%20%3D%20%28lmake%2Euser%5Fenviron%5B%27PYTHON2%27%5D%2C%29)

| Inheritance | Type              | Default       | Dynamic | Example            |
|-------------|-------------------|---------------|---------|--------------------|
| python      | `list` or `tuple` | system python | Full    | `venv/bin/python3` |

This attribute defines the interpreter used to run the `cmd` if it is a function.

Items must be `f-str`.

At the end of the supplied executable and arguments, `'-c'` and the actual script is appended, unless the `use_script` attribut is set.
In the latter case, a file that contains the script is created and its name is passed as the last argument without a preceding `-c`.

Open-lmake uses python3.6+ to read `Lmakefile.py`, but that being done, any interpreter can be used to execute `cmd`.
In particular, python2.7 and all revisions of python3 are fully supported.

If simple enough (i.e. if it can be recognized as a static dep), it is made a static dep if it is within the repo.

The first item (the executable path) undergoes the same interpretation of $ keywords as described in [`environ`](#environ).

### [`readdir_ok`](unit_tests/wine.html#:~:text=readdir%5Fok%20%3D%20True)

| Inheritance | Type   | Default | Dynamic | Example |
|-------------|--------|---------|---------|---------|
| python      | `bool` | `False` | Full    | `True`  |

When this attribute has a false value, reading a local dir that is not `ignore`d nor `incremental` is considered an error as the list of files in a dir cannot be made stable,
i.e. independent of the history (the repo is not constantly maintained without spurious files, nor with all buildable files existing).

If it is true, such reading is allowed and it is the user responsibility to ensure that spurious or missing files have no impact on output, once all deps are up-to-date.

### [`repo_view`](unit_tests/namespaces.html#:~:text=repo%5Fview%20%3D%20%27%2F%27%2Brepo%5Fview)

| Inheritance | Type    | Default | Dynamic | Example   |
|-------------|---------|---------|---------|-----------|
| python      | `f-str` | `None`  | Full    | `'/repo'` |

This attribute defines a dir in which jobs will see the top-level dir of the repo (the root dir).
This is done by using `mount -rbind` (cf. [namespaces](namespaces.html)) before any chroot if asked to do so.

It must be an absolute path not lying in the temporary dir.

If `None`, `''` or not specified, this dir is not mounted, else, it must be an absolute path.
If it already exists, it must be a (preferably empty) dir, else it must be a top-level name (e.g `/a` but not `/a/b`).

This attribute is typically used when using tools that handle absolute paths.
This allows such jobs to be repeatable independently of the location of the repo, making for example caching possible.

### [`resources`](unit_tests/resources.html#:~:text=resources%20%3D%20%7B%20%27gnat%27%3A%20%271%27%20%7D)

| Inheritance | Type   | Default | Dynamic | Example                   |
|-------------|--------|---------|---------|---------------------------|
| Combined    | `dict` | `{}`    | Full    | `{ 'MY_RESOURCE' : '1' }` |

This attribute specifies the resources required by a job to run successfully.
These may be cpu availability, memory, commercial tool licenses, access to dedicated hardware, ...

Values must `f-str`.

The syntax is the same as for `deps`.

After interpretation, the `dict` is passed to the `backend` to be used in its scheduling (cf. [local backend](config.html) for the local backend).

### [`shell`](unit_tests/misc19.html#:~:text=shell%20%3D%20Rule%2Eshell%20%2B%20%28%27%2De%27%2C%29)

| Inheritance | Type              | Default     | Dynamic | Example              |
|-------------|-------------------|-------------|---------|----------------------|
| python      | `list` or `tuple` | `/bin/bash` | Full    | `('/bin/bash','-e')` |

This attribute defines the interpreter used to run the `cmd` if it is a `str`.

Items must be `f-str`.

At the end of the supplied executable and arguments, `'-c'` and the actual script is appended, unless the `use_script` attribut is set.
In the latter case, a file that contains the script is created and its name is passed as the last argument without a preceding `-c`.

If simple enough (i.e. if it can be recognized as a static dep), it is made a static dep if it is within the repo.

The first item (the executable path) undergoes the same interpretation of $ keywords as described in [`environ`](#environ).

### [`side_deps`](unit_tests/ignore.html#:~:text=side%5Fdeps%20%3D%20%7B%20%27BAD%27%20%3A%20%28%27bad%5Fdep%27%2C%27ignore%27%29%20%7D)

| Inheritance | Type   | Default | Dynamic | Example |
|-------------|--------|---------|---------|---------|
| Combined    | `dict` | `{}`    | No      |         |

This attribute is used to define flags to deps when they are acquired during job execution.
It does not declare any dep by itself.
Syntactically, it follows the `side_targets` attribute except that:

- Specified flags are dep flags only where `side_targets` accept both target flags an dep flags.
- The flag `Ignore` or `ignore` only applies to reads to prevent such accessed files from becoming a dep where for `side_targets`, this flag prevents files from being deps or targets.
- `.` may be specified as pattern (or pattern may include it as a possible match) which may be necessary when passing the `readdir_ok` flag.

### [`side_targets`](unit_tests/ignore.html#:~:text=side%5Ftargets%20%3D%20%7B%20%27BAD%27%20%3A%20%28%27bad%5Ftarget%27%2C%27ignore%27%29%20%7D)

| Inheritance | Type   | Default | Dynamic | Example |
|-------------|--------|---------|---------|---------|
| Combined    | `dict` | `{}`    | No      |         |

This attribute is identical to `targets` except that:

- Targets listed here do not trigger job execution, i.e. they do not participate to the [rule selection](rule_selection.html) process.
- It not compulsory to use all static stems as this constraint is only necessary to fully define a job when selected by the rule selection process.

### [`start_delay`](unit_tests/misc10.html#:~:text=start%5Fdelay%20%3D%200)

| Inheritance | Type    | Default    | Dynamic | Example |
|-------------|---------|------------|---------|---------|
| python      | `float` | `3`        | Full    |         |

When this attribute is set to a non-zero value, start lines are only output for jobs that last longer than that many seconds.
The consequence is only cosmetic, it has no other impact.

### [`stderr_ok`](unit_tests/misc1.html#:~:text=stderr%5Fok%20%3D%20True)

| Inheritance | Type   | Default | Dynamic | Example |
|-------------|--------|---------|---------|---------|
| python      | `bool` | `False` | Full    | `True`  |

When this attribute has a false value, the simple fact that a job generates a non-empty stderr is an error.
If it is true, writing to stderr is allowed and does not produce an error. The `lmake` output will exhibit a warning, though.

### [`stems`](unit_tests/basics.html#:~:text=stems%20%3D%20%7B%20%27File1%27%20%3A%20r%27%2E%2A%27%20%2C%20%27File2%27%20%3A%20r%27%2E%2A%27%20%7D)

| Inheritance | Type   | Default | Dynamic | Example          |
|-------------|--------|---------|---------|------------------|
| Combined    | `dict` | `{}`    | No      | `{'File':r'.*'}` |

Stems are regular expressions that represent the variable parts of targets which rules match.

Each entry <key>:<value> define a stem named <key> whose associated regular expression is <value>.

### [`target`](unit_tests/basics.html#:~:text=target%20%3D%20%27%7BFile1%7D%2B%7BFile2%7D%5Fsh)

| Inheritance | Type                       | Default | Dynamic | Example |
|-------------|----------------------------|---------|---------|---------|
| python      | `str` or `list` or `tuple` | -       | No      |         |

This attribute defines an unnamed target.
Its syntax is the same as any target entry except that it may not be `incremental`. Also, such a target may not be a `star` target.

During execution, `cmd` stdout will be redirected to this (necessarily unique since it cannot be a `star`) target.

The `top` flag cannot be used and the pattern is always rooted to the sub-repo if the rule is defined in such a sub-repo.

### [`targets`](unit_tests/misc14.html#:~:text=targets%20%3D%20%7B%20%27DUT%27%20%3A%20r%27dut%5Fsh%7BN%2A%3A%5Cd%7D%27%20%7D)

| Inheritance | Type   | Default | Dynamic | Example                  |
|-------------|--------|---------|---------|--------------------------|
| Combined    | `dict` | `{}`    | No      | `{ 'OBJ' : '{File}.o' }` |

This attribute is used to define the regular expression which targets must match to select this rule (cf. [rule selection](rule_selection.html)).

Keys must be python identifiers.
Values are `list`'s or `tuple`'s whose first item defines the target regular expression and following items define flags.
They may also be a simple `str` in which case it is as if there were no associated flags.

The regular expression looks like python f-strings.
The fixed parts (outside `{}`) must match exactly.
The variable parts, called stems, are composed of:

- An optional name.
  If it exists, it is used to ensure coherence with other targets and the `job_name` attribute, else coherence is ensured by position.
  This name is used to find its definition in the stems `dict` and may also be used in the `cmd` attribute to refer to the actual content of the corresponding part in the target.
- An optional `*`.
  If it exists, this target is a star target, meaning that a single job will generate all or some of the targets matching this regular expression.
  if not named, such stem must be defined.
- An optional `:` followed by a definition (a regular expression).
  This is an alternative to refering to an entry in the `stems` `dict`.
  Overall, all stems must be defined somewhere (in the `stems` `dict`, in a target or in `job_name`) and if defined several times, definitions must be identical.
  Also, when defined in a target, a definition must contain balanced `{}`'s, i.e. there must be as many `{` as `}`.
  If a regular expression requires unbalanced `{}`, it must be put in a `stems` entry.

Regular expressions are used with the `DOTALL` flag, i.e. a `.` matches any character, including `\n`.

The flags may be any combination of the following flags, optionally preceded by - to turn it off.
Flags may be arbitrarily nested into sub-`list`'s or sub-`tuple`'s.


| CamelCase   | snake\_case                                                 | Default | Description                                                                                                    |
|-------------|-------------------------------------------------------------|---------|----------------------------------------------------------------------------------------------------------------|
| `Essential` |   `essential`                                               | Yes     | This target will be shown in a future graphic tool to show the workflow, it has no algorithmic effect.         |
| `Incremental` | [`incremental`](unit_tests/incremental.html#:~:text=%27incremental) | No | Previous content may be used to produce these targets.  Cf note (1).                                      |
| `Optional`  |   `optional`                                                | No      | If this target is not generated, it is not deemed to be produced by the job. Cf note (2).                      |
| `Phony`     | [`phony`](unit_tests/regression.html#:~:text=%27phony) | No | Accept that this target is not generated, in which case it is deemed generated even not physically on disk. Cf note (3). |
| `SourceOk`  | [`source_ok`](unit_tests/regression.html#:~:text=%27source%5Fok) | No  | Do not generate an error if target is actually a source                                                       |
| `NoStar`    |  `no_star`                                                       | Yes | Accept regexpr-based flags (e.g. from star `side_deps` or `side_targets`)                                     |
| `NoWarning` | [`no_warning`](unit_tests/repo.html#:~:text=%27no%5Fwarning) | No | Warning is not reported if a target is uniquified or unlinked before job execution while generated by another job. |
| `Top`       |  `top`                                                       | No | Target pattern is interpreted relative to the root dir of the repo, else it is relative to the `cwd` of the rule.  |

All targets must have the same set of static stems (i.e. stems with no `*` in its name).

Matching is done by first trying to match static targets (i.e. which are not star) then star targets.
The first match will provide the associated stem definitions and flags.

Unless the `top` flag is set, the pattern is rooted to the sub-repo if the rule is defined in such a sub-repo.
If the `top` flag is set, the pattern is always rooted at the top-level repo.

Notes:

- (1): In that case, such targets are not unlinked before execution.
However, if targets have non-targets hard links and are not read-only, they are uniquified, i.e. they are copied in place to ensure modification to such targets do not alter other links.
It is the user responsibility to ensure that any previous content will lead to a correct result.
- (2): Open-lmake will try to find an alternative rule.
If none apply or otherwise do not generate this target, then the target is non-buildable.
This is equivalent to being a star target, except that there is no star stem.
- (3): For star targets, all matching files are deemed generated and no alternative rule s searched to produce the file.

Usage notes:

- The `incremental` flag may be used in situation where a target acts as a cache.
The target could be unlinked, but the job is substantially faster if it not and the result is always correct.
For Example, a job could call another build-system and the user is confident that such other build-system is correct.
- The `optional` flag is used when, depending on the content of the deps, a target may or may not be produced.
- The `phony` flag is typically used when a target acts as a symbolique name to mean that some action has been carried out.
It could be for example the target `ALL`, so that `lmake ALL` rebuilds everything while no actual file named `ALL` exists on disk.
- The `-no_star` flag is used to avoid repetition, as it is generally possible to mention all the flags that must apply to a target,
possibly duplicating flags mentioned in other matching entries (`side_deps` and `side_targets`).
- The `top` flag is used in case of sub-repo, which is an experimental feature.

### [`timeout`](unit_tests/wine.html#:~:text=timeout%20%3D%20120)

| Inheritance | Type    | Default    | Dynamic | Example |
|-------------|---------|------------|---------|---------|
| python      | `float` | no timeout | Full    |         |

When this attribute has a non-zero value, job is killed and a failure is reported if it is not done before that many seconds.

### [`tmp_view`](unit_tests/overlay.html#:~:text=tmp%5Fview%20%3D%20%27%2Ftmp)

| Inheritance | Type    | Default | Dynamic | Example  |
|-------------|---------|---------|---------|----------|
| python      | `f-str` | `None`  | Full    | `'/tmp'` |

This attribute defines the name which the temporary dir available for job execution is mounted on (cf. [namespaces](namespaces.html)).

If `None`, `''` or not specified, this dir is not mounted, else, it must be an absolute path.
If view already exists, it must be (preferably empty) dir, else it must be a top-level name (e.g `/a` but not `/a/b`).

### [`use_script`](unit_tests/use_script.html#:~:text=use%5Fscript%20%3D%20True)

| Inheritance | Type   | Default | Dynamic | Example |
|-------------|--------|---------|---------|---------|
| python      | `bool` | `False` | Full    | `True`  |

This attribute commands an implementation detail.

If false, jobs are run by launching the interpreter followed by `-c` and the command text.

If true, jobs are run by creating a temporary file containing the command text, then by launching the interpreter followed by said filename.

If the size of the command text is too large to fit in the command line, this attribute is silently forced to true.

This attribute is typically use with interpreters that do not implement the `-c` option.

### [`views`](unit_tests/overlay.html#:~:text=views%20%3D%20%7B%20%27read%5Fwrite%27%20%3A%20%7B%27upper%27%3A%27write%27%2C%27lower%27%3A%27read%27%7D%20%7D%20%23%20mount%20read%5Fwrite%20as%20write%20on%20top%20of%20read)

| Inheritance | Type   | Default | Dynamic | Example  |
|-------------|--------|---------|---------|----------|
| Combined    | `dict` | `{}`    | Full    |          |

This attribute defines a mapping from logical views to physical dirs.

Accesses to logical views are mapped to their corresponding physical location (non-recursively) before any chroot if requested.
Views and physical locations must be dirs.

Both logical views and physical locations may be local to the repo, within tmp dir or external, but it is not possible to map a local location to an external view (cf. [namespaces](namespaces.html)).

Dirs in the repo or tmp are created as needed.
External views must pre-exist or be top-level names (e.g `/a` but not `/a/b`).

Physical description may be :

- a `f-str` in which case a bind mount is performed.
- a `dict` with keys `upper` (a `f-str`) and `lower` (a single `f-str` or a list of `f-str`) in which case an overlay mount is performed.
  Key `copy_up` (a single `f-str` or a list of `f-str`) may also be used to provide a list of dirs to create in `upper` or files to copy from `lower` to `upper`.
  Dirs are recognized when they end with `/`.
  Such `copy_up` items are provided relative to the root of the view.

### `virtual`

| Inheritance | Type   | Default | Dynamic | Example |
|-------------|--------|---------|---------|---------|
| None        | `bool` | `False` | No      | `True`  |

When this attribute is true, this `class` is not considered as a rule even though it possesses the required attributes.
In that case, it is only used as a base class to define other rules.
