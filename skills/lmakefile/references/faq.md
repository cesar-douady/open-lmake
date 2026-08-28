# FAQ

Symptom → explanation → remedy, from real-world usage.

## Targets and rules

### My output is empty or corrupted and I use the singular `target`

Singular `target` redirects the job's stdout into the file —
a stray print, a progress bar, a tool writing status to stdout all corrupt it.
Default to `targets` (plural) + explicit writes;
use singular `target` only when stdout-as-content is the point (then keep the cmd silent otherwise).

### What happens when an `optional` target is not produced?

`optional` is syntactic sugar for a star target with no star stem: the job may or may not produce it.
Depending on an absent optional is normal — a dep on a non-existent file is a real, useful dep
(its *appearance* triggers rebuilds; a large share of recorded deps are non-existent files, e.g. all the include-path probes).
Asking `lmake` for an unproduced optional yields `no rule ... does not produce it`, which means "everything was tried".

### `optional` vs `phony`: which one do I want?

They are two opposite answers to "not produced".
With `optional`, a target the job does not generate is *not produced by this rule*:
open-lmake goes looking for another rule to produce it, and if none does, the file is non-buildable —
a rule having it as a static dep is then not selected.
With `phony`, the target is *actively generated as absent*:
it is deemed produced even with nothing on disk, open-lmake looks no further,
and a rule having it as a static dep can be selected (the dep is satisfied, as an absent file).
Pick `optional` for "this rule may or may not be the producer", `phony` for "absence is a valid produced state".

### Can I have two rules with the same targets?

A rule's identity is its target + dep patterns.
Name, prio, cmd and other attributes are not part of it — you can tune them without losing job history.
Two rules with identical targets+deps are rejected by design (it is almost always a bug).
Prio *layering* over the same pattern space is legitimate and idiomatic —
a generic fallback rule (often with an `optional` target) overridden by more specific higher-prio rules;
what prio cannot do is disambiguate two rules whose targets+deps are strictly identical.

### Do I need to exclude intermediate dirs from a tree-shaped star target (venv, install dir)?

No.
With `targets = {'VENV': r'venv/{*:.*}'}`, an intermediate path like `venv/bin` matches the star too,
and the up-hill check ("to build `a/b`, first ask whether `a` is buildable") does consider it — but this settles by itself:
the job creates `venv/bin` as a *dir*, a dir is "nothing", so `venv/bin` is not generated as a file, hence not buildable,
and `venv/bin/pip` resolves through the same job.
No first-level exclusion, no `AntiRule`
(the [conan](https://cesar-douady.github.io/open-lmake/unit_tests/conan.html) and
[py_venv](https://cesar-douady.github.io/open-lmake/unit_tests/py_venv.html) unit tests use exactly this shape).

The trap is a **symlink** the job drops at an intermediate level — a venv creates `venv/lib64 -> lib` on some distros.
A symlink IS a file, so `venv/lib64` is buildable,
and up-hill then makes any hand-spelled name through it (`venv/lib64/.../foo.py` as a static dep or on the `lmake` command line) non-buildable:
"no rule to make it" while `ls` shows the file.
Runtime accesses are unaffected (autodep resolves to the canonical name and records a dep on each traversed symlink).
Remedy: spell canonical names (`venv/lib/...`) in deps and requests.

### Two of my flows produce files in the same area

Writing another rule's target is a non-sense: every buildable file has exactly one official producing job.
If two flows legitimately produce files in the same area, split the areas (each rule its own responsibility) rather than sharing a target pattern.

## Jobs and cmd

### My job fails and the only output is on stderr

By design, non-empty stderr = error.
If the output is normal, redirect it (`2>&1`) so it lands in `lshow -o`;
reserve `stderr_ok = True` for tools that genuinely cannot be silenced — it is the exception, not a default.

### My test failures are silently swallowed

`cmd = '{BIN} ; report...'` discards `{BIN}`'s exit code.
Use `{BIN} && ...`, or `set -e` at the top, or `shell = ('/bin/bash','-e')` on the rule.

### Where should complex logic live: `{...}` interpolations, dynamic attributes, or jobs?

Interpolations and dynamic attributes run inside the engine, in a restricted environment.
Two additions are fine; a fifteen-line procedure with indirections belongs in a job
(a small extra rule producing an intermediate file) — even at the cost of a rerun.
Readability rule of thumb: define tiny helpers as plain Lmakefile globals
(`def read(f): return open(f).read().strip()`) and call them in the interpolation, rather than nesting `open()` three deep.

### `os.environ` is empty in my job

On purpose — that IS the no-env-dependence guarantee, both at Lmakefile evaluation and in jobs.
Deliberate imports go through `lmake.user_environ`,
routed into `environ` / `environ_resources` / `environ_ancillary` so their tracking status is explicit.

### How do I quote things in cmds?

Use `r'''...'''` triple-quoted commands for multi-line or regex-heavy scripts.
To pass a computed value to the shell, prefer `environ = {'CXX': ...}` + `$CXX` in the script over fighting `{{}}` escapes.
For escaping, use `shlex.quote` for the shell and `re.escape` for computed strings landing in a target/dep/stem regex — never a homemade function.

## Aggregating many files

### How do I link/archive/report over an unknown-size set of files?

The list-file idiom, canonical form:

1. A rule produces the **list file** (grep of `LMAKE/manifest` or `git ls-files`, transformed to output names).
2. The consumer declares the list as a **static dep with the `critical` flag**.
3. The consumer's cmd starts with `ldepend --read $(cat {LST})` then `lcheck_deps`, then runs the real command.

Why each piece: tools like `ar` or linkers stop at the first missing input and would never report the full dep set themselves;
`critical` prevents speculatively rebuilding items dropped from the list;
`lcheck_deps` reruns *before* the expensive command if some list item is stale.
Bonus: the same list file serves several consumers (`.a` and `.so`),
and the consumer does not rerun when sources change without changing the list.

Never approximate the list with a glob: a glob picks up strays, misses generated files, and is untracked.

## Statuses, reruns, debugging

### How do I read the status lines?

`done` is authoritative — it will not be retracted.
`may_rerun` is the speculative status that can resolve into `was_dep_error`.
In interactive (colored) output, a speculative failure shows orange, not red; a red `was_failed` line is what confirms it.
Chronic `rerun` on a rule = deps discovered too late in the job —
restructure with the list-file idiom / `critical` / `check_deps`.

### What are the debugging tools?

`lshow -i <file>`: why it was built (forward reason) and which dep triggered it (backward).
`lshow -dv`: deps *including non-existent ones* (often the explanation).
`lshow -D <file>`: reverse deps — who depends on it.
`ldebug -kE <target>`: a shell inside the exact execution environment of the job (`-ke` for a more comfortable, slightly less faithful one).
These show autodep's ground truth — trust them over your reading of the cmd.

## Sources

### Can a rule produce a file that is also committed?

No — sources are a contract.
Declaring something a source is a promise, and open-lmake sanity-checks it.
git owns sources; open-lmake derives files from them.
Never write a rule that *creates* a source (e.g. compressing `x` into a committed `x.gz`): a file comes from git or from a rule, not both.

### But my source could be mechanically refreshed (generated tables, pinned lists)

The regenerable-source idiom: a rule produces `s.computed`;
if it differs from `s`, the job prints (with `stderr_ok`) the exact message `cp s.computed s` and the human runs it.
The flow stays clean — sources on one side, derived files on the other, and every change to a source is a deliberate act.

### Can a job read files under `LMAKE/`?

Never read the volatile ones (`LMAKE/last_output`, `LMAKE/outputs/...`):
their content depends on the previous invocation — irreproducible by construction.
`LMAKE/manifest` and the stable metadata files are fine (they are sources).

## `incremental` and `ignore`, precisely

### What exactly do I promise with `incremental`?

Without `incremental`, open-lmake guarantees your job runs as if after `git clean` — that is what it fights for.
With it, YOU accept any prior content/existence and must produce a correct result from it.
Sharp edge: **any access** (even a pure read) to a file matching an incremental target pattern makes your job its producer —
reading another rule's output through an incremental pattern "steals" ownership, and the official rule's next run will unlink and regenerate.
Only declare `incremental` on patterns your rule genuinely owns.

### Is an `ignore` target still a target?

No.
It is never erased, never checksummed, never tracked;
`side_deps`/`side_targets` do not create deps or targets at all — they attach flags to whatever file accesses match their patterns.

## NFS and locking (last-resort territory)

### I need a lock around a stateful tool; what works on NFS?

Restructure first (see [stateful tools](https://cesar-douady.github.io/open-lmake/stateful_tools.html)).
If a lock is truly needed: on NFS, `flock` and `mkdir`-based locks are **not reliable**;
`open(O_CREAT|O_EXCL)`, `link` and `symlink` based locks work.
Release with `trap release_lock 0` (fires on any exit, including under `set -e`) rather than a command suffix.
Clean stale state at the **start** of the next run, not at the end: crash-proof, and leftover state stays inspectable for post-mortem.

## CLI

### Does `-j 16` make my build faster?

No.
`-j N` is a *cap* on simultaneous jobs per backend, never a boost —
parallelism is already maximal, driven by `resources` and backend configuration.
Adding `-j16` to go faster is a make reflex that does nothing good here.

### `lmake foo` succeeded but other files are stale

`lmake foo` guarantees only that `foo` is up to date; `lmake -a` forces the whole dep tree up to date.

### Why is there no shell completion of buildable targets?

Targets are regexes; the buildable set is infinite by construction.
