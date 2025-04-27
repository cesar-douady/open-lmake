<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Critical deps

The question of critical deps is a performance only question.
Semantically, whether a dep is critical or not has no impact on the content of the files built by open-lmake.

During dep analysis, when a dep (call it `dep1`) has been used and turns out to be out-of-date, open-lmake must choose between 2 strategies regarding the deps that follow:

- One possibility is to anticipate that the modification of `dep1` has no impact on the list of following deps.
  With such an anticipation, open-lmake will keep the following deps, i.e. when ensuring that deps are up-to-date before launching a job, open-lmake will launch all necessary jobs to rebuild
  all deps in parallel, even if the deps have been explicitly declared parallel.
- Another possivility is to anticipate that such a modification of `dep1` will drastically change the list of following deps.
  With such an anticipation, as soone as open-lmake sees a modified dep, it will stop its analysis as the following deps, acquired with an out-of-date content of `dep1` is meaningless.

The first strategy is speculative: launch everything you hear about, and we will see later what is useful.
The second strategy is conservative: build only what is certain to be required.

Generally speaking, a speculative approach is much better, but there are exceptions.

Typical use of critical deps is when you have a report that is built from the results of tests provided by a list of tests (a test suite).

For example, let's say you have:

- 2 tests whose reports are built in `test1.rpt` and `test2.rpt` by some rather heavy means
- a test suite `test_suite.lst` listing these reports
- a rule that builds `test_suite.rpts` by collating reports listed in `test_suite.lst`

In such a situation, the rule building `test_suite.rpts` typically has `test_suite.lst` as a static dep but the actual reports `test1.rpt` and `test2.rpt` are
hidden deps, i.e. automatically discovered when building `test_suite.rpts`.

Suppose now that you make a modification that makes `test2.rpt` very heavy to generate. Knowing that, you change your test suite so list a lighter `test3.rpt` instead.
The succession of jobs would then be the following:

- `test1.rpt` and `test2.rpt` are rebuilt as they are out-of-date after your modification.
- `test_suite.rpts` is rebuilt to collate theses reports.
- Open-lmake then sees that `test3.rpt` is needed instead of `test2.rpt`.
- Hence, `test3.rpt` is (re)built.
- `test_suite.rpts` is finally built from `test1.rpt` and `test3.rpt`.

There are 2 losses of performance here:

- `test2.rpt` is unnecessarily rebuilt.
- `test1.rpt` and `test3.rpt` are rebuilt sequentially.

The problem lies in the fact that `test1.rpt` and `test2.rpt` are rebuilt before open-lmake had a chance to re-analyze the test suite showing that the new tests are test1 and test3.
Generally speaking, this is a good strategy : such modifications of the dep graph happens rather rarely and speculating that it is pretty stable by building known deps before
launching a job is the right option.
But here, because collating is very light (something like just executing `cat` on the reports), it is better to check `tests_suilte.lst` first,
and if it changed, rerun the collation before ensuring (old) tests have run.

This is the purpose of the `critical` flag.
Such a flag can either be passed when declaring static deps in a rule, or dynamically using `lmake.depend` or `ldepend`.

The collating rule would look like:

- Set the `critial` flag on `test_suite.lst` (before or after actually reading it, this has no impact).
- Read `test_suite.lst`.
- Call `ldepend` on the reports listed in `test_suite.lst`.
  This is optional, just to generate parallel deps instead of automatic sequential deps (but if done, it must be before actually reading the reports).
- Collate reports listed in `test_suite.lst`.

And the succession of job would be:

- `test_suite.rpts` is rebuilt before analyzing `test1.rpt` and `test2.rpt` because `test_suite.lst` has changed.
- Open-lmake sees that `test3.rpt` is needed instead of `test2.rpt`.
- Hence, `test1.rpt` and `test3.rpt` are (re)built in parallel.
- `test_suite.rpts` is finally built from `test1.rpt` and `test3.rpt`.
