<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->
<!-- Why open-lmake-->

# Who needs open-lmake?

If you experience any of the following time-wasting situations:

- Your computer executes some stuff you are sure or almost sure (say 99%) is useless.
- You use a single computer although you have access to a compute farm.
- You have not parallelized what can be done because it would take longer to write a sound parallelizing script.
- You have written scripts while you felt that a sound organization of your workspace would have avoided such a need (e.g. preparing a clean tmp directory and cleaning it at the end).
- You have written ad-hoc scripts to handle rather generic situations (e.g. detecting a file changed to save work in case it did not).
- You navigate through your fancy workspace and each step is like a criminal investigation because you don't have the necessary forward & backward pointers (w.r.t your flow).
- What works in your colleague & friend's repository does not work in yours or vice versa.
- What worked last week is broken today.
- You forgot how to use this script you wrote last week because since then, your mind was overloaded with 1000 other stuff.
- You need to use this complex tool (e.g. a CAD tool) about which only the specialist has the know-how, and unluckily, he is on vacation this week.
- You have to wait while your repository is busy because your flow is running (e.g. compilation is on-going, unit tests are running, ...)
  and you know if you edit source files at the same time, nothing good will happen.

Then you need a tool to help you.
Such tools are called build systems.

Open-lmake is such a tool that will provide apt solutions to all the above-mentioned points.

If your development is mainstream (such as writing an app in C, Python or Java) and you are seeking a fast on-boarding tool, you may find some other tools adequate.
These may be for example `Cmake`, `PyBuilder` or `maven`. They will save you from writing common case rules, call the compiler with the right options etc.

In other cases (e.g. you use CAD tools, you write embedded code, the complexity of your flow comes from testing & evaluating KPI's rather than compiling, etc.),
you need a more generic tool and open-lmake is an excellent choice.

In particular, if you already use `make` today and are frustrated by its poor static dependency paradigm and lack of versatility, and if you are tired typing `make clean` every so often,
then converting your code base to open-lmake will be 1) easy and 2) a breath of fresh air (in a large par due to its [automatic dependency mechanism](autodep.md).
