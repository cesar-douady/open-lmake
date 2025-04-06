<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Rule attributes

Each attribute is characterized by a few flags:

- How inheritance is handled:
  - None: (ignore values from base classes)
  - Python: (normal Python processing)
  - Combine: (Combine inherited values and currently defined one).
- The type.
- The default value.
- Whether it can be defined dynamically from job to job:
  - No
  - Simple: globals include module globals, user attributes, stems and targets, no file access allowed.
  - Full:   globals include module globals, user attributes, stems, targets, deps and resources, file accesses become deps.

When targets are allowed in dynamic values, the `targets` variable is also defined as the `dict` of the targets.
Also, if `target` was used to redirect stdout, the `target` variable contains said file name.

Similarly, when deps are allowed in dynamic values, the `deps` variable is also defined as the `dict` of the deps.
Also, if `dep` was used to redirect stdin, the `dep` variable contains said file name.

When a type is mentioned as `f-str`, it means that although written as plain `str`, they are dynamically interpreted as Python f-strings, as for dynamic values.
This is actually a form of dynamic value.

# Dynamic attribute execution

If the value of an attribute (other than `cmd`) is dynamic, it is interpreted within open-lmake, not as a separate process as for the execution of cmd.
This means:

- Such executions are not parallelized, this has performance impact.
- They are executed within a single Python interpreter, this implies restrictions.

Overall, these functions must be kept as simple and fast as possible, in favor of `cmd` which is meant to carry out heavy computations.

The restrictions are the following:

- The following system (or libc) calls are forbidden (trying to execute any of these results in an error):
  - changing directory (`chdir` and the like)
  - spawning processes (fork and the like)
  - exec (execve and the like)
  - modifying the disk (open for writing and the like)
- The environment variables cannot be tailored as is the case with cmd execution (there is no `environ` attribute as there is for `cmd`).
- Modifying the environment variables (via setenv and the like) is forbidden (trying to execute any of these results in an error).
- Altering imported modules is forbidden (e.g. it is forbidden to write to `sys.path`).
  - Unfortunately, this is not checked.
  - `sys.path` is made  `tuple` though, so that common calls such as `sys.path.append` will generate an error.
- `sys.path` is sampled after having read `Lmakefile.py` (while reading rules) and local directories are filtered out. There are no means to import local modules.
- However, reading local files is ok, as long as `sys.modules` is not updated.
- There is no containers, as for `cmd` execution (e.g. no `repo_view`).

## Attributes

### `name`

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

### `virtual`

| Inheritance | Type   | Default | Dynamic | Example |
|-------------|--------|---------|---------|---------|
| None        | `bool` | `False` | No      | `True`  |

When this attribute is true, this `class` is not a rule even if it has the required target & cmd attributes.
In that case, it is only used as a base class to define other rules.

### `prio`

| Inheritance | Type    | Default       | Dynamic | Example |
|-------------|---------|---------------|---------|---------|
| Python      | `float` | `0` or `+inf` | No      | `1`     |

Default value is 0 if inheriting from `lmake.Rule`, else `+inf`.

This attribute is used to order matching priority.
Rules with higher priorities are tried first and if none of them are applicable, rules with lower priorities are then tried (cf [rule selection](rule_selection.html)).

### `stems`

| Inheritance | Type   | Default | Dynamic | Example          |
|-------------|--------|---------|---------|------------------|
| Combined    | `dict` | `{}`    | No      | `{'File':r'.*'}` |

Stems are regular expressions that represent the variable parts of targets which rules match.

Each entry <key>:<value> define a stem named <key> whose associated regular expression is <value>.

### `job_name`

| Inheritance | Type  | Default | Dynamic | Example |
|-------------|-------|---------|---------|---------|
| Python      | `str` | ...     | No      |         |

Default is the first target of the most derived `class` in the inheritance hierarchy (i.e. the MRO) having a matching target.

This attribute may exceptionally be used for cosmetic purpose.
Its syntax is the same as target name (i.e. a target with no option).

When open-lmake needs to include a job in a report, it will use this attribute.
If it contains star stems, they will be replaced by `*`'s in the report.

If defined, this attribute must have the same set of static stems (i.e. stems that do not contain *) as any matching target.

### `targets`

| Inheritance | Type   | Default | Dynamic | Example                  |
|-------------|--------|---------|---------|--------------------------|
| Combined    | `dict` | `{}`    | No      | `{ 'OBJ' : '{File}.o' }` |

This attribute is used to define the regular expression which targets must match to select this rule (cf [rule selection](rule_selection.html)).

Keys must be Python identifiers.
Values are `list`'s or `tuple`'s whose first item defines the target regular expression and following items define flags.
They may also be a simple `str` in which case it is as if there were no associated flags.

The regular expression looks like Python f-strings.
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


| CamelCase     | snake\_case   | Default | Description                                                                                                                   |
|---------------|---------------|---------|-------------------------------------------------------------------------------------------------------------------------------|
| `Essential`   | `essential`   | Yes     | This target will be shown in a future graphic tool to show the workflow, it has no algorithmic effect.                        |
| `Incremental` | `incremental` | No      | Previous content may be used to produce these targets.  In that case, these are not unlinked before execution. However, if the link count is more than 1, they are uniquified, i.e. they are copied in place to ensure modification to the link does not alter other links. |
| `Optional`    | `optional`    | No      | If this target is not generated, it is not deemed to be produced by the job. Open-lmake will try to find an alternative rule. This is equivalent to being a star target, except that there is no star stem. |
| `Phony`       | `phony`       | No      | Accept that this target is not generated, this target is deemed generated even not physically on disk. If a star target, do not search for an alternative rule to produce the file. |
| `SourceOk`    | `source_ok`   | No      | Do not generate an error if target is actually a source                                                                       |
| `NoUniquify`  | `no_uniquify` | No      | If such a target has several hard links pointing to it, it is not uniquified (i.e. copied in place) before job execution.     |
| `NoWarning`   | `no_warning`  | No      | Warning is not reported if a target is either uniquified or unlinked before job execution while generated by another job.     |
| `Top`         | `top`         | No      | target pattern is interpreted relative to the root directory of the repository, else it is relative to the `cwd` of the rule. |

All targets must have the same set of static stems (i.e. stems with no `*` in its name).

Matching is done by first trying to match static targets (i.e. which are not star) then star targets.
The first match will provide the associated stem definitions and flags.

Unless the `top` flag is set, the pattern is rooted to the sub-repo if the rule is defined in such a sub-repo.
If the `top` flag is set, the pattern is always rooted at the top-level repo.

### `target`

| Inheritance | Type                       | Default | Dynamic | Example |
|-------------|----------------------------|---------|---------|---------|
| Python      | `str` or `list` or `tuple` | -       | No      |         |

This attribute defines an unnamed target.
Its syntax is the same as any target entry except that it may not be `incremental`. Also, such a target may not be a `star` target.

During execution, `cmd` stdout will be redirected to this (necessarily unique since it cannot be a `star`) target.

The `top` flag cannot be used and the pattern is always rooted to the sub-repo if the rule is defined in such a sub-repo.

### `side_targets`

| Inheritance | Type   | Default | Dynamic | Example |
|-------------|--------|---------|---------|---------|
| Combined    | `dict` | `{}`    | No      |         |

This attribute is identical to `targets` except that:

- targets listed here do not trigger job execution, i.e. they do not participate to the [rule selection](rule_selection.html) process.
- it not compulsery to use all static stems as this constraint is only necessary to fully define a job when selected by the rule selection process.

### `deps`

| Inheritance | Type   | Default | Dynamic | Example                  |
|-------------|--------|---------|---------|--------------------------|
| Combined    | `dict` | `{}`    | Simple  | `{ 'SRC' : '{File}.c' }` |

This attribute defines the static dependencies.
It is a `dict` which associates Python identifiers to files computed from the available environment.

They are f-strings, i.e. their value follow the Python f-string syntax and semantic
but they are interpreted when open-lmake tries to match the rule (the rule only matches if static dependencies are buildable, cf [rule selection](rule_selection.html)).
Hence they lack the initial `f` in front of the string.

Alternatively, values can also be `list` or `tuple` whose first item is as described above, followed by flags.

The flags may be any combination of the following flags, optionally preceded by - to turn it off.
Flags may be arbitrarily nested into sub-`list`'s or sub-`tuple`'s.

| CamelCase     | snake\_case    | Default | Description                                                                                                                    |
|---------------|----------------|---------|--------------------------------------------------------------------------------------------------------------------------------|
| `Essential`   | `essential`    | Yes     | This dep will be shown in a future graphic tool to show the workflow, it has no algorithmic effect.                            |
| `Critical`    | `critical`     | No      | This dep is [critical](critical_deps.html).                                                                                    |
| `IgnoreError` | `ignore_error` | No      | This dep may be in error, job will be launched anyway.                                                                         |
| `Required`    | `required`     | No      | This dep is deemed to be read, even if not actually read by the job.                                                           |
| `Top`         | `top`          | No      | dep pattern is interpreted relative to the top-level repo, else to the local repo (cf [subrepos](experimental_subrepos.html ). |

Flag order and dependency order are not significative.

### `dep`

| Inheritance | Type                       | Default | Dynamic | Example |
|-------------|----------------------------|---------|---------|---------|
| Python      | `str` or `list` or `tuple` | -       | Simple  |         |

This attribute defines an unnamed static dependency.

During execution, `cmd` stdin will be redirected to this dependency, else it is `/dev/null`.

### `side_deps`

| Inheritance | Type   | Default | Dynamic | Example |
|-------------|--------|---------|---------|---------|
| Combined    | `dict` | `{}`    | No      |         |

This attribute is used to define flags to deps when they are acquired during job execution.
It does not declare any dep by itself.
Syntactically, it follows the `side_targets` attribute except that:

- specified flags are dep flags rather than target flags.
- an additional flag `Ignore` or `ignore` is available to mean that files matching this pattern must not become deps if accessed as read.

### `chroot_dir`

| Inheritance | Type    | Default | Dynamic | Example          |
|-------------|---------|---------|---------|------------------|
| Python      | `f-str` | `None`  | Full    | `'/ubuntu22.04'` |

This attribute defines a directory in which jobs will `chroot` into before execution begins.
It must be an absoluted path.

Note that unless the `repo_view` is set, the repository must be visible under its original name in this chroot environment.

If `None`, `''` or `'/'`, no `chroot` is performed unless required to manage the `tmp_view` and `repo_view` attributes (in which case it is transparent).
However, if `'/'`, [namespaces](namespaces.html) are used nonetheless.

### `repo_view`

| Inheritance | Type    | Default | Dynamic | Example   |
|-------------|---------|---------|---------|-----------|
| Python      | `f-str` | `None`  | Full    | `'/repo'` |

This attribute defines a directory in which jobs will see the top-level directory of the repo (the root directory).
This is done by using `mount -rbind` (cf [namespaces](namespaces.html)).

It must be an absolute path not lying in the temporary directory.

If `None` or `''`, no bind mount is performed.

As of now, this attribute must be a top level directory, i.e. `'/a'` is ok, but `'/a/b'` is not.

### `tmp_view`

| Inheritance | Type    | Default | Dynamic | Example  |
|-------------|---------|---------|---------|----------|
| Python      | `f-str` | `None`  | Full    | `'/tmp'` |

This attribute defines the name which the temporary directory available for job execution is mounted on (cf [namespaces](namespaces.html)).

If `None`, `''` or not specified, this directory is not mounted.
Else, it must be an absolute path.

As of now, this attribute must be a top level directory, i.e. `'/a'` is ok, but `'/a/b'` is not.

### `views`

| Inheritance | Type   | Default | Dynamic | Example  |
|-------------|--------|---------|---------|----------|
| Combined    | `dict` | `{}`    | Full    |          |

This attribute defines a mapping from logical views to physical directories.

Accesses to logical views are mapped to their corresponding physical location. Views and physical locations may be dirs or files depending on whether they end with a `/` or not.
Files must be mapped to files and directories to directories.

Both logical views and physical locations may be inside or outside the repository, but it is not possible to map an external view to a local location (cf [namespaces](namespaces.html)).

Physical description may be :

- a `f-str` in which case a bind mount is performed.
- a `dict` with keys `upper` (a `str`) and `lower` (a single `str` or a list of `str`) in which case an overlay mount is performed.
  Key `copy_up` (a single `str` or a list of `str`) may also be used to provide a list of directories to create in upper or files to copy from lower to upper.
  Directories are recognized when they end with `/`.
  Such `copy_up` items are provided relative to the root of the view.

### `environ`

| Inheritance | Type   | Default | Dynamic | Example                                   |
|-------------|--------|---------|---------|-------------------------------------------|
| Combined    | `dict` | ...     | Full    | `{ 'MY_TOOL_ROOT' : '/install/my_tool' }` |

This attribute defines environment variables set during job execution.

The content of this attribute is managed as part of the job command, meaning that jobs are rerun upon modification.
This is the normal behavior, other means to define environment are there to manage special situations.

The environment in which the open-lmake command is run is ignored so as to favor reproducibility, unless explicitly transported by using value from `lmake.user_environ`.
Hence, it is quite simple to copy some variables from the user environment although this practice is discouraged and should be used with much care.

Except the exception below, the value must be a `f-str`.

If resulting value is `...` (the Python ellipsis), the value from the backend environment is used.
This is typically used to access some environment variables set by `slurm`.

If a value contains one of the following strings, they are replaced by their corresponding definitions:

| Key                       | Replacement                                                      | Comment                                                                                    |
|---------------------------|------------------------------------------------------------------|--------------------------------------------------------------------------------------------|
| `$LMAKE_ROOT`             | The root directory of the open-lmake package                     | Dont store in targets as this may require cleaning repo if open-lmake installation changes |
| `$PHYSICAL_REPO_ROOT`     | The physical directory of the subrepo                            | Dont store in targets as this may interact with cached results                             |
| `$PHYSICAL_TMPDIR`        | The physical directory of the tmp directory                      | Dont store in targets as this may interact with cached results                             |
| `$PHYSICAL_TOP_REPO_ROOT` | The physical directory of the top-level repo                     | Dont store in targets as this may interact with cached results                             |
| `$REPO_ROOT`              | The absolute directory of the subrepo as seen by job             |                                                                                            |
| `$SEQUENCE_ID`            | A unique value for each job execution (at least 1)               | This value must be semantically considered as a random value                               |
| `$SMALL_ID`               | A unique value among simultaneously running jobs (at least 1)    | This value must be semantically considered as a random value                               |
| `$TMPDIR`                 | The absolute directory of the tmp directory, as seen by the job  |                                                                                            |
| `$TOP_REPO_ROOT`          | The absolute directory of the top-level repo, as seen by the job |                                                                                            |

By default the following environment variables are defined :

| Variable      | Defined in   | Value                                              | comment                                                   |
|---------------|--------------|----------------------------------------------------|-----------------------------------------------------------|
| `$HOME`       | Rule         | `$TOP_REPO_ROOT`                                   | See above, isolates tools startup from user specific data |
| `$HOME`       | HomelessRule | `$TMPDIR`                                          | See above, pretend tools are used for the first time      |
| `$PATH`       | Rule         | The standard path with `$LMAKE_ROOT/bin:` in front |                                                           |
| `$PYTHONPATH` | PyRule       | `$LMAKE_ROOT/lib`                                  |                                                           |

### `environ_resources`

| Inheritance | Type   | Default | Dynamic | Example                           |
|-------------|--------|---------|---------|-----------------------------------|
| Combined    | `dict` | `{}`    | Full    | `{ 'MY_TOOL_LICENCE' : '12345' }` |

This attribute defines environment variables set during job execution.

The content of this attribute is managed as resources, meaning that jobs in error are rerun upon modification, but not jobs that were successfully built.

The values undertake the same substitutions as for the `environ` attribute described above.

The environment in which the open-lmake command is run is ignored so as to favor reproducibility, unless explicitly transported by using value from `lmake.user_environ`.
Hence, it is quite simple to copy some variables from the user environment although this practice is discouraged and should be used with much care.

Except the exception below, the value must be a `f-str`.

If resulting value is `...` (the Python ellipsis), the value from the backend environment is used.
This is typically used to access some environment variables set by `slurm`.

### `environ_ancillary`

| Inheritance | Type   | Default | Dynamic | Example                 |
|-------------|--------|---------|---------|-------------------------|
| Combined    | `dict` | `{}`    | Full    | `{ 'DISPLAY' : ':10' }` |

This attribute defines environment variables set during job execution.

The content of this attribute is not managed, meaning that jobs are not rerun upon modification.

The values undertake the same substitutions as for the `environ` attribute described above.

The environment in which the open-lmake command is run is ignored so as to favor reproducibility, unless explicitly transported by using value from `lmake.user_environ`.
Hence, it is quite simple to copy some variables from the user environment although this practice is discouraged and should be used with much care.

Except the exception below, the value must be a `f-str`.

If resulting value is `...` (the Python ellipsis), the value from the backend environment is used.
This is typically used to access some environment variables set by `slurm`.

By default the following environment variables are defined :

| Variable | Defined in | Value               | comment |
|----------|------------|---------------------|---------|
| `$UID`   | Rule       | the user id         |         |
| `$USER`  | Rule       | the user login name |         |

### `python`

| Inheritance | Type              | Default       | Dynamic | Example            |
|-------------|-------------------|---------------|---------|--------------------|
| Python      | `list` or `tuple` | system python | Full    | `venv/bin/python3` |

This attribute defines the interpreter used to run the `cmd` if it is a function.

Items must be `f-str`.

At the end of the supplied executable and arguments, `'-c'` and the actual script is appended, unless the `use_script` attribut is set.
In the latter case, a file that contains the script is created and its name is passed as the last argument without a preceding `-c`.

Open-lmake uses Python 3.6+ to read `Lmakefile.py`, but that being done, any interpreter can be used to execute `cmd`.
In particular, Python2.7 and all revisions of Python3 are fully supported.

If simple enough (i.e. if it can be recognized as a static dep), it is made a static dep if it is within the repo.

### `shell`

| Inheritance | Type              | Default     | Dynamic | Example              |
|-------------|-------------------|-------------|---------|----------------------|
| Python      | `list` or `tuple` | `/bin/bash` | Full    | `('/bin/bash','-e')` |

This attribute defines the interpreter used to run the `cmd` if it is a `str`.

Items must be `f-str`.

At the end of the supplied executable and arguments, `'-c'` and the actual script is appended, unless the `use_script` attribut is set.
In the latter case, a file that contains the script is created and its name is passed as the last argument without a preceding `-c`.

If simple enough (i.e. if it can be recognized as a static dep), it is made a static dep if it is within the repo.

### `cmd`

| Inheritance | Type     | Default     | Dynamic | Example                                                            |
|-------------|----------|-------------|---------|--------------------------------------------------------------------|
| Combined    | `f-str`  | -           | Full    | `'gcc -c -o {OBJ} {SRC}'`                                          |
| Combined    | function | -           | Full    | `def cmd() : subprocess.run(('gcc','-c','-o',OBJ,SRC,check=True))` |

Whether `cmd` is a `str` or a function, the following environment variable are automatically set, in addition to what is mentioned in the `environ` attribute (and the like).
They must remain untouched:

- `$LD_AUDIT`          : A variable necessary for [autodep](autodep.html) when it is set to `'ld_audit'`
- `$LD_PRELOAD`        : A variable necessary for [autodep](autodep.html) when it is set to `'ld_preload'` or `'ld_preload_jemalloc'`
- `$LMAKE_AUTODEP_ENV` : A variable necessary for [autodep](autodep.html) in all cases
- `$TMPDIR`            : The name of a directory which is empty at the start of the job.
  If the temporary directory is not kept through the use of the `keep_tmp` attribute or the `-t` option, this directory is cleaned up at the end of the job execution.

#### if it is a function

In that case, this attribute is called to run the job.
Combined inheritance is a special case for `cmd`.

If several definitions exist along the MRO, They must all be functions and they are called successively in reverse MRO.
The first (i.e. the most basic) one must have no non-defaulted arguments and will be called with no argument.
The other ones may have arguments, all but the first having default values.
In that case, such `function`'s are called with the result of the previous one as unique argument.
Else, if a `function` has no argument, the result of the previous function is dropped.

During evaluation, when the job runs, its global `dict` is populated to contain values referenced in these functions.
Values may come from (by order of preference):

- The `stems`, `targets`, `deps`, `resources` as named in their respective `dict`.
- `stems`, `targets`, `deps`, `resources` that contain their respective whole `dict`.
- Any attribute defined in the class, or a base class (as for normal Python attribute access).
- Any value in the module globals.
- Any builtin value.
- undefined variables are not defined, which is ok as long as they are not accessed.

Because jobs are executed remotely using the interpreter mentioned in the `python` attribute
and to avoid depending on the whole `Lmakefile.py` (which would force to rerun all jobs as soon as any rule is modified),
these functions and their context are serialized to be transported by value.
The serialization process may improve over time but as of today, the following applies:

- Basic objects are transported as is : `None`, `...`, `bool`, `int`, `float`, `complex`, `str`, `bytes`.
- `list`, `tuple`, `set` and `dict` are transported by transporting their content. Note that reconvergences (and a fortiori loops) are not handled.
- functions are transported as their source accompanied with their context : global accessed variables and default values for arguments.
- Imported objects (functions and `class`'es and generally all objects with a `__qualname__` attribute) are transported as an `import` statement.
- Builtin objects are transported spontaneously, without requiring any generated code.

Values are captured according to the normal Python semantic, i.e. once the `Lmakefile` module is fully imported.
Care must be taken for variables whose values change during the `import` process.
This typically concerns loop indices.
To capture these at definition time and not at the end, such values must be saved somewhere.
There are mostly 2 practical possibilities:

- Declare an argument with a default value. Such default value is saved when the function is defined.
- Define a class attribute. Class attributes are saved when its definition ends, which is before a loop index.

The job is deemed to be successful if no exception is raised.

#### if it is a `f-str`

In that case, this attribute is executed as a shell command to run the job.
Combined inheritance is a special case for `cmd`.

While walking the MRO, if for a base class `cmd` is defined as a function and it has a `shell` attribute, the value of this attribute is used instead.
The purpose is that it is impossible to combine `str`'s and functions because they use different paradigms.
As a consequence, a base class may want to have 2 implementations, one for subclasses that use Python `cmd` and another for subclasses that use shell `cmd`.
For such a base class, the solution is to define `cmd` as a function and set its `shell` attribute to the `str` version.

If several definitions exist along the MRO, They must all be `str`'s and they are run successively in reverse MRO in the same process.
So, it is possible for a first definition to define an environment variable that is used in a subsequent one.

As for other attributes that may be dynamic, `cmd` is interpreted as an f-string.

The job is deemed to be successful if the return code of the overall process is `0`.

### `cache`

| Inheritance | Type    | Default | Dynamic | Example |
|-------------|---------|---------|---------|---------|
| Python      | `f-str` | -       | Simple  |         |

This attribute specifies the cache to use for jobs executed by this rule.

When a job is executed, its results are stored in the cache.
If space is needed (all caches are constrained in size), any other entry can be replaced.
The cache replacement policy (described in its own section, in the config chapter) tries to identify entries that are likely to be useless in the future.

### `compression`

| Inheritance | Type  | Default | Dynamic | Example |
|-------------|-------|---------|---------|---------|
| Python      | `int` | `0`     | Full    | `1`     |

This attribute specifies the compression level used when caching.
It is passed to the zlib library used to compress job targets.

- `0` means no compression.
- `9` means maximum compression.

### `backend`

| Inheritance | Type    | Default | Dynamic | Example   |
|-------------|---------|---------|---------|-----------|
| Python      | `f-str` | -       | Full    | `'slurm'` |

This attribute specifies the [backend](backends.html) to use to launch jobs.

### `autodep`

| Inheritance | Type    | Default                                       | Dynamic | Example    |
|-------------|---------|-----------------------------------------------|---------|------------|
| Python      | `f-str` | `'ld_audit'` if supported else `'ld_preload'` | Full    | `'ptrace'` |

This attribute specifies the method used by [autodep](autodep.html) to discover hidden dependencies.

### `resources`

| Inheritance | Type   | Default | Dynamic | Example                   |
|-------------|--------|---------|---------|---------------------------|
| Combined    | `dict` | `{}`    | Full    | `{ 'MY_RESOURCE' : '1' }` |

This attribute specifies the resources required by a job to run successfully.
These may be cpu availability, memory, commercial tool licenses, access to dedicated hardware, ...

Values must `f-str`.

The syntax is the same as for `deps`.

After interpretation, the `dict` is passed to the `backend` to be used in its scheduling (cf @pxref{local-backend} for the local backend).

### `max_stderr_len`

| Inheritance | Type  | Default | Dynamic | Example |
|-------------|-------|---------|---------|---------|
| Python      | `int` | `100`   | Full    | `1`     |

This attribute defines the maximum number of lines of stderr that will be displayed in the output of `lmake`.
The whole content of stderr stays accessible with the `lshow -e` command.

### `allow_stderr`

| Inheritance | Type   | Default | Dynamic | Example |
|-------------|--------|---------|---------|---------|
| Python      | `bool` | `False` | Full    | `True`  |

When this attribute has a false value, the simple fact that a job generates a non-empty stderr is an error.
If it is `True`, writing to stderr is allowed and does not produce an error. The `lmake` output will exhibit a warning, though.

### `auto_mkdir`

| Inheritance | Type   | Default | Dynamic | Example |
|-------------|--------|---------|---------|---------|
| Python      | `bool` | `False` | Full    | `True`  |

When this attribute has a true value, executing a `chdir` syscall (e.g. executing `cd` in bash) will create the target directory if it does not exist.

This is useful for scripts in situations such as:

- The script does `chdir a`.
- Then try to read file `b` from there.
- What is expected is to have a dependency on `a/b` which may not exist initially but will be created by some other job.
- However, if directory `a` does not exist, the `chdir` call fails and the file which is open for reading is `b` instead of `a/b`.
- As a consequence, no dependency is set for `a/b` and the problem will not be resolved by a further re-execution.
- Setting this attribute to true creates directory `a` on the fly when `chdir` is called so that it succeeds and the correct dependency is set.

### `keep_tmp`

| Inheritance | Type   | Default | Dynamic | Example |
|-------------|--------|---------|---------|---------|
| Python      | `bool` | `False` | Full    | `True`  |

When this attribute is set to a true value, the temporary directory is kept after job execution.
It can be retreived with `lshow -i`.

Sucessive executions of the same job overwrite the temporary directory, though, so only the content corresponding to the last execution is available.
When this attribute has a false value, the temporary directory is cleaned up at the end of the job execution.

### `force`

| Inheritance | Type   | Default | Dynamic | Example |
|-------------|--------|---------|---------|---------|
| Python      | `bool` | `False` | Full    | `True`  |

When this attribute is set to a true value, jobs are always considered out-of-date and are systematically rerun if a target is needed.
It is rarely necessary.

### `max_submits`

| Inheritance | Type  | Default | Dynamic | Example |
|-------------|-------|---------|---------|---------|
| Python      | `int` | `10`    | No      |         |

The goal is to protect agains potential infinite loop cases.
The default value should be both comfortable (avoid hitting it in normal situations) and practical (avoid too many submissions before stopping).

### `timeout`

| Inheritance | Type    | Default    | Dynamic | Example |
|-------------|---------|------------|---------|---------|
| Python      | `float` | no timeout | Full    |         |

When this attribute has a non-zero value, job is killed and a failure is reported if it is not done before that many seconds.

### `start_delay`

| Inheritance | Type    | Default    | Dynamic | Example |
|-------------|---------|------------|---------|---------|
| Python      | `float` | `3`        | Full    |         |

When this attribute is set to a non-zero value, start lines are only output for jobs that last longer than that many seconds.
The consequence is only cosmetic, it has no other impact.

### `kill_sigs`

| Inheritance | Type              | Default             | Dynamic | Example |
|-------------|-------------------|---------------------|---------|---------|
| Python      | `list` or `tuple` | `(signal.SIGKILL,)` | Full    |         |

This attribute provides a list of signals to send the job when @lmake decides to kill it.

A job is killed when:

- `^C` is hit if it is not necessary for another running `lmake` command that has not received a `^C`.
- When timeout is reached.
- When `check_deps` is called and some dependencies are out-of-date.

The signals listed in this list are sent in turn, once every second.
Longer interval can be obtained by inserting `0`'s. `0` signals are not sent and anyway, these would have no impact if they were.

If the list is exhausted and the job is still alive, a more agressive method is used.
The process group of the job, as well as the process group of any process connected to a stream we are waiting for, are sent `SIGKILL` signals instead of just the process group of the job.
The streams we are waiting for are `stderr`, and `stdout` unless the `target` attribute is used (as opposed to the `targets` attribute)
in which case `stdout` is redirected to the the target and is not waited for.

Note: some backends, such as slurm, may have other means to manage timeouts. Both mechanisms will be usable.

### `max_retries_on_lost`

| Inheritance | Type  | Default | Dynamic | Example |
|-------------|-------|---------|---------|---------|
| Python      | `int` | `1`     | No      |         |

This attribute provides the number of allowed retries before giving up when a job is lost.
For example, a job may be lost because of a remote host being misconfigured, or because the job management process (called `job_exec`) was manually killed.

In that case, the job is retried, but a maximum number of retry attemps are allowed, after which the job is considered in error.

### `use_script`

| Inheritance | Type   | Default | Dynamic | Example |
|-------------|--------|---------|---------|---------|
| Python      | `bool` | `False` | Full    | `True`  |

This attribute commands an implementation detail.

If false, jobs are run by launching the interpreter followed by `-c` and the command text.

If true, jobs are run by creating a temporary file containing the command text, then by launching the interpreter followed by said file name.

If the size of the command text is too large to fit in the command line, this attribute is silently forced to true.
