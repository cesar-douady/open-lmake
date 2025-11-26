<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Job execution

Job are executed by calling the provided interpreter (generally python or bash).

When calling the interpreter, the following environment variable are automatically set, in addition to what is mentioned in the `environ` attribute (and the like).
They must remain untouched:

- `$LD_AUDIT`          : A variable necessary for [autodep](autodep.html#:~:text=%24LD%5FAUDIT)   when the `autodep` attribute is set to `'ld_audit'`.
- `$LD_PRELOAD`        : A variable necessary for [autodep](autodep.html#:~:text=%24LD%5FPRELOAD) when the `autodep` attribute is set to `'ld_preload'` or `'ld_preload_jemalloc'`.
- `$LMAKE_AUTODEP_ENV` : A variable necessary for [autodep](autodep.html) in all cases even when the `autodep` attribute is set to `'none'`.
- `$TMPDIR`            : The name of a dir which is empty at the start of the job.
  If the temporary dir is not kept through the use of the `keep_tmp` attribute or the `-t` option, this dir is cleaned up at the end of the job execution.

After job execution, a checksum is computed on all generated files, whether they are allowed or not, except ignored targets (those marked with the `ignore` attribute).

The job is reported ok if all of the following conditions are met:

- Job execution (as mentioned below) is successful.
- All static targets are generated
- All written files are allowed (either appear as target, side target or are dynamically allowed by a call to `ltarget` or `lmake.target`)
- Nothing is written to stderr, or the `stderr_ok` attribute is set.

## [if cmd is a `str`](unit_tests/basics.html#:~:text=class%20CatSh%28Cat%29%20%3A%20target%20%3D%20%27%7BFile1%7D%2B%7BFile2%7D%5Fsh%27%20cmd%20%3D%20%27cat%20%7BFIRST%7D%20%7BSECOND%7D%27)

Because this attribute undergo dynamic evaluation as described in the `cmd` rule attribute, there is not further specificities.

The job execution is successful (but see above) if the interpreter return code is 0.

## [if cmd is a function](unit_tests/basics.html#:~:text=class%20CatPy%28Cat%2CPyRule%29%20%3A%20target%20%3D%20%27%7BFile1%7D%2B%7BFile2%7D%5Fpy%27%20def%20cmd%28%29%20%3A%20for%20fn%20in%20%28FIRST%2CSECOND%29%20%3A%20with%20open%28fn%29%20as%20f%20%3A%20print%28f%2Eread%28%29%2Cend%3D%27%27%29)

In that case, this attribute is called to run the job.

During evaluation, its global `dict` is populated to contain values referenced in these functions.
Values may come from (by order of preference):

- The stems, targets, deps, resources, side targets and side deps, as named in their respective `dict`.
- `stems`, `targets`, `deps`, `resources` that contain their respective whole `dict`.
- if a single target was specified with the `target` attribute, that target is named `target`.
- if a single dep was specified with the `dep` attribute, that dep is named `dep`.
- Any attribute defined in the class, or a base class (as for normal python attribute access).
- Any value in the module globals.
- Any builtin value.
- undefined variables are not defined, which is ok as long as they are not accessed (or they are accessed in a try/except block that handle the `NameError` exception).

Static targets, deps, side targets and side deps are defined as `str`.
Star targets, side targets and side deps are defined as functions taking the star-stems as argument and returning the then fully specified file.
Also, in that latter case, the `reg_expr` attribute is defined as a `str` ready to be provided to the `re` module
and containing named (if corresponding star-stem is named) groups, one for each star-stem.

The job execution is successful (but see above) if no exception is raised.
