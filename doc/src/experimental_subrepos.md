<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Sub-repositories

Sub-repositories are repositories that contain repositories, i.e. some `Lmakefile.py` are present in sub-directories.

In that situation, it is reasonable to assume that the `Lmakefile.py` are made to handle building files underneath it.

To support this situation, open-lmake allow you to simply mention such sub-repos, so that:

- Targets only match within the sub-repo (and escape is possibly by setting the `top` flag to the target to provide global rules).
- The same applies to deps.
- `cmd` is run from this sub-repository, i.e. its cwd is set accordingly.
- The priority of deeper rules are matched first, so that builds in a sub-repo is not pertubated by rules of englobing repo.
