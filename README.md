# open lmake
This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)

Copyright (c) 2023 Doliam

This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).

This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

# purpose 

lmake is like make, except that it is practical, scalable and reliable

## more specifically

- handle parallelism & remote execution
- automatic dependency tracking :
	- system activity is tracked to detect file accesses (reads & writes)
	- dependencies are automaticcaly added upon read
	- no need for any kind of pre-analysis
	- even dependencies ot non-existing files are keps, in case they appear
- it is extremely fast :
	- everything is cached, in particular dependencies so that the discovery process is only run when a job is run for the first time or when execution showed they changed
	- up-to-date analysis is based on CRC's, not on date, so that when a target is remade identically to its previous content, dependents are not remade
	- all the internal engine is based on id's, not strings, strings are only used to import (e.g. dep tracking) or export (job execution)
- it is extremely memory efficient :
	- keeping the state of all dependencies is inherently very expensive
	- thorough efforts are made to keep this book keeping minimal
	- in particular, names (that tend to be rather long in practice) are kept in a prefix-tree sharing the directory parts
- generally speaking 1.000.000 targets can be handled with no burden
- makefile is based on Python3.6 (& upward) :
	- no reason to invent a new language
	- each rule is a class
	- can leverage loops, conditions, inheritance, ... all Python power
	- job scripts can be either shell scripts or Python functions
	- very readable, no cabalistic automatic variables make has
- target matching is based on regular expressions :
	- rather than the very poor '%' of make
	- there can be several stems
	- dependencies are generating using f-strings, very flexible
- rules may have several targets :
	- usually target list is known before job is executed
	- but target reg-expr is also supported for rules such as untar that generate a bunch of targets whose precise list depends on source content
- lmake is oriented towards reproducibility :
	- it tracks the source control system (usually git) and refuses to rely on data that are not tracked
		- you can force such dependencies, in which cases these exceptions will be reported at each run
- and more...

# installation 

## requirements
- c++20
- python 3.6 or later with developer support (i.e. access to the Python.so file)

it has been tested with the dockers listed in the docker directory

## to compile lmake

- modify the first few lines of Makefile under "build configuration" to suit your needs if required
- type make
	- this builds all necessary files and some unit tests
	- you must invoke 'git clean -fdx' if you modified the Makefile
	- you may have to invoke 'git clean -fdx lmake_env' or even 'git clean -fdx' after a 'git pull'
		- lmake_env is a directory which builds lmake under lmake, for test purpose, no guarantee that the resulting lmake is funtional for now
		- it is not cleaned on purpose before running as this creates variability for testing lmake, but may fail
		- and generally speaking, make is not robust to past history, so a full 'git clean -fdx' may be necessary
	- you can type make LMAKE to just build all necessary files
	- you can type make lmake.tar.gz (built by default) to make a tar ball of the compiled files that you can easily deploy
- install
	- untar lmake.tar.gz wherever you want and have your PATH point to the bin directory.
		- the bin sub-dir contains the executables meant to be executed by the user
		- the _bin sub-dir contains the executables necessary for lmake to run, but not meant to be directly invoked by the user
			- it also contains some executables to help debugging lmake itself.
		- the lib sub-dir contains binary and python files for use by the user
		- the _lib sub-dir contains the binary and python files necessary for lmake to run, but not meant for direct use by the user
		- the relative positions of these 4 dirs must remain the same, i.e. they must stay in the same directory with the same names.
- specialization
	- you can specialize the build process to better suit your needs :
	- this is done by modifying the first lines of Makefile, between the vvvvvvvvvvvvv line and the ^^^^^^^^^^^^^^^^ line
	- if you modify these variables, you should execute git clean as make will not detect this modification automatically
	- reasonable specializations are :
		- modify the -j option in the MAKEFLAGS variable to tailer the degree of parallelism of the build process
		- modify the compiler you want to use through the CC variable
		- modify the python you want to use through the PYTHON variable
		- modify the -fsanitize options through the ASAN_FLAGS and TSAN_FLAGS (which are exclusive of each other)  to produce a code with numerous checks, but rather slow
		- modify the compilation optimization level
			- if you want to debug, -O0 -g is recommanded as debugging with optimizations is significantly more challenging
			- if you want to do some performance measurement, you should use -O3 -DNDEBUG, not -O3 alone as this will suppress an enormous quantity of superfluous checks
			  (essential and cheap ones are kept, though). This will become the production flags when lmake is reliable enough.
			- as long as lmake is in beta state, default flag is -O3, so that there are numerous checks through out execution.
			  Adding -g multiplies the executable sizes by about 10x.

# coding rules

## statics & globals

- variables with executable cxtor/dxtor are never put (or with much care) in statics or globals
	- this includes string's, despite these being declared constexpr in std lib
- reason is that init order is unpredictible, nor finalization order
- this induces hard to find bugs
- when required, a pointer (initilized to nullptr) and a new is done in a timely manner
- this way, variable is not automatically destructed

## cases
- CamelCase for namespaces, types, template parameters and constexpr
- snake_case for other names

## name prefixes
- _   : private (including static functions in .cc files that are de facto not accessible from elsewhere)
- s_  : static
- g_  : global
- t_  : thread local
- np_ : non-portable

names are suffixed with _ if needed to suppress ambiguities

## abbreviations
- general rules :
	- words are abbreviated using the following list if they are not exposed to users
	- words may further be abbreviated using only consons when name spans no more than a page (~100 lines)
	  or when such abbreviation makes code more readable
	- words may further be abbreviated to a single letter when name spans no more than a few lines
	- else words are not abbreviated
- special cases :
	abbrev full-name
	cap       capacity
	del       delete
	dict      dictionary
	dep       dependency
	diff      difference
	dst       destination
	expr      expression
	filename  file_name
	hdr       header
	id        identifier
	idx       index
	len       length
	namespace name_space
	ongoing   on_going
	par       parallel
	proc      procedure, process
	regexpr   regular_expression
	res       result
	rsv       reserv, reserved
	ser       serial
	sock      socket
	symlink   symbolic link
	tab       table
	tmp       temporary
	tmpl      template
	val       value
	wakeup    wake_up

## layout
- lines are limited to 200 characters
- functions are limited to 100 lines :
	- there are a few exceptions, though, where it was impossible to cut without making too artificial a sub-function
- generally speaking code is put on a single line when several lines are similar and alignment helps readability
- separators (such as commas, operators, parentheses, ...) pertaining to the same expression are at the same indentation level
	- and subexpressions are at the next indentation level if on one or several lines by themselves
	- the identation level is the number of tab's appearing on the line before the considered item
	- when item is less than 3 characters long, the next indentation level (preceded by a tab) is on the same line
	- expressions are either compact (no space or minimum), spaced (a space between each operators and sub-expressions) or multi-line (with sub-expressions indented)
	- example :
		a = make_a(
			my_first_coef [  0] * my_first_data                            // note alignment makes expression structure appearing immediately
		+	my_second_coef[i  ] * my_second_data                           // note + at identation level 3, subexrpession at indentation level 4
		+	my_third_coef [i*2] * my_third_data                            // note following comment means this one is repeated
		+	my_foorth_coef[i*3] * my_foorth_data                           // .
		) ;

## invariants are either
- in swear/fail if reasonably fast to check
- in a function chk (even if never called) when they can be expressed programatically
- else in comments preceded by the keyword INVARIANT

## invariants are expected and enforced by public methods 
- this is a general consideration : methods are private/public depending on their dangerosity
- private methods can violate them, though
	- e.g. _clear are local methods violating invariants, clear handles consequences
- public methods can violate unrelated invariants they can live w/
	- e.g. Node.mk_plain does not enforce job invariant about having all its targets set

## data members are 
- public when they can be modified individually w/o violating invariants
- private otherwise

## comments
- placement w.r.t. code
	- generally, comments start in column 80 (assuming tab of 4 characters)
	- they are put in columns 20, 40 or 60 if commenting lists of short items (data members, enum members, ...)
	- the are always separated with code with at least 1 character and 1 line
	- comments in successive lines are aligned
- standard comments
	- //vvvvvvvvvvvvvvvvvvvvvvvvvv
	- main purpose of the function
	- //^^^^^^^^^^^^^^^^^^^^^^^^^^
	- "XXX"            means something has to be reworked
	- "/!\"            means something requires your special attention
	- "INVARIANT"      means an invariant is described
	- "fast path"      means that the corresponding code can be suppressed without altering the semantic
	- "."              means same as above
	- "..." at the end means comment continue on next line
	- /*garbage*/      means we dont care about the value, it is only there to be certain having no uninitialized values

- apply https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines to the best extent

# general description

## main classes
- Req represents an lmake command
	- several Req's may be present as several lmake command may run (but there is a single server serving all of them)
- Rule represents a derivation rule
	- i.e. a pattern to derive some target files from some source files
- Job represents a job
	- i.e. a script that may be executed by instantiating a Rule for a particular set of stems
	- a Job has several targets and several dependencies
	- targets may be explicit in the Rule (it is said static), or it may be described as matching a regular expression (it is said star).
		- this is not to be confused by regular expressions used to match a Rule. For example :
			- {File}.o            in a compilation rule   : this is a static target (a Job produces a single .o)
			- {Dir}.untar/{File*} in a tar exapnsion rule : these are star targets  (a Job produces a bunch of files in a single directory)
	- dependencies may be expressed explicitly in the Rule (is is said static), or discovered by spying job execution syscall's (it is said hidden)
		- typically in a compilation rule, .c files are static deps, .h files are hidden deps
- Node represents a file
	- a Node has a prioritized list of Job's to try to generate it
	- Job's with higher priorities are tried first
	- at given priority, if several jobs can be executed :
		- they are all tried in parallel, hoping that a single one will actually generate the node
			- if several of them actually generate the node, it will be an error condition
		- unless it can be certain in advance that several of them will generate the node, in which case the error is generated before execution

## the heart of the algorithm is composed of
- for static considerations (i.e. does not depend on Req) :
	- Node::set_buildable         : analyse a Node and determine if it can be made (3-way answer : No, Yes, Maybe)
		- calls Job::Job on Job candidates (down-hill recursion)
	- Job::Job for the plain case : construct a Job if its dependances have a chance to be makable
		 - calls Node::set_buildable on static deps (down-hill recursion)
- for dynamic considerations (i.e. depends on Req) :
	- Node::make  : analyse a Node, calling Job::make on job candidates that can produce it
		- calls Job::make on Job candidates (down-hill recursion)
		- calls Job::make on Job's waiting for it as a dep (up-hill recursion)
	- Job::make   : analyse a Job, looking at deps and calling submit if necessary, and waking up dependents if up to date
		- calls Node::make on all deps (down-hill recursion)
		- calls Job::submit
		- calls Node::make on targets just made up-to-date (up-hill recursion)
	- Job::submit : submit a Job
		- launch job execution which will eventually trigger a call to Job::end when done
	- Job::end    : analyse a Job at end of execution, calling make to analyse the result
		- calls Job::make to analyze execution and ensure everything is ok (or re-submit if there is any reason to do so)

## state
- the state is directly maintainted on disk in mapped files :
	- files are located in LMAKE/store
	- code that handle them is in :
		- src/lmakeserver/store.hh&cc for its part specific to lmake
		- src/store                   for generic code that handle :
			- simple objects (possibly with side-car, i.e. a secondary storage with a 1 to 1 correspondance)
			- vectors
			- prefix-tree
			- red-black tree (not used in lmake, could be suppressed)
- this makes booting extremely fast, suppressing the need to keep a live daemon
- persistent states are associated to Rule, Job, Node but not Req

## traces
- when lmake is executed, a trace of activity is generated for debug purpose
- this is true for all executables (lmake, lmakeserver, autodep, ...)
- traces are located in :
	- LMAKE/trace/<executable>
		- for lmakeserver, the most important trace, an history of a few last execution is kept
	- LMAKE/trace/job_exec/<job_id> for remote job execution

# modification

* to add a backend :
	- make a file src/lmakeserver/backends/<your_backend>.cc
		- and git add it
	- use src/lmakeserver/backends/local.cc as a template
	- run 'git grep PER_BACKEND' to see all parts that must be modified

* to add a cache :
	- make files src/lmakeserver/caches/<your_cache>.hh & .cc
		- and git add them
	- use src/lmakeserver/caches/dir_cache.hh & .cc as a template
	- run 'git grep PER_CACHE' to see all parts that must be modified

* to add a command handled by the server :
	- make a file src/<your command>.cc
		- and git add it
	- use src/lshow.cc as a template
	- run 'git grep PER_CMD' to see all parts that must be modified
