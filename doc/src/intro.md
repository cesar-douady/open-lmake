<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Introduction

To introduce the basic concepts of open-lmake, we will consider a case including:

- C/C++ compilation
- link edition of executables
- tests using test scenari
- test suites containing list of test scenari

This flow is implemented [here](examples/cc.dir/Lmakefile.html).

We will implement such a simple flow with a full decoupling of the flow (as described in `Lmakefile.py`) and project data (as described by the other source files).
In this flow, we will assume that:

- for an executable `foo.exe`, its `main` is located in `foo.c` or `foo.cc`
- for a feature `foo.o`, its interface is in `foo.h` or `foo.hh` and it implementation (if any) is in `foo.c` or `foo.cc`

With these assumptions, which correspond to usual code organization, the list of objects necessary to link an executable can be derived automatically by analyzing the source files.

## The `Lmakefile.py` file

`Lmakefile.py` is the file that describes the flow. It is analogous to `Makefile` when using `make` and is plain python (hence its name).
It is composed of 3 parts:

- config
- sources
- rules

In this introduction, we will use the default config and nothing needs to be put in `Lmakefile.py`.

Open-lmake needs to have an explicit list of the source files.
If your code based is managed by `git`, it will be automatically used to list source files.
We will assume we are in this common case for this introduction.

We are left with rules that really describe the flow.
Rules are python classes inheriting from `lmake.rules.Rule`.

So we must import these:

```python
from lmake.rules import Rule
```

Now, let us start with the compilation rules.

These will look like:

```python
class CompileC(Rule) :
	targets = { 'OBJ' : '{File:.*}.o' }
	deps    = { 'SRC' : '{File}.c'    }
	cmd     = 'gcc -c -o {OBJ} {SRC}'

class CompileCpp(Rule) :
	targets = { 'OBJ' : '{File:.*}.o' }
	deps    = { 'SRC' : '{File}.cc'   }
	cmd     = 'gcc -c -o {OBJ} {SRC}'
```

Notes:

- `targets` define the stems (here `File`) with a regular expression (here `.*`).
- `deps` define the static deps, i.e. the deps that are needed for all jobs of the rule, but upon execution, other deps (such as .h files) may be discovered (and most of the time will).
- `cmd` is an f-string. There is no f-prefix because it is not expanded while reading `Lmakefile.py` but when a job is executed.
- The same is true for `deps` values, these are f-string (without the f-prefix).
- To be selected to build a file, a rule must match the file with one if its targets **and** its static deps must be buildable.
  For example, if `foo.c` exists and `foo.cc` does not, rule CompileC can be selected to build `foo.o` and CompileCC cannot.
- Hidden deps, on the contrary, may or may not be buildable, this is not a problem for open-lmake.
  It may or may not be a problem for the command, depending on how it is written.
- Note the use of 'buildable' in the previous points and instead of 'exist'.
  If a file does not exist and is buildable, it will be built as soon as open-lmake learns its need.
  If a file exists and is not buildable, it will be `rm`'ed if it was produced by an old job, else it will be declared 'dangling' and this is an error condition.
  In this latter case, most of the time, it corresponds to a file that must be added to git.

For the link edition, here it is:

```python
for ext in ('c','cc') :
	class ListObjects(Rule) :
		name    = f'list objects from {ext}'
		targets = { 'LST' : '{File:.*}.lst'   }
		deps    = { 'SRC' : f'{{File}}.{ext}' }
		def cmd() :
			# this is a rather complex code that computes the transitive closure of included files
			# including included files of the .c (or .cc) file for each included .h (or .hh) file
			# such details are outside the scope of this document
			...

class Link(Rule) :
	targets = { 'EXE' : '{File:.*}.exe' }
	deps    = { 'LST' : '{File}.lst'    }
	cmd     = 'gcc -o {EXE} $(cat {LST})'
```

Notes:

- You have the full power of python, including loops, conditionals etc.
- It is not a problem to have several classes defined with the same name (as `ListObjects` here).
  However, to avoid confusion when reporting execution to the user open-lmake refuses to have several rules with the same name.
  The name of a rule is its `name` attribute and defaults to the class name.
- cmd can be either a string, in which case it is interpreted as an f-string and its expanded is run with bash to execute the job,
  or a function, in which case it is called to execute the job
- rule that generate .lst files does not prevent the existence of such files as sources.
  In such a case, the rule will not be executed.

Finally, we need tests :

```python
class Test(Rule) :
	target = '{File:.*}.test_out'
	deps   = { 'SCN' : '{File}.scn' }
	cmd    = '{SCN}'

class TestSuiteExpansion(Rule) :
	targets = { 'SCN'       : '{File:.*}.test_dir/{Test*:.*}.scn' }
	deps    = { 'TEST_SUIT' : '{File}.test_suite'                 }
	def cmd() :
		for line in open(TEST_SUIT) :
			name,scn = line.split(None,1)
			open(SCN(name),'w').write(scn)
			os.chmod(SCN(name),0o755)

class TestSuiteReport(Rule) :
	target = '{File:.*}.test_suit_report'
	deps   = { 'TEST_SUIT' : '{File}.test_suite' }
	def cmd() :
		for line in open(TEST_SUIT) :
			name = line.split(1)[0]
			stdout.write(open(f'{File}.test_dir/{name}.test_out'))
```

Notes:

- One can define `target` rather than `targets`.
  It is a single target and is special in that is is fed by the stdout of `cmd`.
  Although more rarely used, `dep` can be used and feeds `cmd` as its stdin.
- In `TestSuiteExpansion` targets, there is a `*` after `Test`.
  This a so-called 'star-stem'.
  It means that a single execution of the job generates files with diferent values for this star-stem.
- for python `cmd` (when it is a function), targets, deps and stems are accessible as global variables.
  Star-stems are not defined (they would be meaningless) and the corresponding targets are functions instead of variables: you must pass the star-stems as arguments.

Special notes on content based up-to-date definition:

- Open-lmake detects a file has changed if its content has changed, not its date.
- This means that a checksum is computed for all targets generated by all jobs.
- This has a marginal impact on performance: it uses xxh as a checksum lib, it is both of excellent quality (though not crypto-robust) and bracingly fast.
- This is not only an optimization but has consequences on the flow itself.
- Here, we define a test suite as a list of named test scneari.
  When the test suite is modfiied, all scenari are rebuilt, but only those that have actually changed are rerun.
- And this is common case: you often modify the test suite to change a few tests or add a few ones.
  It would be unacceptable to rerun all tests in such case and would require the flow to be organized in another way.
  Yet, defining test suites this way is very comfortable.

## Execution

The first time you execute the flow, you need to execute all steps :

- compile all source files to object files
- link object files to the executable
- run all tests

Once this has been done, what needs to be executed depends on what has been modified:

| What has been modified    | What needs to be re-executed                                         | Notes                                                     |
|---------------------------|----------------------------------------------------------------------|-----------------------------------------------------------|
| nothing                   | nothing                                                              |                                                           |
| a .c (or .cc) source file | compile said source file, link, run all tests                        | if .o does not change, nothing is run any further         |
| a .h (or.hh) include file | compile source files including said .h (or .hh), link, run all tests | if no .o change, nothing is run any further               |
| a test suite file         | run modified/new tests                                               |                                                           |
| a `cmd` in `Lmakefile.py` | run all jobs using corresponding rule and all depending jobs         | depending jobs are only executed if file actually changes |

## Further notes

### Use of the `critical` attribute

Some deps can be declared `critical`.
This has no semantic impact but may be important performance wise.
Open-lmake computes the actual deps of a job while it executes it while ideally, you would need to know them beforehand.
So it considers the known deps (i.e. collected during the last run) as a good approximation of the ones that will be collected during the current run.

The general principle is:

- Rebuild known deps.
- Execute job.
- If new deps appear, rebuild them.
- If one of such new deps changes during this rebuild, rerun job.
- Iterate until no new deps appear.

Suppose now that you have a dep that contains a list of some files that will become deps (such as the `LST` dep in the `Link` rule).
If this list changes, it may very well suppress such a file (an object file in the `Link` case).
This means that a file may be uselessly recompiled.

If a `critical` dep exists, the firt step of the general principle becomes:

- rebuild known deps, except those located after a modified `critical` dep.

The `Link` rule would be better if written as:

```python
class Link(Rule) :
	targets = { 'EXE' : '{File:.*}.exe'               }
	deps    = { 'LST' : ( '{File}.lst' , 'critical' ) }
	cmd = '''
		ldepend --read $(cat {LST})
		lcheck_deps
		gcc -o {EXE} $(cat {LST})
	'''
```

The drawback of using the `critical` attribute is that the job will more often be executed twice.
While often such jobs are fast (as the `TestSuiteReport`), the link phase may be heavier and we would like to avoid executing it twice.
The idea here is:

- `ldepend` creates deps.
- `lcheck_deps` checks that deps accumulated up to the calling point are up-to-date.
- This guarantees that gcc will discover no new deps.

### Use of a base class

Quite often, you want to define a vocabulary of stems.
In our example, we have `File` and `Test`.

We may define a base class to contain these definitions:

```python
class Base(Rule) :
	stems = {
		'File' : '.*'
	.	'Test' : '.*'
	}
```

Then all rules can inherit from `Base` instead of `Rule` and this vocabulary is defined.
