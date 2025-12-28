<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# ETA

An ETA estimation is made possible because the execution time for each job is recorded in open-lmake book-keeping after all successful runs
(if a job ends in error, it may very well have been much faster and the previous execution time is probably a better estimate than this one).
When a job has never run successfully, an ETE is used instead of its actual execution time by taking a moving average of all the jobs of the same rule.

That said, a precise ETA would require a fake execution of the jobs yet to be run which can take all deps and resources into account.
However this is way too computationally expensive, so a faster process must be done, even at the expense of precision.

In all cases, the ETA assumes that no new hidden deps are discovered and that no file is steady so that all jobs currently remaining will actually be executed.

2 approaches can be considered to estimate the time necessary to carry out remaining jobs :

- Resources limited : deps are ignored, only resources are considered.
  Roughly, the time is the division of the quantity of resources necessary by the quantity of resources available.
  For example, if you need 10 minutes of processing and you have 2 cpus, this will last 10/2=5 minutes.
- Deps limited : resources are ignored and only deps are considered. This means you only look at the critical path.
  For example if you need to run a 2 minutes job followed by a 3 minutes job, and in parallel you must run a 4 minutes job, this will last 2+3=5 minutes.

Open-lmake uses the first approach.
For that it measures the parallelism of each job while running and the ETA is computed after the sum of the costs of all waiting and running jobs,
the cost being the execution time divided by the observed parallelism.
Jobs running for the first time inherit a moving average of the last 100 run jobs of the same rule.
