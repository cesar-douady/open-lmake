# open-lmake rule cheatsheet

Distilled from the open-lmake doc chapters `rules`, `lmake_rules_module` and
`lmake_module` — for full semantics see
https://cesar-douady.github.io/open-lmake/rules.html (and siblings); if a source
checkout is available, the same chapters live in `doc/src/`, and `lib/lmake/rules.py`
is the authoritative definition of the base classes below — short, exhaustive and
commented for exactly this purpose (also installed with open-lmake, importable as
`lmake.rules`). Attribute inheritance follows the
Python MRO with a "combine" mechanism: dict attributes merge, `...` (Ellipsis) in a
list/dict splices the inherited value (e.g. extend `$PATH`-like lists).

## Base classes (`lmake.rules`)

| Class | Use |
|---|---|
| `Rule` | Normal rule. Needs `target`(s) + `cmd`. |
| `AntiRule` | Declares patterns that are NOT buildable (carve-outs). No `cmd`. |
| `SourceRule` | Declares patterns as sources (beyond the manifest). |
| `PyRule` / `Py2Rule` | Python `cmd()`; handles import machinery (incl. `readdir_ok` on `sys.path` dirs). `PyRule` is python3; the prose doc mentions a `Py3Rule` alias but the code defines only these two. |
| `AliasRule` | Phony alias targets (`lmake ALL`). |
| `HomelessRule` | Sets `$HOME` to `$TMPDIR` — shields against user dotfiles. |
| `RustRule` | Forces `autodep='ld_preload'` (rust uses a dedicated loader that bypasses ld_audit's hooks). |
| `TraceRule` | Traces executed shell lines to stdout. |
| `ConfigRule` | Expands a user-provided python config file (e.g. `config.py`) into an on-disk hierarchy that jobs read piecemeal via `lmake.config_dict('config')` — unrelated to `lmake.config` (the global build config). |
| `DirtyRule` | Absorbs all undeclared writes via a catch-all star side_target (`Incremental`+`NoWarning`). Debug only — never in a finalized flow. |

## Main rule attributes

| Attribute | Meaning / notes |
|---|---|
| `target` / `targets` | Singular: `cmd` stdout is redirected to it. Plural: dict `{NAME: 'pattern'}` or `{NAME: ('pattern', 'flag', ...)}`. Patterns are f-string-like regexes with stems, expanded by lmake at match time — no `f` prefix, unless deliberately two-stage templating (Python expands first at import; escape lmake's braces as `{{}}`). |
| `dep` / `deps` | Singular: connected to `cmd` stdin. Plural: dict of static deps (condition rule selection: rule matches only if static deps are buildable). Same flag syntax as targets. |
| `stems` | Dict of named regexes shared by targets/deps. Required home for regexes with unbalanced `{}`. Regexes are DOTALL. |
| `side_targets` / `side_deps` | Patterns that may be written/read without being real targets/deps — where flags like `ignore`, `incremental`, `readdir_ok` on trees are declared. |
| `cmd` | Shell string (f-string semantics, expanded at job time) or python function (context injected as globals; star targets are functions of the star stem). Along MRO, python `cmd`s chain; shell `cmd`s concatenate. |
| `name` | Unique rule identifier (default: class name). MUST be set explicitly for loop-generated rules. |
| `prio` | Higher prio matches first (rule-selection tie-break within a group). |
| `job_name` | Display/coherence name for jobs (rarely needed). |
| `backend` | `'local'` (default) / `'sge'` / `'slurm'`. Missing daemon → transparent local fallback. |
| `resources` | Dict (dynamic-capable): `cpu`, `mem` (MB), `tmp` (MB) understood by all backends; extra keys (e.g. license tokens) are scheduler resources. |
| `cache` | Name of a cache declared in `lmake.config.caches.<name>` — opt-in per rule. |
| `compression` | Cache entry compression, e.g. `('zlib', lvl)`. |
| `check_abs_paths` | Error if the repo's absolute path leaks into a target — keep True-able for cache portability (or use `repo_view`). |
| `environ` / `environ_resources` / `environ_ancillary` | Job env. Command-like (rerun on change) / resource-like (rerun only failed) / untracked. User env is otherwise ignored. |
| `autodep` | Spy method: `'ld_audit'` (default) / `'ld_preload'` / `'ptrace'` / `'seccomp'` (kernel-dependent) / `'none'`. `lmake.autodeps` lists what's available. Change only when the default demonstrably fails (static binaries, rust, wine...). |
| `readdir_ok` | Allow reading directories inside the repo. Deliberate opt-out of tracking — see good-practices. |
| `auto_mkdir` | Auto-create dirs on chdir-to-nonexistent (legacy flows). |
| `stderr_ok` | Non-empty stderr is not an error. |
| `max_stderr_len` | Truncate reported stderr. |
| `timeout` | Kill job after N seconds. |
| `kill_sigs` | Signal escalation list used to kill jobs (SIGKILL appended). |
| `kill_daemons` | Also kill daemons spawned by the job. |
| `start_delay` | Only report job start after N s (quiet fast jobs). |
| `max_submits` / `max_retries_on_lost` / `retried_errors` | Retry knobs; the default `max_submits` already bounds rerun loops. |
| `force` | Always rerun. Debug/bench only — never a workflow mechanism. |
| `keep_tmp` | Keep `$TMPDIR` after run for inspection (debug). |
| `python` / `shell` | Interpreter executables (+ options). |
| `use_script` | Pass `cmd` via a script file instead of `-c` (very long cmds). |
| `views` / `tmp_view` / `repo_view` / `lmake_view` / `chroot_dir` | Namespace/mount remapping (advanced; reproducible views of repo/tmp). |
| `virtual` | Class is a base only, not a rule (auto-detected if no target). |

Most attributes accept a **function** instead of a value (dynamic attributes),
evaluated per-job with stems in scope. Constraints: they run inside the lmake server —
fast, pure, no chdir/fork/exec/env mutation; file reads become deps of the job.

## Dep flags (in `deps` values, `side_deps`, `lmake.depend`, `ldepend`)

| Flag | Effect |
|---|---|
| `critical` | On change, stop speculative rebuild of subsequent deps; rebuild the new dep list first. For deps that are lists of deps (test manifests). |
| `essential` | Cosmetic (dataflow display). Default on. |
| `ignore` | Reads of this pattern are not recorded. Escape hatch — see good-practices. |
| `ignore_error` | Dep may be in error; run anyway (colored test reports). |
| `readdir_ok` | This dep may be read as a directory. |
| `top` | Pattern rooted at top-level repo (subrepos). |
| `required` (API) | Dep must be buildable (default for static deps). |

Prefix a flag with `-` to negate it. Flags nest in sub-tuples.

## Target flags (in `targets` values, `side_targets`, `lmake.target`, `ltarget`)

| Flag | Effect |
|---|---|
| `essential` | Cosmetic. Default on. |
| `incremental` | Not unlinked before run; previous content may be reused. YOU guarantee correctness from any prior state. The sound way to keep a tool's internal scratch/cache tree. |
| `optional` | If not generated, target deemed not produced (another rule may apply). |
| `phony` | Deemed generated even if absent on disk (`ALL`, aliases). |
| `source_ok` | No error if the target is actually a source. |
| `no_warning` | Silence uniquify/unlink warnings. |
| `ignore` | Reads AND writes ignored. Last resort. |
| `top` | Rooted at top-level repo. |

## `lmake` module — inside `Lmakefile.py`

- `lmake.config` — global config: `lmake.config.backends.<be>` (local resource pools,
  sge/slurm daemon params, `n_max_queued_jobs`), `lmake.config.caches.<name>`
  (`dir`, `repo_key`...), `lmake.config.sub_repos`, `path_max`, `max_dep_depth`,
  `link_support`...
- `lmake.manifest = [...]` / `lmake.sources.{auto_sources,git_sources,manifest_sources}` —
  source declaration. Default: `Manifest` file, else `git ls-files` (submodules
  included). Dirs with trailing `/` = whole subtree (may be outside the repo).
- `lmake.version` / `check_version(major, minor)` — pin compatibility.
- `lmake.repo_root`, `lmake.top_repo_root`, `lmake.autodeps`, `lmake.backends`.
- `lmake.user_environ` — launching user's env snapshot. Discouraged; if used, route
  through `environ*` dicts so it is tracked.

## `lmake` module — inside a python `cmd()` (CLI twin in parentheses)

- `depend(*files, critical=, ignore=, ignore_error=, readdir_ok=, follow_symlinks=, read=, required=, regexpr=, verbose=)` (`ldepend`) — add/flag deps.
- `target(*files, write=, incremental=, ignore=, source_ok=, no_warning=, allow=, regexpr=)` (`ltarget`) — declare/flag targets dynamically.
- `check_deps(delay=0, sync=False)` (`lcheck_deps`) — verify all deps so far are
  up-to-date; kills+reruns the job if not (fail fast in long jobs, after reading a
  critical dep's content).
- `run_cc(*cmd_line, marker=..., stdin=None)` — run a compiler, capture `-MMD`-style
  dep files precisely.
- `encode(table, ctx, val, min_length)` / `decode(table, ctx, code)` (`lencode`/`ldecode`)
  — bijective value↔short-code tables (stable names for long param sets; the table
  file is versioned with the repo).
- `list_deps()` / `list_targets()` / `cp_target_tree` / `mv_target_tree` / `rm_target_tree`
  — introspect/manage the job's accesses.
- `xxhsum(text)` / `xxhsum_file(file)` — the checksums lmake itself uses.
- `get_autodep()` / `set_autodep()` / `class Autodep` — locally toggle spying (rare).

## Rule selection (why "my rule doesn't match")

For a required file, global pre-checks come first: name length, is-it-a-source,
uphill dir buildability (if dir `a` is buildable, `a/b` is not), and
`AntiRule`/`SourceRule` matching. Only then are rules tried by `prio` group: within
a group, a rule matches if its target pattern matches with consistent stems and all
its static deps are buildable. `lshow -i <file>` explains the actual selection.

## CLI survival kit

`lmake <targets>` (build) · `lshow -i|-d|-t|-E|-e <target>` (why/what) ·
`ldebug <job>` (rerun a job in a debug env) · `lforget` (forget a job's history) ·
`lmake_repair` (repair a broken repo state) · `lmark` (freeze files) ·
`ldepend`/`ltarget`/`lcheck_deps`/`lencode`/`ldecode` (in-job).
