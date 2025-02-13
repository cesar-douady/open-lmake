<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# The `lmake.import_machinery` module

### `fix_import()`

This should be called before importing any module that may be dynamically generated.

It updates the import mechanism to ensure that a dep is set to the source file when importing a module, even if such source file does not exist (yet).
Without fix, when a statement `import foo` is executed, although `foo.py` is read if it exists, Python does not attempt to access it if it does not exist.
This is embarassing if `foo.py` is dynamically produced as initially, it does not exist and if no attempt is made to access it, there will be no dep on it and it will not be built.

### `module_suffixes`

This variable holds the list of suffixes used to generate deps when importing a module and `lmake.import_machinery.fix_import()` has been called.

It is better to reduce this list to what is really needed in you flow, i.e. the list of suffixes used for generated python modules
(modules that are sources are not concerned by this list and deps to them will be accurate in all cases).
Reducing this list avoids useless deps.

The default value is the full standard list,
e.g. `('.py','.cpython-312-x86_64-linux-gnu.so','.abi3.so','.so','/__init__.py','/__init__.cpython-312-x86_64-linux-gnu.so','/__init__.abi3.so','/__init__.so')`
for a Python version 3.12 running on Linux with a x86\_64 processor architecture.

A reasonable value would be `('.py','.so','/__init__.py')`, or `('.py','/__init__.py')` if no module are compiled, or even `('.py',)` if no package is generated.
