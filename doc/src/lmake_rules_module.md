<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# The `lmake.rules` module

## Base rules

### [`class Rule`](unit_tests/admin.html#:~:text=class%20Test%28Rule%29%20%3A)

Base class for plain rules.

A class becomes a rule when:

- it inherits, directly or indirectly, from `Rule`
- it has a `target` or `targets` attribute
- it has a `cmd` attribute

### [`class AntiRule`](unit_tests/rust.html#:~:text=class%20AntiRustRust%28AntiRule%29%20%3A%20target%20%3D%20r%27%7B%3A%2E%2A%7D%2Ers%2Ers)

Base class for anti-rules.

A class becomes an anti-rule when:

- it inherits, directly or indirectly, from `AntiRule`
- it has a `target` or `targets` attribute

An anti-rule has no cmd nor deps.

It applies to a file as soon as it matches one of the targets.
In that case, the file is deemed unbuildable.

### [`class SourceRule`](unit_tests/generic_sources.html#:~:text=class%20XSrc%28SourceRule%29%20%3A%20target%20%3D%20r%27%7BFile%3A%2E%2A%7D%2Esrc)

Base class for source-rules.

A class becomes a source-rule when:

- it inherits, directly or indirectly, from `SourceRule`
- it has a `target` or `targets` attribute

A source-rule has no cmd nor deps.

It applies to a file as soon as it matches one of the targets.
In that case, the file is deemed to be a source.

If such a file is required and does not exist, it is an error condition.

## Helper rules

### `class DirtyRule(Rule)`

This class may be used to ignore all writes that are not an official target.

By itself, it is a dangerous class and must be used with care.
It is meant to be a practical way to do trials without having to work out all the details, but in a finalized workflow, it is better to avoid the usage of this class.

### [`class HomelessRule(Rule)`](unit_tests/home.html#:~:text=class%20Homeless1%28HomelessRule%29%20%3A%20target%20%3D%20%27homeless1%27%20cmd%20%3D%20%27%5B%20%24HOME%20%3D%20%24TMPDIR%20%5D%27)

This class sets `$HOME` to `$TMPDIR`.
This is a way to ensure that various tools behave the same way as if they were run for the first time.
By default `$HOME` points to the root of the repo, which permits to put various init code there.

### [`class Py2Rule(Rule)`](unit_tests/python2.html#:~:text=class%20Cat%28Py2Rule%29%20%3A), `class Py3Rule(Rule)` and [`class PyRule(Rule)`](unit_tests/basics.html#:~:text=class%20CatPy%28Cat%2CPyRule%29%20%3A)

These classes may be used as base class for rules that execute python code doing imports.

It manages `.pyc` files.
Also, it provides deps to module source files although python may optimize such accesses and miss deps on dynamically generated modules.

If `cmd` is not a function, and python is called, this last feature is provided if `lmake.import_machinery.fix_import` is called.

Py2Rule is used for python2, Py3Rule is used for python3.
PyRule is an alias for Py3Rule.

### [`class RustRule(Rule)`](unit_tests/rust.html#:~:text=class%20RunRust%28RustRule%29%20%3A)

This class may be used as a base class to execute executable written in rust.

Rust uses a special link mechanism which defeats the default `ld_audit` autodep mechanism.
This base class merely sets the autodep method to `ld_preload` which works around this problem.

### [`class TraceRule(Rule)`](unit_tests/trace_exec.html#:~:text=class%20Dut%28TraceRule%29%20%3A)

This class sets the `-x` flag for shell rules and manage so that traces are sent to stdout rather than stderr.

This allow to suppress the common idiom:

```bash
echo complicated_command
complicated_command
```
