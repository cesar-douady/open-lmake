# Why sticking to alpha-algorithm

[this paper](https://gittup.org/tup/build_system_rules_and_algorithms.pdf) does a fairly good analysis of the goal of a build system.
In particular it points out the importance of the performance.

However, it takes as granted the fact that the user will most often do a full build of their project.
There are numerous situations where they do not, for example:

- They work on a component of the whole project and test it with a unit test.
- The full flow not only describe the compilation of a software, but also a bunch of tests (probably much more than the number of source files) and they are working one failing test.

In such situations, it may very well happen that the partial DAG deriving forward from the updated sources is larger than the partial DAG deriving backward from the built targets.
Ideally, we would like to build the partial DAG from updated sources to built the targets, but this is harder to do and not described in the paper.

Also, it is considered as granted that the time to go through the DAG (backward from the targets) is significant.
This is true for `make`, but is not necessary:

- According to the paper, `makes` takes about half an hour to analyze 100.000 files.
- `open-lmake` takes less than a second in the same situation, less than a common single compilation.

While I can't explain what is going on on the `make` side, what happends on the `open-lmake` side is:

- It is not necessary to `stat` all intermediate files, only source files need to be `stat`ed.
- `open-lmake` keeps its internal DAG data-base on disk in an optimized format, directly mapped in the process at start-up.

Mapping this entire DAG has constant time as long as it is not read, and only the partial DAG aiming to the targets will ever be read, leading to time proportional to its size.
On average, it may very well be comparable to the size of the DAG originating from the updated sources.
And in all cases, we are speaking of less than a second to an already rather large project.

Also, detecting that a file has been modified is far from easy while keeping a good reliability level:

- The IDE and the source control system may very well be instrumented (as is any job in `open-lmake`), but what if the user uses a plain linux command (such as `sed` or whatever) ?
- A solution would be to use `inotify` (which probably did not exist at the time the paper was written), but:
	- it would require one `inotify` instance per source file, which makes it impractical as soon as the project reaches an even moderate size,
	- `inotify` advertises itself as _unreliable_, for example is the file is modified on another host through a network file system, such modification would go unnoticed.
- Another solution would be to use `fuse` and again, nothing prevents the user from modifying directly the underlying file without going through `fuse`.

So the only reliable way is to explore the partial DAG aiming at the asked targets, a time that will go unnoticed by the user in most cases,
except a extreme cases (such as building a full Linux distro) where a few seconds of delay will be easily accepted, even if nothing has changed.
