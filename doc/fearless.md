<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->
<!-- Why open-lmake-->

# Fearlessness

Fearlessness is essential for an engineer.
It means trusting a tool without having to understand every detail of its inner workings.

### A few examples

- **`diff` is fearless**  
  Its detailed algorithm is fairly complex but one generally does not have to worry about it: it outputs diffs, period.

- **Rust advocates "fearless concurrency"**  
  When programming in C/C++ in a multi-thread context, a cautious engineer knows they have to take care of locks, pay special attention to any shared data, etc. C/C++ is **fearful** in this regard.
  There are even specialized tools to help them deal with that.  
  On the other hand, when programming in Rust, they know the language won't allow them to do anything dangerous or unreliable (unless instructed to do so).
  Rust is **fearless** in this regard.

- **Excel is fearless**  
  When a user changes a cell, Excel automatically updates all dependent cells --- no questions asked.  
  Nevertheless, the underlying algorithm is fairly complex to reach this fearlessness while keeping a decent level of performance, but the user does not care about these details.

The example of Excel is interesting because it pretty much resembles a build system: it manages a DAG and must propagate modifications downstream.

### A thought experiment: What if Excel worked like `make`

- You enter values: A1 = `1`, A2 = `10`, B1 = `=A1+1`. B1 displays `2`.
- You change A1 to `2`. B didn't update because ... you didn't specify that B1 depends on A1.
- You manually specify dependencies: You inform Excel that B1 depends on A1 (through some form attached to B1 or whatever).
- B1 displays `3` as expected.
- You change B1’s formula to `=A1-1`: It still displays `4` because... A1 didn’t change.
- You change B1’s formula to `=A2+1`: It still displays `4` because... A1 (yes, **A1**!) hadn’t been modified since B1's last update.
- You realize your mistake and update B1's dependencies: It still displays `4` because... A2 hadn't been modified either since B1's last update.
- Frustrated and exhausted, you finally hit the 'Recompute All' button...
- More complex cases (e.g., the `INDIRECT()` function) break even further.
- ...

In short, such a tool would be completely unusable without frequent presses of the 'Recompute All' button to be safe.

To some extent, the same is true for most build systems (including all major ones): there are numerous situations where they fail to recompute what they should.

### Fundamental flaws of traditional build systems

These build systems follow this approach:

- Here’s our algorithm (see the documentation).
- Fit your build process into it.

**Fearlessness is left to the user** and is mostly impossible to reach.

### How open-lmake changes the paradigm

Open-lmake flips the approach on its head:

- Here are the guarantees (see the documentation).
- Ensure your build process fits within these guarantees.

It offers one key guarantee: _files have the same content they would after a full rebuild._  
This is the cornerstone of **open-lmake's fearlessness**.
