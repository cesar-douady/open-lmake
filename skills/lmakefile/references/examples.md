# Canonical open-lmake examples

**How to access them** (assume the user does NOT have the open-lmake sources on
disk): everything below is rendered on the documentation site —

- unit test `unit_tests/<name>.py` → `https://cesar-douady.github.io/open-lmake/unit_tests/<name>.html`
- example `examples/<dir>/` → `https://cesar-douady.github.io/open-lmake/examples/<dir>/Lmakefile.html`

Fetch those pages on demand rather than trusting memory.

The source repo lives at https://github.com/cesar-douady/open-lmake — you can browse
individual files there, or clone it when you want to grep across the tree:

    git clone https://github.com/cesar-douady/open-lmake.git

The default branch is fine; to match a specific installed version, `git checkout
vXX.YY.Z` (e.g. `v26.02`-series tags). In a checkout the same relative paths apply —
and only a checkout has the examples' `run` driver scripts and the `_lib/ut.py`
test helper (not rendered on the site).

Reading a unit test: every `unit_tests/*.py` file is dual-role — imported as the
`Lmakefile.py` (the `if __name__!='__main__':` branch, the part that interests you)
and executed as a test driver (the `else:` branch) that materializes sources and
asserts job counts via `ut.lmake(target, done=N, new=M, steady=K, ...)`. Knowing
this format is what makes the tests readable as examples.

Reading order:

1. **`examples/hello_world.dir/Lmakefile.py`** — heavily commented fundamentals:
   what makes a class a rule, stems→regex matching, `deps`, shell vs python `cmd`.
   Its `run` script demonstrates touch-vs-change incrementality interactively.
2. **`unit_tests/chain.py`** — minimal 3-rule chain; the diff-compare idiom
   (`.ok` target whose content is the check result — the anti-sentinel).
3. **`unit_tests/basics.py`** — sh/py/Alias rules, inheritance, the `ut.lmake`
   assertion style, `lshow`/`ldebug` smoke usage.
4. **`unit_tests/depend.py`** — dynamic deps, `ldepend` vs plain access, iterating
   over autodep backends; the `step.py` idiom for testing rebuild logic.
5. **`unit_tests/ignore.py`** — the full dep/target flag vocabulary
   (`ignore`/`incremental`/`source_ok`), static form and dynamic form
   (`ltarget --ignore`, `lmake.target(..., ignore=True)`).
6. **`unit_tests/critical.py`** — `critical` deps, static and dynamic; what it
   changes in rebuild scheduling.
7. **`unit_tests/dyn.py`** — the reference for dynamic attributes: nearly every
   attribute as a function (`deps()`, `resources()`, `environ*`, `autodep()`...).
8. **`unit_tests/dyn_resources.py`** — resources/env as callables reading files;
   shows which reads do and don't force reruns.
9. **`unit_tests/cache.py`** — cache config, per-rule `cache=`, `compression`,
   hit/miss assertions (`hit_done`), `lcache_repair`.
10. **`unit_tests/codec.py`** — `lencode`/`ldecode` for stable short names of long
    parameter sets.
11. **`unit_tests/sub_repos.py`** — nested repos via `lmake.config.sub_repos`.
12. **`unit_tests/conan.py`** and **`unit_tests/cargo.py`** — the two stateful-tool
    integration patterns (venv-as-star-target + `readdir_ok` + tool cache disabled;
    incremental side-target scratch). Detailed in `stateful-tools.md`.
13. **`examples/cc.dir/Lmakefile.py`** — the capstone: realistic C/C++ flow in ~160
    lines — `lmake.run_cc` include tracking, transitive-closure computation, link
    rule, `compile_commands.json` generation for IDEs, and a scatter/gather
    regression harness (star target `{Test*}` exploding a test list; gather via
    `ldepend` with `critical`). Its `run` script walks through incremental behavior.

Backends: `unit_tests/slurm_local.py` (readable slurm/local config),
`unit_tests/sge.py`. Advanced dynamic dep-scanning: `examples/cpp20_modules.dir/`
(logical→physical name mapping via symlink rules, `gcc -M` scanner rule,
`check_deps` for perf) — niche but the best reference for "the dep graph itself is
computed by rules".

Documentation map (chapters at `https://cesar-douady.github.io/open-lmake/<name>.html`;
sources under `doc/src/` in a checkout): `intro` (worked example, best mental
model), `rules` (exhaustive attribute reference), `writing_lmakefile` (file
structure, inheritance/combine), `execution` + `autodep` (read/write semantics,
dir-read rules, error conditions), `data_model` + `rule_selection`
(buildable/dangling, how a target picks its rule), `critical_deps`, `cache`,
`backends`, `codec`, `glossary` (precise vocabulary: dangling, steady, manual,
quarantine, uniquify, official job).
