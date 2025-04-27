<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Meta data

The `LMAKE` dir at the root of the repo contains numerous information that may be handy for the user.

It also contains a `lmake` dir containing private data for open-lmake's own usage.

### `LMAKE/config_deps`, `LMAKE/rules_deps` and `LMAKE/sources_deps`

These files contain a list of files that open-lmake has read to process `Lmakefile.py` when reading each section (config, rules and sources).

They contain several types of lines, depending on the first char:

- `#`: comment line
- `*`: line contains the open-lmake installation dir
- `+`: line contains an existing file that was read
- `!`: line contains a non-existing file that was accessed

These contents are then used to determine if each section must be refreshed when a new `lmake` command is run.

### `LMAKE/config`

This file contains a description of the `lmake.config` `dict` as it has been understood by open-lmake after having processed `Lmakefile.py`.

### `LMAKE/manifest`

This file contains a description of the sources as it has been understood by open-lmake after having processed `Lmakefile.py`.

### `LMAKE/rules`

This file contains a description of the rules as they have been understood by open-lmake after having processed `Lmakefile.py`.

### `LMAKE/outputs/<date>/<time>`

This file contains a transcript of the `lmake` command that has been run at `<time>` on `<day>`.
Such logs are kept for a number of days given in `lmake.config.console.history_days`.

### `LMAKE/last_output`

This file is a symbolic link to the last transcript.

### `LMAKE/targets`

This file contains the targets that have been required by `lmake` commands in chronological order (with duplicates removed).

### `LMAKE/version`

This file contains a state-recording version of open-lmake.
If the recorded version does not match the used version, none of the open-lmake commands can be used.

### `LMAKE/debug`

This dir contains a sub-dir for each job `ldebug` was used for.
These sub-dirs are named after the job id as displayed by `lshow -i`.

### `LMAKE/tmp`

This dir contains a sub-dir for each job which was run while keeping its tmp dir.
These sub-dirs are named after the job id as displayed by `lshow -i`.

### `LMAKE/quarantine`

This dir contains all files that have been quarantined.
A file is quantantined when open-lmake decides it must be unlinked and it contains manual modifications, i.e. modifications made outside the control of open-lmake.
In that case, in order to be sure that no user work is lost, the file is quarantined in this dir rather than unlinked.

