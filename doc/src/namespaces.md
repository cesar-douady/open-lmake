<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Namespaces

Namespaces are used to isolate jobs.
This is used to provide the semantic for the `chroot_dir`, `repo_view`, `tmp_view` and `views` attributes.

In that case, pid's are also isolated which allows for a reliable job end: when the top-level process exits, the namespaces are destroyed and no residual process can survive.
This guarantees that no daemon is left behind, uncontrolled.

Note that this is true even when `chroot_dir` is `'/'`, which otherwise provides no other effect by itself.

Namespaces can be used in the following situations :

- Open-lmake provides a cache mechanism allowing to prevent executing a job which was already executed in the same or another repo.
  However, some jobs may use and record absolute paths.
  In that case, the cache will be inefficient as the result in a repo is not identical to the one in another repo.
  This is current practice, in particular in the EDA tools community (which may be rather heavy and where caching is mostly desirable).
  Using the `repo_view` attribute is an effective way to circumvent this obstacle.
- Open-lmake tracks all deps inside the reposity and listed source dirs. But it does not track external deps, typically the system (e.g.the `/usr` dir).
  However, the `chroot_dir` attribute is part of the command definition and a job will be considered out of date if its value is modified.
  Hence, this can be used as a marker representing the whole system to ensure jobs are rerun upon system updates.
- some software packagess (e.g. EDA tools) are designed to operate on a dir rather than dealing with distinct input and output files/dirs.
  This goes against reentrancy and thus reliability, repeatability, parallelism etc.
  This problem can be solved with symbolic links if they are allowed.
  In all cases, it can be solved by using the `tmp_view` and copying data back and forth between the repo and the tmp dir.
  Or, more efficient, it can be solved by adequately mapping a logical steady file or dir to a per job physical file or dir (respectively).


When entering a `chroot_dir` with [chroot(2)](https://man7.org/linux/man-pages/man2/chroot.2.html), the user and group databases become those specified in the chroot dir.
It may not contain entries for the current user, or the network service may not be available in this environment.
In that case, some actions can be carried out by open-lmake to restore names for the user and its group using the `chroot_action` rule attribute.
Supported actions are:

- `overwrite`: user name and its associated group name are transported from the native namespace, while losing entries for other users and groups.
This is light performance wise and can be used without fear for performance.
