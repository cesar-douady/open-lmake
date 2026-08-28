---
name: lmakefile
description: >
  Write, review, and debug Lmakefiles for open-lmake (a functional build system with
  automatic dependency detection). Use whenever creating or editing Lmakefile.py or an
  Lmakefile/ package, integrating a tool into an open-lmake flow, or answering questions
  about open-lmake rules, deps, targets, caching, backends (SGE/slurm), or autodep.
---

# Writing Lmakefiles for open-lmake

open-lmake is NOT make/CMake/Bazel with different syntax. It is a **functional** build
system and most reflexes learned from other build systems are wrong here. Read
`references/from-other-build-systems.md` if you feel the urge to write a stamp file,
a glob, or an `export CFLAGS`.

There are not many pitfalls to avoid here — mostly there is a habit to unlearn:
working around pitfalls that do not exist. Simple things work very well; write the
simple thing first.

## Mental model (internalize this first)

- **A target's content is a pure function of the rules and the source contents.**
  open-lmake guarantees a stable state: after any sequence of edits, `lmake <target>`
  yields the same content as a fresh clone would. It is pessimistic, never optimistic.
- **Change detection is by content checksum, not date.** If a job reruns and produces
  identical content, downstream jobs do not rerun. Touching a file rebuilds nothing
  downstream (the job reruns, is reported `steady`, and propagation stops).
- **Dependencies are discovered, not listed.** Every job runs under *autodep* (spying
  via ld_audit/ld_preload/ptrace/seccomp): every file **read** becomes a dep, every file
  **written** must match a declared target. You never list header files, imported
  modules, includes — autodep sees them. You only list:
  - *static deps* (`deps = {...}`): the deps needed for **rule selection** — a rule
    only matches a target if its static deps are buildable;
  - *declared targets* (`target` / `targets`): the regex patterns that decide which
    rule builds which file.
- **Rules are Python classes** inheriting `lmake.rules.Rule` (or `PyRule`, `AntiRule`,
  `SourceRule`...). `Lmakefile.py` (or an `Lmakefile/` package) has three parts:
  config (`lmake.config...`), sources (`lmake.manifest`, defaults to `git ls-files`),
  and rules.
- **Targets, deps and cmd are expanded by open-lmake at match/job time** (with
  stems, deps, targets in scope), so they read like f-strings but normally carry no
  `f` prefix: `cmd = 'gcc -o {DST} {SRC}'`. An actual `f` prefix is legal and means
  **two-stage templating**: Python expands once at import time (loop variables,
  module constants), then open-lmake expands the result at job time — double the
  braces (`{{read(DEP)}}`) for everything that must reach lmake. The bug is only
  referencing job-scope names (stems, deps) in single braces under an `f` prefix.
- **Stems** are named regexes forming the variable parts of target patterns:
  `target = '{File:.*}.o'`. All targets of one rule share the same static stems.
  A **star stem** (`'{Test*:[^/]+}.log'`) means one job produces the whole family of
  matching files (scatter); in a python `cmd`, a star target is exposed as a function
  of the star stem.
- **Sources come from git by default.** Unless a `Manifest` file or an explicit
  `lmake.manifest` says otherwise, the source list is `git ls-files` (submodules
  included). Consequence: a freshly created file is NOT a source until it is
  `git add`-ed (`git add -N` / `--intent-to-add` suffices) — the working-tree
  *content* of tracked files is used, but *membership* is git's.
- **A file is *buildable*** if rule selection finds a job for it. A file that exists
  but is neither a source nor buildable is *dangling* — an error (usually means:
  `git add` it). Sources cannot be overwritten by rules (unless `source_ok`).
- **Every parameter of a build SHOULD live in a file path or a file content**, never
  in an environment variable or in system state. To build variant X with seed S,
  encode X and S in the target's name and let a stem capture them. This is the
  design goal, not an enforced invariant: rules can and do smuggle inputs through
  `environ`, tool installs, or host state — when debugging a build that behaves
  differently between users/machines, look precisely for the places where this
  principle is violated.
- **Design order:** (1) what is my flow, (2) what depends on what, (3) how are my
  dirs and files organized, (4) how do I explain it to lmake — in that order. The
  Lmakefile transcribes a flow you already understand; it is not where you design it.

## Golden rules

The full catalog, with rationale and the design method, is in
`references/good-practices.md` — read it before designing a flow. Condensed:

DO — granularity at the finest level with natural inputs/outputs (neither
monoliths nor confetti); variants encoded as stems in target paths; file lists
derived from `LMAKE/manifest`, never from the filesystem; small real targets
instead of `phony` for aliases/summaries (`AliasRule` for pure aliases);
`critical` on list-of-deps files, paired with `lcheck_deps` in long jobs; the
right environ dict (`environ` / `environ_resources` / `environ_ancillary`);
`ldepend`/`ltarget` for dynamic deps/targets; `lmake.run_cc` for C/C++; a shared
stems vocabulary in a base class; per-rule cache opt-in with `check_abs_paths`;
honest `resources` and per-rule `backend`.

DON'T — no sentinel/stamp files; no directory reads (`readdir_ok` only on trees
provably owned by one rule); no side effects and no host probing at
Lmakefile-evaluation time; no env-driven builds; no undeclared writes
(`DirtyRule` is debug-only); no `ignore` on stateful tool homes (see
`references/stateful-tools.md`); no absolute paths baked into outputs; no
`force = True` as workflow.

## Classic pitfalls (LLMs make these constantly)

1. **Misplaced `f`-prefix on `cmd`/`deps`/`targets` patterns.** Expansion happens at
   job time with stems/targets/deps in scope, so plain strings are the default. An
   `f` prefix triggers Python expansion *first* (import time) — deliberate and useful
   for loop variables, but then everything meant for lmake needs `{{doubled}}`
   braces; job-scope names in single braces under `f` are the classic error.
2. **Rules generated in a loop**: every rule needs a unique `name` (default = class
   name), and loop variables are captured at end of import — freeze them as class
   attributes (`class R(Rule): version = v`) or default args, then set
   `name = f'build-{v}'` (this one IS a real f-string, evaluated at definition).
3. **Same set of static stems**: all (non-star) targets of a rule must use exactly the
   same static stems. A target may not be a plain constant while another has stems.
4. **A file that is both read and written** by the same job is auto-`incremental`
   and an error unless it is finally unlinked (or is a source with `source_ok`);
   with `incremental` YOU guarantee any previous content produces a correct result.
5. **stderr is an error by default** (`stderr_ok = True` to allow), and writing to a
   dep of another running job, chdir'ing out of the repo, etc. all have precise
   documented semantics — check the `execution` doc chapter before fighting them.
6. **Uphill rule**: if dir `foo` is buildable as a file, `foo/bar` cannot be built.
   Recursive patterns (`{File}.x` depending on `{File}`) can explode — the `Infinite`
   error; bound them with rule design, not with `max_dep_depth` bumps.
7. **Python `cmd()` gets its context as globals** (stems, deps, targets injected as
   module-level names, star targets as functions) — there is no `self`.
8. **Default to `targets` (plural).** The singular `target` redirects the job's
   stdout into the file, so any stray print or chatty tool corrupts it. Use singular
   only when stdout-as-content is exactly what you want.
9. **Don't `mkdir -p` target directories** — open-lmake creates the dirs of
   identifiable targets before the job runs.
10. **Don't reach for `-j` to "go faster".** `lmake -j N` exists but is a *cap*
    (limits simultaneous jobs per backend), never a boost: parallelism is already
    maximal, driven by declared `resources` and backend configuration. Adding
    `-j16` to a build command is a reflex from make that does nothing good here.
11. **A new file must be `git add`-ed before lmake sees it.** Sources default to
    `git ls-files`, so a just-created test/source file is invisible (its readers
    see a non-buildable dep, or it shows up `dangling`) until `git add` — a bare
    `git add -N <file>` is enough. Create file → `git add` → `lmake`, in that order.

## Architectural facts to design around

- **A job cannot produce a rule.** The whole rule set must exist when `Lmakefile.py`
  is imported. So a shared rule library must be present *before* the build: prefer
  vendoring it (git submodule, subrepo, committed copy) — it stays editable and
  debuggable in place. If organizational constraints force fetching it, do so in an
  explicit, idempotent setup step outside the build (pinned version + checksum, one
  shared bootstrap implementation — not per-repo divergent copies, and never as an
  import-time side effect), knowing the cost: fetched rules can't be patched locally
  without a republish or an override mechanism.
- **Files outside the repo are assumed stable.** open-lmake treats the surrounding
  installation (toolchains, system libs) as identical across users and grid hosts.
  Pin external tool versions explicitly; when an external dir must be tracked, list
  it in the manifest (trailing `/` entries may point outside the repo).

## Verify your work (always do this)

After writing or modifying rules:

1. Run `lmake <a-representative-target>` and read the status lines:
   - `done` = ran OK; `new` = new source seen; `steady` = ran but output identical
     (repeated `steady` on the same job = suboptimal flow, e.g. an unused dep);
   - `dangling` = a file exists that is neither a source nor buildable — most often
     a new file not yet `git add`-ed; otherwise fix the manifest or the rule;
   - `rerun`/`may_rerun` = a dep was discovered out-of-date mid-run (normal once;
     chronic = restructure with `critical`/`check_deps`).
2. Inspect a job with `lshow -i <target>` (info), `lshow -d` (deps as recorded, in
   order), `lshow -t` (targets), `lshow -E` (execution script) — this shows what
   autodep actually saw, which is the ground truth.
3. Check incrementality: touch a source without changing it (`steady`, no downstream
   rebuild), make a semantic change (only the affected subgraph rebuilds).

## References (load on demand)

- `references/rules-cheatsheet.md` — every rule attribute, dep/target flag, base
  class, and the `lmake` module API, in dense form.
- `references/good-practices.md` — design method, the Do list with rationale, and
  the anti-pattern catalog (symptom → why it breaks open-lmake → replacement).
- `references/stateful-tools.md` — integrating conan/pip/venv/cargo/sbt-like stateful
  tools without breaking purity or the shared cache.
- `references/from-other-build-systems.md` — reflex-mapping table from
  make/CMake/Ninja/Bazel habits to open-lmake idioms.
- `references/examples.md` — index of the canonical examples and unit tests inside
  the open-lmake repo, with what each one teaches.
- `references/faq.md` — symptom → explanation → remedy entries from real usage:
  statuses (`done`/`may_rerun`), `optional` vs `phony`, rule identity, the
  list-file idiom, incremental ownership, NFS locking, `lshow`/`ldebug`.

These four (good-practices, stateful-tools, from-other-build-systems, faq) are
also chapters of the official doc site — the book includes these very files, so
skill and doc cannot diverge. Their cross-links use the site URLs; locally they
resolve to the sibling files listed above.

The authoritative documentation lives at https://cesar-douady.github.io/open-lmake/
(rendered book; key chapters: `rules.html`, `execution.html`, `autodep.html`,
`critical_deps.html`, `cache.html`, `glossary.html`). The same site renders every
unit test (`unit_tests/<name>.html`) and example (`examples/<dir>/Lmakefile.html`)
— working, tested Lmakefiles, the best source of idioms. Assume the website is your
only access: users generally do NOT have the open-lmake sources on disk. If a
checkout happens to be available (github.com/cesar-douady/open-lmake), its
`doc/src/*.md` and `unit_tests/*.py` match the installed version, which the website
(tracking `main`) may not.
