<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# The `lmake.sources` module

### `manifest_sources(manifest='Manifest')`

This function returns the list of sources found in the `manifest` file, one per line.
Comments are supported as everything following a `#` itself at start of line or preceded by a space.
Leading and trailing white spaces are stripped after comment removal.

### `git_sources( recurse=True , ignore_missing_submodules=False )`

This function lists files under `git` control, recursing to sub-modules if `recurse` is true and ignore missing such sub-modules if `ignore_missing_submodules` is true.

The `git` repository can be an enclosing directory of the open-lmake repository.
In that case, sources are adequately set to track `git` updates.

### `auto_sources(**kwds)`

This function tries to find sources by calling `manifest_sources` and `git_sources` in turn, untill one succeeds.
Arguments are passed as pertinent.

In absence of source declaration, this function is called with no argument to determine the sources.
