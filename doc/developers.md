<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Directory layout
- `_bin`       : contains scripts mostly used for building open-lmake
- `debian`     : contains files necessary to build a debian package
- `doc`        : contains documentation
- `docker`     : contains the docker files used to validate open-lmake
- `examples`   : contains abundantly commented examples
- `ext`        : contains all code coming from external sources
- `_lib`       : contains all python code
- `lmake_env`  : contains an open-lmake repo that allow to build open-lmake (not fully functional) under open-lmake (used as an example/unit test)
- `museum`     : contains dead code that could come back to life in a near or far future
- `src`        : contains all C++ source files
- `src/engine` : contains all C++ source files that are specific to lmakeserver
- `unit_tests` : contains unit tests

# Coding rules

## Statics & globals

- variables with executable cxtor/dxtor are never put (or with much care) in statics or globals
	- this includes string's, despite these being declared constexpr in the STL
- reason is that init order is unpredictible, as is finalization order
- this induces hard to find bugs
- when required, a pointer (initilized to nullptr) and a new is done in a timely manner
- this way, variable is not automatically destructed

## Cases
- CamelCase for namespaces, types, template parameters and constexpr
- snake\_case for other names

## Name prefixes
- `_`   : private (including static functions in .cc files that are de facto not accessible from elsewhere)
- `c_`  : const
- `s_`  : static
- `g_`  : global
- `t_`  : thread local
- `np_` : non-portable
- `::`  : standard library or a few exceptions defined in `src/utils.hh` which, in my mind, should have been part of the STL, e.g. `::vmap` (a vector of pairs)

Names are suffixed with \_ if needed to suppress ambiguities.
Several prefixes can be used, for example a private static variable will start with `_s_`.

## Abbreviations
- general rules:
	- words are abbreviated depending on their use and span: the shorter the span and the heavier the usage , the more they are abbreviated
	- words may be abbreviated by their beginning, such as env for environ
	- words may be abbreviated using only consons such as src for source
	- these may be combined as in dst for destination
	- words may further be abbreviated to a single letter or by the first letter of each word (e.g. tf for target flag) when name spans no more than a few lines
	- words include standard name such as syscall names or libc functions
- special cases:
	<table>
	<tr> <th> abbrev   </th> <th> full-name              </td> </tr>
	<tr> <td> crc      </td> <td> checksum               </td> </tr>
	<tr> <td> ddate    </td> <td> disk date              </td> </tr>
	<tr> <td> dflag    </td> <td> dependency flag        </td> </tr>
	<tr> <td> lnk      </td> <td> symbolic link          </td> </tr>
	<tr> <td> ongoing  </td> <td> on going               </td> </tr>
	<tr> <td> pdate    </td> <td> process date           </td> </tr>
	<tr> <td> psfx     </td> <td> prefix/suffix          </td> </tr>
	<tr> <td> regexpr  </td> <td> regular expression     </td> </tr>
	<tr> <td> serdes   </td> <td> serialize, deserialize </td> </tr>
	<tr> <td> tflag    </td> <td> target flag            </td> </tr>
	<tr> <td> wrt      </td> <td> with respect to        </td> </tr>
	</table>

## File layout
- lines are limited to 200 characters (as is this document source)
- functions are limited to 100 lines:
	- there are few exceptions, though, where it was impossible to cut without making too artificial a sub-function
- generally speaking code is put on a single line when several lines are similar and alignment helps readability
- separators (such as commas, operators, parentheses, ...) pertaining to the same expression are at the same indentation level
	- and subexpressions are at the next indentation level if on one or several lines by themselves
	- the identation level is the number of tab's appearing on the line before the considered item
	- when item is less than 3 characters long, the next indentation level (preceded by a tab) is on the same line
	- expressions are either compact (no space or minimum), spaced (a space between each operators and sub-expressions) or multi-line (with sub-expressions indented)
	- example:
`
		a = make_a(
			my_first_coef [  0] * my_first_data  // note alignment makes expression structure appearing immediately
		+	my_second_coef[i  ] * my_second_data // note + at identation level 2, subexrpession at indentation level 3
		+	my_third_coef [i*2] * my_third_data  // note following comment means this one is repeated
		+	my_foorth_coef[i*3] * my_foorth_data // .
		) ;
`

## Special words
These special words deserv a dedicated syntax coloring in your prefered editor :

- `DF`                          : `default : FAIL() ;`, used at the end of switch statements when all cases are suposed to be enumerated
- `DN`                          : `default : ;`       , used at the end of switch statements to allow default as noop
- `throw_if` and `throw_unless` :                       functions that take a condition as 1st arg and throw a string made after other args if condition is (is not) met
- `self`                        : `(*this)`             fix c++ that should have defined this as a reference rather than a pointer, makes the code significantly lighter

## Invariants are either
- in swear/fail if reasonably fast to check
- in a function `chk` (even if never called) when they can be expressed programatically
- else in comments preceded by the keyword INVARIANT

## Invariants are expected and enforced by public methods
- this is a general consideration: methods are private/public depending on their dangerosity
- private methods can violate them, though
	- e.g. `_clear` are local methods violating invariants, clear handles consequences
- public methods can violate unrelated invariants they can live w/
	- e.g. `Node.mk_plain` does not enforce job invariant about having all its targets set

## Data members are
- public when they can be read with no pre-condition and modified individually w/o violating invariants
- private otherwise

## `if` branch order
When there is a choice between "if (cond) branch1 else branch2" and "if (!cond) branch2 else branch1", the order is governed by the following prioritized considerations (prefer means put first):

- if there is natural chronological order between branch1 and branch2, respect the natural order
- prefer simpler branch
- prefer normal case to error case
- prefer "true" case to "false" case (at semantic level)
- prefer positive test (i.e. prefer == to !=, x or +x to !x, etc.)

## `bool` values and `if`
Most objects have a natural "empty" value, such as empty strings, empty vectors, the first value of an enum, etc.

- It is extremely practical to write `if (err_msg) process_err() ;` rather than `if (!err_msg.empty()) process_err() ;`
- This suggests to have casts to bool mostly everywhere, but:
	- this does not apply to enum nor to STL classes
	- this creates a lot of ambiguities
	- this is actually pretty dangerous as this weakens static type checking (as bool can in turn be converted to int...)
- The pefect balance is to define the prefix operators `+` (non empty) and `!` (empty):
	- we can write `if (+err_msg) process_err() ;` or `if (!err_msg) process_ok() ;` which is still very light
	- it can apply to any type

## goto's
- goto's are used when they allow the code to be easier to read and understand
- they are always forward unless specifically flagged with a `BACKWARD` comment, which is exceptional

## comments
- comments can be realigned with the command `_bin/align_comments 4 200 [// or #]`
- standard comments
	- `//vvvvvvvvvvvvvvvvvvvvvvvvvvvv`
	- `main_purpose_of_the_function()`
	- `//^^^^^^^^^^^^^^^^^^^^^^^^^^^^`
	- `XXX`            means something has to be reworked              (it is recommanded to highlight it in you editor)
	- `/!\`            means something requires your special attention (it is recommanded to highlight it in you editor)
	- `BACKWARD`       means the associated goto goes backward
	- `INVARIANT`      means an invariant is described
	- `fast path`      means that the corresponding code can be suppressed without altering the semantic
	- `.`              means same as above
	- ` ...` at the end means comment continue on next line where `...` appears again
	- `/*garbage*/`    means we dont care about the value, it is only there to be certain having no uninitialized values

## Guideline
we apply <https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines> to the best extent.

# General description

## Main classes
- Req represents an lmake command
	- several `Req`'s may be present as several lmake command may run (but there is a single server serving all of them)
- `Rule` represents a derivation rule
	- i.e. a pattern to derive some target files from some source files
- `Job` represents a job
	- i.e. a script that may be executed by instantiating a Rule for a particular set of stems
	- a Job has several targets and several dependencies
	- targets may be explicit in the Rule (it is said static), or it may be described as matching a regular expression (it is said star).
		- this is not to be confused by regular expressions used to match a Rule. For example:
			- `{File}.o`            in a compilation rule   : this is a static target (a job produces a single .o)
			- `{Dir}.untar/{File*}` in a tar exapnsion rule : these are star targets  (a job produces a bunch of files in a single directory)
	- dependencies may be expressed explicitly in the Rule (aka static), or discovered by spying job execution syscall's (aka hidden or dynamic)
		- typically in a compilation rule, .c files are static deps, .h files are hidden deps
- `Node` represents a file
	- a `Node` has a prioritized list of `Job`'s to try to generate it
	- `Job`'s with higher priorities are tried first
	- at given priority, if several jobs can be executed:
		- they are all tried in parallel, hoping that a single one will actually generate the node
			- if several of them actually generate the node, it will be an error condition
		- unless it can be certain in advance that several of them will generate the node, in which case the error is generated before execution

## The heart of the algorithm is composed of
- for static considerations (i.e. does not depend on Req):
	- `Node::set_buildable`: analyse a Node and determine if it can be made (3-way answer: No, Yes, Maybe)
		- calls `Job::Job` on Job candidates (down-hill recursion)
	- `Job::Job` for the plain case: construct a Job if its dependances have a chance to be makable
		 - calls `Node::set_buildable` on static deps (down-hill recursion)
- for dynamic considerations (i.e. depends on Req):
	- `Node::make`  : analyse a Node, calling Job::make on job candidates that can produce it
		- calls `Job::make` on Job candidates (down-hill recursion)
		- calls `Job::make` on Job's waiting for it as a dep (up-hill recursion)
	- `Job::make`   : analyse a Job, looking at deps and calling submit if necessary, and waking up dependents if up to date
		- calls `Node::make` on all deps (down-hill recursion)
		- calls `Job::submit`
		- calls `Node::make` on targets just made up-to-date (up-hill recursion)
	- `Job::submit` : submit a Job
		- launch job execution which will eventually trigger a call to Job::end when done
	- `Job::end`    : analyse a Job at end of execution, calling make to analyse the result
		- calls `Job::make` to analyze execution and ensure everything is ok (or re-submit if there is any reason to do so)

## State
- the state is directly maintainted on disk in mapped files:
	- files are located in `LMAKE/store`
	- code that handle them is in:
		- `src/lmakeserver/store.hh` & `.cc` for its part specific to lmake
		- `src/store`                        for generic code that handle:
			- simple objects (possibly with side-car, i.e. a secondary storage with a 1 to 1 correspondance)
			- vectors
			- prefix-tree
			- red-black tree (not used in lmake, could be suppressed)
- the prefix tree is mostly used to store file and job names
	- only a 32 bits id is used in most of the code
	- id's (the Node and Job objects are very light objects containing only the id) can find their name with the name() method
	- names can find their associated id by just constructing the Name or Job object with the name as sole argument
	- overall, this is extremely efficient and fast
		- need about 20-40 bytes per file, independently of the name length which is often above 200
		- building the name string from the tree is marginally slower than a simple copy and the id mechanism makes this need the exception rather than the rule
- this makes booting extremely fast, suppressing the need to keep a live daemon
- persistent states are associated with Rule's, Job's, Node's but not Req's

## Traces
- when `lmake` is executed, a trace of activity is generated for debug purpose (unless compiled with `$LMAKE_FLAGS` including `t`)
- this is true for all executables (`lmake`, `lmakeserver`, `lautodep`, ...)
- traces are located in:
	- `LMAKE/lmake/local_admin/trace/<executable>`
		- for `lmakeserver`, the most important trace, an history of the last few executions is kept
	- `LMAKE/lmake/remote_admin/job_trace/<seq_id>` for remote job execution
- the first character of each line is either ' or "
	- this is because the trace file is managed as a circular buffer for performance
	- so each time we wrap around, this first character is toggled between ' and "
- trace entries are timestamped and a letter indicates the thread:
	- '=' refers to the main thread
	- in server:
		- `C` : cancel jobs in sge backend
		- `D` : handle lencode/ldecode
		- `E` : job end
		- `G` : SGE launch jobs
		- `H` : heartbeat
		- `K` : cancel jobs in slurm backend
		- `J` : record job data
		- `L` : Local launch jobs
		- `M` : job management
		- `Q` : manage queries from clients
		- 'P' : intermediate thread that launches ptrace'd children
		- `R` : deferred reports
		- `S` : job start
		- `T` : wait terminated processes in local backend
		- `U` : Slurm launch jobs
		- `W` : deferred processing of wakeup connection errors
		- 'Y' : thread used to gather stderr from python when printing stack trace in case of uncaught exception and !HAS\_MEMFD
	- in job exec:
		- `<number>` : compute crc
	- in lmake:
		- `I` : manage ^C
- trace records are indented to reflect the call graph
	- indentation are done with tabs, preceded by a follow up character (chosen to be graphically light), this eases the reading
	- when a function is entered, a `*` replaces the follow up character
- to add a trace record:
	- in a function that already has a `Trace` variable, just call the variable with the info you want to trace
	- else, declare a variable of type `Trace`. The first argument is a title that will be repeated in all records using the same trace object
	- all `Trace` objects created while this one is alive will be indented, thus reproducing the call graph in the trace
	- booleans cannot be traced as this is mostly unreadable. Instead use the `STR` macro to transform the boolean into a string. `STR(foo)` will be `foo` if foo is true, else `!foo`.

## Streams
Open-lmake does not use stream at all.
The reason is that there are problems with streams when using statically linked libstdc++ (required to be robust to all installations) when python modules refer to it dynamically.
By the way, the execution is lighter and code is not heavier.

# Modification

* before pushing any modification:
	- run make without argument to check nothing unrelated to your modifications is broken

* to add a slurm version
	- in slurm source tree, type `./configure`
	- copy file `slurm/slurm.h` from slurm source tree to `ext/slurm/<version>/slurm/slurm.h`
	- also copy included files from there, as of today, they are `slurm/slurm_errno.h` and `slurm/slurm_version.h`
	- git add `ext/slurm/<version>`
	- <version> may be a prefix, e.g. `24.11` works for all `24.11.x`
	- git clean and remake

* to add a backend:
	- make a file `src/lmakeserver/backends/<your_backend>.cc`
		- and `git add` it
	- use `src/lmakeserver/backends/local.cc` as a template
	- run `git grep PER_BACKEND` to see all parts that must be modified

* to add a cache:
	- make files `src/lmakeserver/caches/<your_cache>.hh` & `.cc`
		- and git add them
	- use `src/lmakeserver/caches/dir_cache.hh` & `.cc` as a template
	- run `git grep PER_CACHE` to see all parts that must be modified

* to add a command handled by the server:
	- make a file `src/<your_command>.cc`
		- and `git add` it
	- use `src/lshow.cc` as a template
	- run `git grep PER_CMD` to see all parts that must be modified

# Coverage
In order to produce coverage results, do the following:
	- `git clean -ffdx` (or at least `find . -name '*.gcda' -o -name '*tok' | xargs rm ; git clean -ffdx lmake_env*` if C flag is already in use)
	- `LMAKE_FLAGS=C make ALL` (or faster : `LMAKE_FLAGS=C make -j8 LMAKE LINT ALIGN lmake.tar.gz ; make ALL`)
		- a few fails are ok (as of now, `unit_tests/namespaces.dir/tok` fails)
	- `make GCOV`

All data are generated in the `gcov` directory:
	- `summary` : a file level summary showing a count of uncovered lines and total lines for each file
	- `gcov_dir` : the directory that was used to generate coverage reports, it contains raw outputs from `gcov`
	- a copy of the source tree where files are annotated with execution count on a line by line basis
