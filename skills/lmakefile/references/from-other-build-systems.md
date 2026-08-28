# Coming from other build systems

Not a migration guide — a de-training table.
Left column: the reflex you (or the codebase you are translating) bring along.
Right column: what open-lmake actually wants.
The numbered references point to the anti-pattern catalog in [good practices](https://cesar-douady.github.io/open-lmake/good_practices.html).

| Your reflex | open-lmake idiom |
|---|---|
| List every dependency of a target by hand (`foo.o: foo.c foo.h bar.h`) | List only *static* deps (what is needed to select the rule, typically the primary input). Headers, includes, imports are discovered by autodep at run time. |
| Generate dep files (`gcc -MMD` + `-include *.d`) and wire them back in | `lmake.run_cc()` if you want compiler-precise reporting, but plain autodep already catches includes. No dep-file plumbing. |
| `.PHONY: all test clean` | `phony` target flag / `AliasRule` for `all`-style names. No `clean` at all: outputs are tracked, a fresh state is always reachable, and stale files are diagnosed (`dangling`), not scrubbed. |
| Stamp/sentinel files to sequence steps | Real outputs as targets; content is the interface. Sequencing emerges from data deps (anti-pattern 1). |
| Timestamps decide staleness; `touch` to force | Content checksums decide. `touch` causes a run that ends `steady` and stops there. To force a rerun, change an input; `lforget` for manual invalidation while debugging. |
| `make -j N`, ordering hacks, `.NOTPARALLEL` | The scheduler owns parallelism; you declare `resources` (cpu/mem/tmp/licenses) per rule and it fills the machine or the grid. Never serialize via fake deps. |
| Recursive make (`$(MAKE) -C subdir`) | One coherent graph. For nested projects, `lmake.config.sub_repos` (experimental) — each subrepo keeps its own Lmakefile but matching stays scoped. |
| A "configure" step that snapshots the system (CMake configure, autoconf) | No global configure phase. Configuration is data: config files as deps, variants as stems in target paths, config-derived files produced by rules. |
| Out-of-source build dirs chosen by the user (`build/`, `cmake -B`) | The repo IS the namespace: all outputs live in-repo under patterns your rules define (e.g. a `BUILD/...` prefix by convention, uppercase like open-lmake's own `LMAKE/` dir). Same name = same content, always. |
| Toolchain found via `$PATH`/`$CC`/`CFLAGS` from the user's shell | The user env is ignored by design. Pin interpreters and tools in rules (`environ`, explicit paths, version-pinned install rules); flags come from dep files. |
| Bazel: enumerate `srcs`/`deps` per package in BUILD files | No per-package enumeration: regex targets + static deps for selection; autodep does the fine grain. Sandbox-like hygiene comes from autodep spying, not from declaring everything. |
| Bazel remote cache / ccache bolted on | Native: `lmake.config.caches` + per-rule `cache=`. Keep outputs free of absolute paths (`check_abs_paths`) so entries are portable across users. |
| Hermeticity via containers per action | Jobs run under autodep spying; for stronger isolation open-lmake has `views`/`repo_view`/`tmp_view`/`chroot_dir` (namespace remapping), still fully tracked. |
| Ninja/gyp/meson: a generator emits the real build graph | No generation step: rules are regex-parameterized classes, so one rule covers the whole family a generator would enumerate. If you are generating rules in a loop, you usually wanted a stem. |
| `$(shell ls src/*.c)` / `file(GLOB ...)` | Never glob. Grep the manifest (`LMAKE/manifest`) in a small rule; depend on its output (anti-pattern 2). |
| Order-only prerequisites (`\| dir`) to create directories | Nothing to do: target directories are handled by open-lmake (`auto_mkdir` exists for legacy chdir-based flows). |
| `.DELETE_ON_ERROR`, partial-output paranoia | Built-in: targets are unlinked before run (unless `incremental`), failed jobs do not pollute, `manual`/`quarantine` protect user edits. |
| `install`/`deploy` bundled in the default target | Keep publishing out of the build graph (or as an explicit separate leaf rule). Build outputs must be reproducible artifacts (anti-patterns 8 and 9). |
| Version/date/host stamped into binaries at every build | Anti-pattern here: it defeats content-based cutoff. Isolate volatile metadata in one leaf, keep artifacts bit-stable (anti-pattern 9). |
| CI pipeline = bash scripts around the build system | Push logic INTO rules (they are tracked, cached, parallel, grid-dispatchable); CI just runs `lmake <goal>`. A target name is a complete, reproducible request. |

Two habits transfer well and should be kept:
writing small composable steps (make-style one-recipe-one-output discipline maps directly onto rules),
and lockfile-based pinning of third-party deps (open-lmake wants the lockfile as a source and the fetches as checksummed rules).
