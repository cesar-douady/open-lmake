<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Rule selection

When open-lmake needs to ensure that a file is up to date, the first action is to identify which rule, if any, must be used to generate it.
This rule selection process works in several steps described below.

A file is deemed buildable if the rule selection process leads to a job that generates the file.

### Name length

First, the length of the target name is checked agains `lmake.config.path_max`.
If the target name is longer, then the process stops here and the file is not buildable.

### Sources

The second step is to check target agains sources and source dirs.

If the target is listed as a source it is deemed buildable.
No execution is associated, though, the file modifications made by the user are tracked instead.

If the target is within a dir listed as a source dir (i.e. appears ending with a `/` in the manifest), it is deemed buildable if it exists.
If it does not exist, it is not buildable.
In both cases, the process stops here.

### Up-hill dir

The third step is to see if a up-hill dir (i.e. one of the dir along the dir path leading to the file) is (recursively) buildable.

If it is the case, the rule selection process stops here and the file is not buildable.

### `AntiRule` and `SourceRule`

The following step is to match the target against `AntiRule`'s and `SourceRule`'s (ordered by their `prio` attribute, high values are considered first).
If one is found, the target is buildable if it matches a `SourceRule` and is not if it matches an `AntiRule`.

If it matches a `SourceRule` and it does not exist, it is still buildable, but has an error condition.

In all cases, as soon as such a match is found, the process stops here.

### Plain rules

The rules are split into groups. Each group contains all of the rules that share a given `prio`.
Groups are ordered with higher `prio` first.

The following steps is executed for each group in order, until a rule is found. If none is found, the file declared not buildable.

#### Match a target

For a given rule, the file is matched against each target in turn.
Static targets are tried first in user order, then star targets in user order, and matching stops at the first match.
Target order is made of `targets` and `target` entries in reversed MRO order (i.e. higher classes in the python class hierarchy are considered first),

If a target matches, the matching defines the value of the static stems (i.e. the stems that appear without a `*`).
Else, the rule does not apply.

#### Check static deps

The definition of the static stems allow to compute :

- The other targets of the rule. Static targets become the associated file, star targets becomes regular expressions in which static stems are expanded.
- Static deps by interpreting them as f-strings in which static stems and targets are defined.

Static deps are then analyzed to see if the are (recursively) buildable, and if any is not buildable, the rule does not apply.

#### Group recap

After these 2 previous steps have been done for the rules of a group, the applicable rules are analyzed the following way:

- If no rule apply, next group is analyzed.
- If the file matches several rules as a sure target (i.e. a static target and all static deps are sure),
  the file is deemed buildable, but if required to run, no job will be executed and the file will be in error.
- If the file matches some rules as a non-sure target (i.e. a star target or a dep is not sure), the corresponding jobs are run.
  If no such jobs generate the file, next group is analyzed.
  If several of them generate the file, the file is buildable and in error.
