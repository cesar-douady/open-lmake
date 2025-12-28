<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->
<!-- Why open-lmake-->

# Benchmarks

The goal of this benchmark is to compare major build systems with open-lmake.
The build systems evaluated are `make`, `ninja`, `bazel` and open-lmake.
CMake is not run because it uses either `make` or `ninja` as backend.

In addition, a simple `bash` script running jobs statically parallelized.
Of course, for this script, the notion of no-op build is meaningless.
Its main purpose is to serve as a reference.

## Benchmark environment

This benchmark is derived from [Benchmarking the Ninja build system](https://david.rothlis.net/ninja-benchmark/#:~:text=A%20no%2Dop%20build%20takes,versus%201.5s%20for%20Ninja.).

Because the data necessary to run the benchmark are not provided, I have approximately reproduced it:

- 10k executables
- each of them composed of a main C source file and 10 other C source files, for a total of 110k C source files
- as many .h include files
- 10 `#include` directives in each main C source file and 5 #include directives in each other C source file
- C source files are mostly empty (a mere function doing nothing).
- a `all_10000` target depending on all 10k executables.
- a `all_10` target depending on the first 10 executables.

The dir is prepared by running the `unit_tests/bench` unit test.
[unit\_tests/bench](../unit_tests/bench.py) can be inspected to have a detailed understanding.

For each build-system:

- `all_10000` is built a first time from a fresh code base (fresh rebuild)
- all derived files are removed (as well as `bazel`'s cache)
- `all_10000` is rebuilt (full rebuild)
- `all_10000` is rebuilt a second time (full no-op rebuild)
- `all_10` is rebuilt
- `all_10` is rebuilt a second time (partial no-op rebuild)

## used command lines

| build system | command line                                             | comments                                              |
|--------------|----------------------------------------------------------|-------------------------------------------------------|
| bash         | `./run`                                                  | 16 parallel processes and target are built-in         |
| make         | `make -j 16 all_10000`                                   |                                                       |
| ninja        | `ninja -j 16 all_10000`                                  |                                                       |
| bazel        | `bazel build --jobs=16 --spawn_strategy=local all_10000` |                                                       |
| lmake        | `lmake all_10000`                                        | number of parallel jobs is provided in `Lmakefile.py` |

In all cases output is redirected to `/dev/null` to avoid pertubation due to displaying.

## Notes on the benchmark

Thanks to open-lmake [autodep mechanism](autodep.md), there is no deps information in the config file.
Hence the fresh rebuild is longer as deps are discovered and this only pertinent for open-lmake.
For `make`, there is typically a `depend` target to build.
For `ninja` this work would be done by meson or CMake.
For `bazel`, this is done either by hand or by a generator.
However, I have not measured these phases for other build systems.

This flat organization is painless for `make`, `ninja` and open-lmake, but is contrary to `bazel`'s spirit.
And to some extent, this is not unreasonnable: in practice, projects rarely store 100k source files in a single flat dir.
As a consequence, the partial no-op rebuild would be typically much faster with `bazel` because it would only explore the necessary sub-dirs.
This is key for `bazel`'s scalability.

`bazel` can understand loops in its config file.
Although this could have been done here, it would not be representative of a real situation where deps, names etc. are all different for each target, defeating any loop based approach.
As a consequence, all config files have a line for each target, except open-lmake where this information is stored in its dynamic state.  
Also we use built-in rules that may not execute exactly the same compilation command.

The running host has 8 cpu's (including hyper-threading) and the number of parallel jobs is 16, which seems best for all build systems.

## Results

|                                 | `bash` |   `make`   | `ninja` | `bazel` | open-lmake | Comment                                                       |
|---------------------------------|--------|------------|---------|---------|------------|---------------------------------------------------------------|
| fresh rebuild                   |        |            |         |         | 7m 24s     | initial build                                                 |
| full rebuild                    | 5m44s  | **5m 41s** | 5m 46s  | 7m 46s  | 6m 47s     | after erasing all built files and `bazel` cache               |
| full no-op rebuild              |        | 9.594s     | 1.053s  | 5.623s  | **0.774s** | after no modification                                         |
| partial no-op rebuild           |        | 0.775s     | 0.562s  | 5.270s  | **0.047s** | build of a target that only requires exploration of 100 files |
| config file size (lines)        | 120017 | 120008     | 120011  | 120002  | **11**     | note open-lmake contains no dep info in its config            |
| resident memory on full rebuild |        | 467M       | 149M    | 8.1G    | **137M**   |                                                               |

## Notes on the results

I could not reproduce `make`'s catastrophic scalability results mentioned by David Röthlisberger.
Full no-op rebuild takes about 7s where he mentioned around 70s.

`bazel` manages a cache.
Open-lmake manages deps (the cache is not turned on).
Also, content based up-to-date analysis requires frequent checksum computation.
It is normal that such full builds go slightly slower than those without book-keeping (`bash`, `make` and `ninja`).  
As long as these overheads stay reasonable in this extreme case, which they are, this is a pretty good trade-off in view of the value brought by such features.
This benchmark is extreme in this regard because source files are mostly empty, so that the overhead dominates the measurements.  
In practice, on more realistic cases (but more difficult to isolate), such book-keeping overhead is barely noticeable (at least for open-lmake).

Independently of the hierarchical structure, open-lmake only reads the pertinent part of its state, hence the excellent results for the partial no-op rebuild.

The enormous memory consumption of `bazel` may probably be traded off.
This is the result with default settings.
