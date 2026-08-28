# Good practices

open-lmake is not make/CMake/Bazel with a different syntax: it is a functional build system, and most reflexes learned from other build systems work against it
(see [coming from other build systems](https://cesar-douady.github.io/open-lmake/from_other_build_systems.html) for a reflex-by-reflex mapping).
There are not many pitfalls to avoid here; there is mostly a habit to unlearn: working around pitfalls that do not exist.
Simple things work very well; write the simple thing first.

Design order: (1) what is my flow, (2) what depends on what, (3) how are my dirs and files organized, (4) how do I explain it to open-lmake — in that order.
The Lmakefile transcribes a flow you already understand; it is not where you design it.

A guiding principle: every parameter of a build should live in a file path or in a file content, never in an environment variable or in system state.
To build variant X with seed S, encode X and S in the target's name and let a stem capture them.
This is a design goal, not an enforced invariant: rules can smuggle inputs through `environ`, tool installs or host state —
when debugging a build that behaves differently between users or machines, look precisely for the places where this principle is violated.

## Do

- **Set the granularity at the finest level where things have natural inputs and outputs.**
  One transformation per rule; let the scheduler exploit the parallelism.
  A rule that configures, compiles, links and tests in one `cmd` hides the graph from the scheduler and the cache.
  The inverse excess is equally wrong: do not fragment below the natural input/output boundaries.
  If one tool performs a whole coherent job, one rule running that tool is the right size.
- **Encode variants in target paths.**
  Seed, build type, profile, tool version: stems in the target name (`{mode:release|debug}/{File}.o`), or content of a dep file.
  Asking for a different file is how you ask for a different build.
- **Derive file lists from the manifest, not from the filesystem.**
  To enumerate sources (e.g. all `.cpp` under a dir), write a rule that greps `LMAKE/manifest` (a regular readable file listing all sources) and emits the list as its target.
  This replaces every `glob`/`find`/readdir and is fully tracked: adding a source flows through git → manifest → list → rebuild.
- **For alias or summary targets, prefer a small real target over `phony`.**
  `lmake foo` guarantees exactly one thing: `foo` is up to date.
  A phony `all` does nothing after `rm -rf build` if no source changed — correct behavior (nothing observable would differ), and `lmake -a` is the tool that forces a whole dep tree up to date.
  A tiny real target (a report, a checksum line) gives an honest done/steady status; `AliasRule` covers pure aliases.
- **Use `critical` on deps that are themselves lists of deps** (e.g. a test-suite manifest):
  when the list changes, open-lmake stops speculatively rebuilding the old list and rebuilds the new one in parallel
  (cf. [critical deps](https://cesar-douady.github.io/open-lmake/critical_deps.html)).
  Pair with `lmake.check_deps()` / `lcheck_deps` inside long jobs to fail fast on out-of-date deps.
- **Choose the right environ dict.**
  `environ` is part of the command (change → rerun);
  `environ_resources` is resource-like (change → rerun only failed jobs);
  `environ_ancillary` is traceability only (change → no rerun).
  The user's environment is deliberately ignored; importing values from `lmake.user_environ` is discouraged and, if unavoidable, must go through one of these dicts.
- **Use `lmake.depend()` / `lmake.target()`** (CLI: `ldepend` / `ltarget`) inside jobs for dynamic cases: promote a computed file list to deps, flag accesses.
- **Use `lmake.run_cc(...)`** for C/C++ compilers: it wraps the compiler's `-MMD`-style dep output so include deps are reported precisely.
- **Factor a stems vocabulary in a base class.**
  Define the project's naming regexes (`stems = {...}`) once in a shared base rule; concrete rules inherit it (dict attributes merge along the MRO).
- **Opt into the shared cache per rule** (`cache = '<name>'` + `lmake.config.caches`) for expensive deterministic jobs.
  Set `check_abs_paths` (or use `repo_view`) so the repo's absolute path never leaks into targets — otherwise cache entries are not portable across users and checkouts.
- **Declare resources honestly** (`resources = {'cpu':.., 'mem':.., 'tmp':..}` plus scheduler-specific tokens such as license counts) and pick the backend per rule.
  Round mem values so the scheduler's job classes stay few.

## Anti-patterns

Each entry: the reflex → why it breaks open-lmake → what to do instead.
These are the mistakes most often imported from other build systems or from scripting around the build.

### 1. Sentinel / stamp files

**Reflex:** `touch build.stamp` to mark "step X ran"; other rules depend on the stamp.

**Why it breaks:** open-lmake compares content, not dates — an empty or constant stamp carries no information, so nothing downstream can depend on what the job actually did.
Stamps exist to fake dependency edges that open-lmake computes natively.

**Instead:** make the real output the target.
If the job's value is "it checked something", make the check's report the target (a diff, a log, an `.ok` file whose content is meaningful).
For pure aliases, use the `phony` flag or `AliasRule`.

### 2. Reading directories (glob / find / ls / wildcard)

**Reflex:** `SRCS = glob('src/**/*.cpp')` or `for f in *.c`.

**Why it breaks:** a directory listing depends on build history (which outputs happen to exist), so it cannot be made stable; open-lmake rejects readdir by default for this reason.
With `readdir_ok` you silence the error but own the instability.

**Instead:** a small rule that greps `LMAKE/manifest` and emits the file list as its target; downstream rules depend on that list file.
Reserve `readdir_ok` for trees provably owned by a single rule (a venv built by one job) or `$TMPDIR`.

### 3. Network or filesystem side effects at Lmakefile-evaluation time

**Reflex:** download a dependency, unpack a toolchain, or rewrite a directory while the Lmakefile is being imported ("bootstrap before the build").

**Why it breaks:** evaluating the build graph must be pure — it runs inside the lmake server, is not spied by autodep, is not a job, is not cached, and mutates state that rules then treat as sources.
Two evaluations may disagree; the repo is no longer a function of git state.

**Instead:** downloads are jobs.
Write a rule whose target is the downloaded artifact, pinned by version and checksum (both in the rule or in a config file that is a dep); verify the checksum in `cmd`.
Better: vendor the dependency (git submodule, subrepo) so the manifest owns it.

Special case: a shared rule library cannot be a job's output at all (rules must exist at Lmakefile-evaluation time).
Vendor it when you can — it stays locally editable.
When constraints force fetching it, keep the fetch out of Lmakefile evaluation: an explicit, idempotent, pinned and checksummed setup step,
and accept that fetched rules cannot be patched without republishing or an override mechanism.

### 4. Host-dependent rule definitions

**Reflex:** at import time, probe the machine (`shutil.which('qsub')`, hostname, OS release) and define rules, backends or resources differently.

**Why it breaks:** two users get different build graphs from the same commit — the antithesis of a functional build.
Combined with a shared cache it is unsound.

**Instead:** rule definitions are constant.
Backend unavailability already degrades gracefully (missing daemon → local execution).
If the OS genuinely changes the recipe, make it explicit data: an OS stem in the target path, or a config target whose content is produced by a rule.

### 5. Environment-variable-driven builds

**Reflex:** `CFLAGS`, `BUILD_ID`, `DEBUG=1 lmake ...` read via `os.environ` in rules.

**Why it breaks:** open-lmake deliberately ignores the user's environment — jobs see a controlled env.
Sneaking values in via `lmake.user_environ` reintroduces untracked inputs: builds differ between users with no dep explaining why.

**Instead:** flags and modes are file content (config files as deps) or target-path stems.
If an env value is truly needed (e.g. `DISPLAY` for a GUI test), route it through `environ_resources`/`environ_ancillary` so its tracking status is explicit.

### 6. Monolithic rules

**Reflex:** one rule that configures + compiles + links + tests, or a `cmd` that loops over all files.

**Why it breaks:** the scheduler, incrementality and the cache work at job granularity.
A monolith serializes what could parallelize, reruns everything when one input changes, and produces cache entries too coarse to hit.
It also hides the dependency structure autodep would otherwise map for free.

**Instead:** one transformation per rule, files as the interface between rules,
star stems for scatter (one job per test, seed or variant), a gather rule with `ldepend` + `critical` for the fan-in.

### 7. `ignore` flags as duct tape

**Reflex:** a job writes somewhere unexpected → add `('...', 'ignore')` until the errors stop; put whole tool homes under `ignore`.

**Why it breaks:** `ignore` tells open-lmake to un-see reads and writes.
Jobs stop being pure functions of their recorded deps: reruns may differ, the multi-user cache can return wrong hits,
and you end up adding manual locks to serialize accesses open-lmake can no longer see.
One `ignore` begets another.

**Instead:** declare the writes (`side_targets`, star targets), move scratch to `$TMPDIR`,
or restructure so the tool's state lives inside the job's own target tree
(see [stateful tools](https://cesar-douady.github.io/open-lmake/stateful_tools.html)).
Legitimate `ignore` uses are narrow and cosmetic (e.g. `.pyc` noise) — anything a rebuild could miss must stay tracked.

### 8. Writes outside declared targets

**Reflex:** scratch files next to sources, writing metadata into the source tree, uploading artifacts from inside a build rule — "it's just a temp file".

**Why it breaks:** undeclared writes are errors (or, if ignored, untracked state).
Writes into the source tree make the repo dirty as a side effect of building.
Uploads and publishes inside build rules make the build non-idempotent and non-cacheable.

**Instead:** `$TMPDIR` for scratch (private, wiped, untracked by design).
All persistent outputs under a dedicated build prefix matched by target patterns.
Publishing is a separate explicit rule (or outside the build), never bundled into producing an artifact.

### 9. Reading mutable global state inside rules

**Reflex:** embed `git describe`, the current date, `$BUILD_ID` or the hostname into built artifacts.

**Why it breaks:** the output is no longer a function of tracked inputs; identical deps yield different contents,
defeating checksums, the propagation stop on `steady` jobs, and the cache.

**Instead:** if version info must be embedded, produce it as a target from tracked inputs where possible,
and confine the volatile part to one tiny leaf rule so the non-reproducibility does not contaminate the graph.
Keep artifacts bit-reproducible; attach volatile metadata outside the build.

### 10. In-place updates

**Reflex:** a job "updates" a file it also reads (append to a log, patch a config).

**Why it breaks:** the result depends on prior content — history, not sources.
open-lmake flags read+write of the same file as an error unless `incremental` is claimed,
and `incremental` is a promise (any prior state yields a correct result), not a waiver.

**Instead:** write a new file.
If a tool insists on in-place state, isolate it per job or honor the `incremental` contract for real
(see [stateful tools](https://cesar-douady.github.io/open-lmake/stateful_tools.html)).

### 11. Second build system embedded blindly

**Reflex:** call cmake/sbt/cargo/ninja inside one big rule and let it manage its own graph and cache.

**Why it breaks (partially):** it can be sound — autodep still tracks reads and writes —
but the inner tool's scratch must be declared, its own daemon and cache dirs confined, and the granularity collapses to one job.
Inner tools with out-of-repo homes or background daemons leak state.

**Instead:** acceptable as a transition, with the inner scratch tree declared as an `incremental` star side target,
tool caches disabled or confined to the repo or `$TMPDIR`, and `HomelessRule` to shield `$HOME`.
Long-term, split the inner graph into open-lmake rules when the tool allows it.

### 12. `force = True` / manual reruns as workflow

**Reflex:** "this job must always run" (fetch latest, re-test flaky, poll something).

**Why it breaks:** always-run jobs poison incrementality upstream of everything they feed.
Flakiness and freshness are inputs in disguise.

**Instead:** make the varying input explicit — a seed stem, a dated snapshot file produced outside the build, a pinned version bump.
`force` is for debugging.
