<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->
<!-- Why open-lmake-->

# Quick comparison

|                                      | make        | ninja            | bazel            | CMake            | open-lmake        | Comment                                                             |
|--------------------------------------|-------------|------------------|------------------|------------------|-------------------|-----------------------------------------------------------|
| [Automatic dependencies](autodep.md) | ❌          | ❌               | ❌               | ❌               | ✅                |                                                           |
| Concurrent builds                    | ❌          | ❌               | ❌ (locked)      | ❌               | ✅                | can safely launch `build a & build b` ?                   |
| Concurrent source editition          | ❌          | ❌               | ❌               | ❌               | ✅                | is it safe to edit a source while building ?              |
| symbolic link support                | ❌          | ❌               | ❌               | ❌               | ✅                | can you use symbolic links and while staying consistent ? |
| `unzip` like job support             | ❌          | ❌               | ❌               | ❌               | ✅                | can the list of targets be content dependent ?            |
| Multiple target                      | ❌          | ✅               | ✅               | ❌               | ✅                | can a single job have several targets ?                   |
| Self-tracking                        | ❌          | ➖ commands only | ✅               | ➖ can detect    | ✅                | handle modifications of the config file ?                 |
| Scalability                          | ➖ <100.000 | ✅ >100.000      | ✅ >1.000.000    | ❓ not a backend | ✅ >1.000.000     | c.f. [benchmarks](benchmark.md)                           |
| content based                        | ❌          | ❌               | ✅               | ❌               | ✅                | rebuild only if dependencies content change ?             |
| Matching                             | ➖ single % | ❌               | ❌               | ❌               | ✅ full regexpr   |                                                           |
| User friendly DSL                    | ❌ specific | ✅ very simple   | ➖ Python subset | ❌ specific      | ✅ Python         |                                                           |
| Remote job execution                 | ❌          | ❌               | ✅               | ❌               | ✅ slurm or SGE   |                                                           |
| inter-user cache                     | ❌          | ❌               | ✅ (no abs path) | ❌               | ✅ (experimental) | can you reuse the result of another user ?                |
| job isolation                        | ❌          | ❌               | ✅ (container)   | ❌               | ✅ (autodep)      |                                                           |
| large recommanded rule set           | ➖          | ❌               | ✅               | ✅               | ❌                |                                                           |
| portable (Windows, mac, Linux)       | ✅          | ✅               | ✅               | ✅               | ❌ (linux only)   |                                                           |

# `make`

`make` used to be an excellent tool when it was first written by [Dr Stuart Feldman](https://en.wikipedia.org/wiki/Stuart_Feldman) almost 50 years ago.
Before there was nothing, and for a long time, until the arrival of `ninja` in the early 2010's (other build systems in the mean time were based on `make`), there was `make`.

Nevertheless, as seen from today, `make` suffers a rather long list of flaws.

## Lack of reliability

`make` tracks only some of the dependencies of a target, for example `make` will not rebuild a target if:
- the recipe to produce it is modified in the makefile.
- its recipe accesses a file which is not declared as a dependency.
- a dependency has been modified while the target was being rebuilt.

Open-lmake handles all such cases:
- It is self-tracking, i.e. it handles correctly the modifications of its configuration.
- It automatically tracks dependencies, leaving no room to the recipe to access files without open-lmake being aware of it.
- It checks dependencies at the end of each job to ensure they were stable througout the whole execution.

## The dependencies nightmare

When you compile a `.c` file to produce a `.o` file, the `.c` file is an obvious dependency.
But most of the time, there are also numerous `.h` files that are less appearant.
Moreover, the list of such `.h` files changes dynamically as the code is modified.

In addition to that, `make` has a static view of dependencies : they are stored in the makefile.

All this makes managing dependencies a nightmare when using make.
This often results in approximations leading to loss of efficiency when over-approximating and loss of reliability when under-approximating.

On the other side, open-lmake only needs the obvious dependencies, for example the `.c` file to build `.o` files.
And even, this is often not even necessary. It is only required in such situations where:

- you have a `foo.c` file but no `foo.cc` file.
- you have compilation rules for C (from `.c` files) and C++ (from `.cc` files).
- you require `foo.o`

In that case, open-lmake needs to know what rule to apply, and for that it needs to know that the first one requires a `.c` file and the second one a `.cc` file.
Because `foo.c` exists and `foo.cc` does not, open-lmake can choose the first rule if statically given the dependency of the direct source.

## Lack of scalability

`make` can reasonably manage hundreds, perhaps thousands of files.
Above that, it will suffer significant lack of performances.

Current builds often need hundreds of thousands to millions of files.
This is completely out of the scope of `make`.
This lead the use of recursive makefiles which have proven to perform very poorly.

On the other hand, open-lmake can easily handle millions of files with no burden.

## Rigidity

To simplify writing makefiles, `make` contains suffix rules (more recently mostly equivalent pattern rules).
This is way too rigid.
So rigid that more modern tools have completely abandonned such genericity.

The actual need is to have more flexibility in defining when a rule applies to produce a given file.

On its side, open-lmake uses full regular expressions to describe matching rules, which provides the necesary flexibility to define any flow a human can reasonably understand.

# ninja

`ninja` is not by itself a build system.
It is advertised as a backend-tool for build systems such as meson and CMake.

`ninja` is also advertised as being extremely fast.
This is true in comparison with `make` (which it aimed to replace).
But this is only true if do not need to rerun the front-end to regenerate its configuration.
It is true that quite often, rerunning the front-end is not necessary.
The problem is that it is up to the user to take this decision, and the user may make mistakes, leading to the classical lack of performance/lack of reliability dilemma.

Finally, `ninja` handles only files that are explicitly mentioned in its configuration.
If you have a big project, with millions of files (sources and derived), but work on a small part of it, say requiring only 1000 of them,
you still need to generate the full configuration, a time consuming process.

If your project is somewhat parametrizable, the overall space can be virtually infinite (with say billions or more possible targets), making this approach completely impracticable.

Open-lmake dynamically generates its internal graph, to the only extent it is needed (its static configuration is made of regular expressions, not list of files).

Moreover, it automatically decides, reliably, when rematching is necessary.
For example, in the case above with 2 rules to produce `.o` files from `.c` and `.cc` files, if you used to have a `foo.c` file but move it to `foo.cc`,
open-lmake will automatically see that the selection of the C compilation rule is not adequate any more and will rematch the `foo.o` file
to find out that the right rule to use is the C++ compilation one.

As with `ninja`, most of the time, this is not necessary, and open-lmake will be at least as fast as `ninja`, with reliability as a bonus.

# bazel

As `ninja`, `bazel` advertises itself as a back-end tool, although in a softer form.
As per its documentation: "It is common for BUILD files to be generated or edited by tools".
This defeats the goal of its DSL Starlark.
Starlark was designed as a subset of Python that provides guarantees (such as repeatability, speed, etc.).
But if a generator is used, the real DSL is the one of the generator, and the guarantees of Starlark do not hold anymore.

Open-lmake allows full Python, avoiding the need for a generator alltogether.

The reliability of dependencies is also left to such a tool, or to the user.
Again, per its documentation:

- "BUILD file writers must explicitly declare all of the actual direct dependencies for every rule to the build system, and no more."
- "Failure to observe this principle causes undefined behavior: the build may fail, but worse, the build may depend on some prior operations,
or upon transitive declared dependencies the target happens to have.
Bazel checks for missing dependencies and report errors, but it's not possible for this checking to be complete in all cases."

In practice, this guarantee is virtually impossible to be provided by a user, leading to incomplete dependency graphs and unreliable results or the use of a front-end tool.

And even, front-end tools are most often partial.
For example, people often use `gcc -M` to generate dependencies.

However, imagine the following scenario:

- you work with `git` (as is quite common)
- you run `gcc -M -Idir1 -Idir2 foo.c` as a job
- foo.c contains `#include "my_lib.h"`
- it turns out `my_lib.h` lies in `dir2`.
- `gcc -M` generates a dependency on `dir2/my_lib.h`
- you do a `git pull` and somebody else has created `dir1/my_lib.h`
- you build `foo.o`

In that case, there is a dependency to `dir2/my_lib.h`, but not to `dir1/my_lib.h`.
Build systems relying on such use of `gcc -M` (including `bazel` as this part is left to the user) will incorrectly consider `foo.o` as up to date.
This is in contradiction with `bazel` advertising "speed through correctness".

Open-lmake correctly maintains a dependency to `dir1/my_lib.h`, in addition to `dir2/my_lib.h`.
More precisely, it maintains a dependency to "`dir1/my_lib.h` must not exist nor be buildable". If this statement does not hold, `foo.o` is rebuilt, as expected.

# CMake

CMake is front-end tool only.
It generates configurations for `make` ou `ninja`.

In addition to flaws inherited from its backends (cf above), CMake syntax is rather obscure and it is difficult to debug.

Also, CMake contains a lot of predefined commands to manage a lot of specific situations.
Such commands should not be part of the build system itself and they participate to the overall complexisty of this tool.

On the contrary, open-lmake is fully generic contains no specific cases to manage such situations.
There is no predefined project organization.
