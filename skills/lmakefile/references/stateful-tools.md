# Stateful tools

How to integrate conan, pip/venv, cargo, sbt, maven and similar tools into an open-lmake flow.

open-lmake's model: a job's output is a pure function of the file contents it reads.
Package managers and some build tools violate this by design: they keep internal databases (often sqlite),
out-of-repo homes (`~/.conan2`, `~/.cargo`, `~/.ivy2`),
and decide what exists by consulting that state instead of the filesystem.
Autodep can trace file reads and writes, but it cannot assign meaning to "row updated inside a shared sqlite file" —
so shared mutable homes are fundamentally at odds with tracking, caching and multi-user soundness.

## First, answer: who guarantees what?

Before picking a strategy, decide who owns each piece of tool state — the answer dictates everything:

- **The tool owns its state**: you trust it unconditionally (its cache/DB is correct whatever happens around it, including concurrent use).
  Then `ignore` its whole state area and accept the consequences in full (level 3 below):
  open-lmake cannot help you inside that zone, and the shared cache must be off for every rule that reads it.
- **open-lmake owns the state**: then every file in the zone is a job output like any other —
  it must be possible to `lmake` any file there unambiguously, with exactly one official producer per file.

The unsound middle ground — half-declared state, rules stepping on each other's targets — is the only truly bad choice.
A layered scheme (e.g. an install rule at higher prio and a build rule below it, both covering the same tree with star targets)
is legitimate as long as no rule ever produces a file whose official producer is another rule.

There is a ladder of integration strategies, from best to last-resort.
Always start at the top.

## Level 0 — do not run the tool at build time

Vendor the dependency (submodule, subrepo) or fetch it with a plain rule:
one artifact = one rule = one target, pinned by version + checksum verified in `cmd`.
The package manager runs outside the build (to compute a lockfile, which is then a source);
the build itself only consumes pinned artifacts.
This is the only level with full cache soundness and zero caveats.
Prefer it whenever the dependency count is manageable.

## Level 1 — confine the tool's whole state inside the repo, one rule owns it

Give the tool a home that is a target tree of exactly ONE rule.
The [conan unit test](https://cesar-douady.github.io/open-lmake/unit_tests/conan.html) is instructive precisely because it splits the tree across two rules
and its driver asserts the resulting failure (`done=1, failed=1, rc=1`):

```python
class Pip(Rule) :                          # succeeds: owns the whole tree
    targets    = { 'PIP' : 'venv/bin/pip' , 'VENV' : r'venv/{*:.*}' }
    readdir_ok = True                      # pip insists on scanning dirs
    cmd        = 'python3 -m venv venv'

class Conan(Rule) :                        # FAILS, and the test asserts it:
    targets    = { 'CONAN' : 'venv/bin/conan' }   # pip install writes throughout
    deps       = { 'PIP'   : 'venv/bin/pip'   }   # venv/ (lib, bin, ...) — all
    readdir_ok = True                             # undeclared here, and the tree
    cmd        = 'venv/bin/python3 -m pip install conan'  # already belongs to VENV above
```

The working shape is one rule per tool venv, owning the whole tree — creation and install in the same job:

```python
class ConanVenv(Rule) :
    targets    = { 'CONAN' : 'conan-venv/bin/conan'
               ,   'VENV'  : r'conan-venv/{*:.*}'   }
    environ    = { 'PIP_NO_CACHE_DIR' : '1' }
    readdir_ok = True
    cmd        = '''
        python3 -m venv conan-venv
        conan-venv/bin/pip install conan==<version>   # pin it (hardcoded, or read from a dep file)
    '''
```

Key ingredients:

- the entire tree is a star target (`.../{*:.*}`) of exactly one job — no other job writes there, so `readdir_ok` on it is defensible;
- the tool's own cache is disabled (`PIP_NO_CACHE_DIR=1` — pip actually disables its cache for any value of this variable)
  or pointed into the tree or `$TMPDIR`, so no state escapes;
- downstream rules depend on concrete files in the tree (`.../bin/conan`).

Cost: installing per-repo (not per-machine).
That is the price of correctness, and the shared cache gives most of it back (the install job itself becomes a cache hit for every user).

## Level 2 — declared incremental scratch (the cargo pattern)

For tools with a valuable warm cache (incremental compilers), declare the tool's scratch tree as an `incremental` side/star target:
it is not wiped between runs, the tool reuses it, and open-lmake still records everything.
From the [cargo unit test](https://cesar-douady.github.io/open-lmake/unit_tests/cargo.html):

```python
class CompileRust(HomelessRule,RustRule) :                   # excerpt; test-harness lines omitted
    targets      = { 'EXE'        :   r'{Dir:.+/|}{Module:[^/]+}/target/debug/{Module}'  }
    side_targets = { 'SCRATCHPAD' : ( r'{Dir}{Module}/{*:.*}' , 'Incremental' )          }
    deps         = { 'PKG' : '{Dir}{Module}/Cargo.toml' , 'SRC' : '{Dir}{Module}/src/main.rs' }
    stderr_ok    = True
    cmd          = 'cd {Dir}{Module} ; cargo build'
```

Constraints for this to be sound:

- one job owns the scratch tree (patterns must not overlap between jobs);
- `incremental` is a promise: from ANY prior scratch state, the tool must produce a correct result
  (true for well-behaved incremental compilers, false for tools whose DB can go stale or corrupt — judge per tool);
- the home and env of the tool must still be confined (e.g. `RUSTUP_HOME` pointed at a controlled location, `HomelessRule` to shield `$HOME`).

## Level 3 — shared home + `ignore` (last resort, document the debt)

Pointing the tool at a shared home and flagging it `('home/{*:.*}', 'ignore')` as a side_dep/side_target makes the errors disappear — and with them, correctness:

- jobs read state open-lmake does not record → identical tracked inputs can yield different outputs;
- the multi-user / shared cache becomes unsound for every rule touching the home
  (a hit computed by user A may be wrong for user B) — disable `cache` on these rules;
- concurrent jobs race on the tool's internal DB → you must add manual locking
  (flock around every invocation; sqlite "database is locked" retries are the smell of this)
  because open-lmake cannot serialize accesses it does not see;
- rebuild-from-clean is no longer guaranteed to reproduce.

If forced here (tool has no way to confine state, dependency set too large for level 0/1):
keep the ignored zone as small as possible, wrap every tool invocation in a lock, disable the shared cache for affected rules,
and leave a comment marking this as a known unsoundness with the intended exit
(usually level 0: pre-materialize packages from a lockfile into tracked paths).

## Per-tool quick notes

- **pip/venv**: level 1 works well (venv-as-star-target).
  Disable the pip cache (`PIP_NO_CACHE_DIR=1`, or point `PIP_CACHE_DIR` into `$TMPDIR`),
  unset `PYTHONPATH`/`VIRTUAL_ENV` inherited assumptions, pin the interpreter.
  A venv cannot inherit another venv's packages (interpreters chain via symlinks, site-packages do not) — one venv per tool, each installed by one rule.
  Spell canonical names in deps: a venv may create a `lib64 -> lib` symlink, and names spelled through it are non-buildable by the up-hill rule
  (see the [FAQ](https://cesar-douady.github.io/open-lmake/faq.html)).
- **conan (v2)**: internally sqlite-backed; a shared `CONAN_HOME` is level 3 with all its debt.
  Prefer level 0: resolve outside the build to a lockfile, then one rule per package materializing from the lock into tracked paths;
  or level 1 with a per-repo home built by a single rule chain (profiles → install → build), accepting the serialization through that chain.
- **cargo**: level 2 is proven (see above). Confine `RUSTUP_HOME`/`CARGO_HOME`.
- **sbt/ivy/coursier, maven**: default homes under `~`; run under `HomelessRule` and confine caches into the repo or `$TMPDIR`.
  Resolution is network-bound → push it to level 0 (fetch rules per artifact, checksummed) and keep compilation as plain rules.
- **Any tool with a daemon** (gradle, sbt server, bazel): disable the daemon;
  a background process outliving the job is untracked state by definition (`kill_daemons` exists for the ill-behaved ones).

## Rule of thumb

If the tool decides what to do by consulting anything other than the files the job reads,
either feed it a fresh, controlled state (levels 0-1), own its scratch explicitly (level 2), or accept and fence the unsoundness (level 3).
Never level 3 by default, and never level 3 combined with a shared cache.
