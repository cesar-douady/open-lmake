<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2026 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Meta data

The `LMAKE` dir at the root of the repo contains numerous information that may be handy for the user.

It also contains a `lmake` dir containing private data for open-lmake's own usage.

`LMAKE/environ` and `LMAKE/manifest` can be freely used in jobs and are considered as sources if they are listed in `lmake.manifest`, which is automatic by default.

### `LMAKE/config_deps`, `LMAKE/rules_deps` and `LMAKE/sources_deps`

These files contain a list of files that open-lmake has read to process `Lmakefile.py` when reading each section (config, rules and sources).

They contain several types of lines, depending on the first char:

- `#`: comment line
- `*`: line contains the open-lmake installation dir
- `+`: line contains an existing file that was read
- `!`: line contains a non-existing file that was accessed

These contents are then used to determine if each section must be refreshed when a new `lmake` command is run.

### `LMAKE/auto_tmp`

This dir contains the default tmp dirs of jobs.

### `LMAKE/config`

This file contains a description of the `lmake.config` `dict` as it has been understood by open-lmake after having processed `Lmakefile.py`.

### `LMAKE/debug`

This dir contains a sub-dir for each job `ldebug` was used for.
These sub-dirs are named after the job id as displayed by `lshow -i`.

### `LMAKE/environ`

This file contains the list of environment variables actually used in `Lmakefile.py` in the form of lines containing `<key>=<value>`.

### `LMAKE/last_output`

This file is a symbolic link to the last transcript.

### `LMAKE/lmakefile_tmp`

This dir contains the tmp dirs used when reading `Lmakefile.py`.
When several passes are used, each pass has its own tmp dir.
These are kept after reading for debugging purpose.

### `LMAKE/manifest`

This file contains a description of the sources as it has been understood by open-lmake after having processed `Lmakefile.py`.

### `LMAKE/matching`

This file contains a description of the matching performed by open-lmake when looking for rules to generate a file.
It is composed of matching entries.

Each matching entry starts with a header providing a prefix and a suffix.
Files matching the prefix and suffix are matched against the rules listed underneath.

The prefix and suffix are given in the form `<prefix>*<suffix>' to mimic shell patterns.
However, in this case, the `*` may match a negative number of characters, i.e. the prefix and the suffix may overlap.

The listed rules are provided as:

- Its priority: if a rule at given priority match and generate the file, rules of lower priority are not tried.
- Its name.
- The target corresponding to this prefix/suffix pair.

### `LMAKE/outputs/<date>/<time>`

This file contains a transcript of the `lmake` command that has been run at `<time>` on `<day>`.
Such logs are kept for a number of days given in `lmake.config.console.history_days`.

### `LMAKE/quarantine`

This dir contains all files that have been quarantined.
A file is quanrantined when open-lmake decides it must be unlinked and it contains manual modifications, i.e. modifications made outside the control of open-lmake.
In that case, in order to be sure that no user work is lost, the file is quarantined in this dir rather than unlinked.

### `LMAKE/rules`

This file contains a description of the rules as they have been understood by open-lmake after having processed `Lmakefile.py`.

### `LMAKE/targets`

This file contains the targets that have been required by `lmake` commands in chronological order (with duplicates removed).

### `LMAKE/tmp`

This dir contains a sub-dir for each job which was run while keeping its tmp dir.
These sub-dirs are named after the job id as displayed by `lshow -i`.

### `LMAKE/version`

This file contains a state-recording version of open-lmake.
If the recorded version does not match the used version, none of the open-lmake commands can be used.
