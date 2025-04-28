<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->
<!-- Why open-lmake-->

# Binary packages

Open-lmake binary packages are available on launchpad.net for the following systems :

- ubuntu22.04 (jammy)
- ubuntu24.04 (noble)

To install these, execute:

- `sudo add-apt-repository ppa:cdouady/open-lmake`
- `sudo apt update`
- `sudo apt install open-lmake`

# Compilation

## Requirements

To compile open-lmake, you will need:

- c++20
- python3.6 or later with developer support (i.e. access to the `libpython*.so` file)

It has been tested with the dockers listed in the docker directory

## Procedure

- type `make`
	- this builds all necessary files and some unit tests
	- you must invoke `git clean -fdx` if you modified the Makefile or otherwise if you want a reliable build (`make` is not open-lmake)
	- you may have to invoke `git clean -fdx lmake_env*` or even `git clean -fdx` after a `git pull`
		- `lmake_env` is a directory which builds lmake under lmake, for test purpose, no guarantee that the resulting package is funtional for now
		- `lmake_env-cache` is a directory containing cached results from jobs in `lmake_env`
		- they are not cleaned on purpose before running as this creates variability for testing lmake, but may fail
		- and generally speaking, make is not robust to past history, so a full 'git clean -fdx' may be necessary to get a reliable build
	- you can type `make LMAKE` to just build all necessary files
	- you can type `make lmake.tar.gz.SUMMARY` (built by default) to make a tar ball of the compiled files that you can easily deploy
- install
	- untar `lmake.tar.gz` wherever you want and have your `$PATH` point to the `bin` directory.
		- the `bin` sub-directory contains the executables meant to be executed by the user
		- the `_bin` sub-directory contains the executables necessary for open-lmake to run, but not meant to be directly invoked by the user
			- it also contains some executables to help debugging open-lmake itself.
		- the `lib` sub-directory contains binary and python files for use by the user
		- the `_lib` sub-directory contains the binary and python files necessary for open-lmake to run, but not meant for direct use by the user
		- the relative positions of these 4 directories must remain the same, i.e. they must stay in the same directory with the same names.
- specialization
	- you can specialize the build process to better suit your needs:
	- this can be done by setting variables
		- for example, you can run: `CXX=/my/g++ make`
		- `$PYTHON2. can be set to your preferred python2 (defaults to `python2` as found in your `$PATH`). You will be told if it is not supported.
		- `$PYTHON` can be set to your preferred python3  (defaults to `python3` as found in your `$PATH`). You will be told if it is not supported.
		- `$CXX` can be set to your preferred C++ compiler (defaults to `g++`     as found in your `$PATH`). You will be told if it is not supported.
		- `$SLURM_ROOT` can be set to the root directory of the slurm installation (by default, `slurm/slurm.h` will be searched in the standard include path).
		  For example, `slurm.h` will be found as `$SLURM_ROOT/include/slurm/slurm.h`
		- `$LMAKE_FLAGS` can be defined as O[0123]g?d?t?S[AB]P?
			- O[0123] controls the `-O option`                                      (default: 1 if profiling else 3            )
			- g       controls the absence of `-g option`                           (default: debug                            )
			- d       controls     `-DNDEBUG`                                       (default: asserts are enabled              )
			- t       controls     `-DNO_TRACE`                                     (default: traces are enabled               )
			- SA      controls the `-fsantize=address -fsanitize=undefined` options (exclusive with ST                         )
			- ST      controls the `-fsantize=thread`                       option  (exclusive with SA                         )
			- P       controls the `-pg`                                    option  (profiling info is in gmon.out.<tool>.<pid>)
		- the `-j` flag of make is automatically set to the number of processors, you may want to override this, though
	- this is true the first time you run make. After that, these values are remembered in the file `sys_config.env`.
	- you can freely modify this file `sys_config.env`, though. It will be taken into account.
	- it is up to you to provide a suitable `$LD_LIBRARY_PATH` value.
	  it will be transferred as a default value for rules, to the extent it is necessary to provide the lmake semantic

# Installation

Open-lmake does not require to be installed.
You can run it directly from the build directory.
This is the simplest way unless you seek a system-wide installation.

If running under Ubuntu and you have the necessary packages installed (that you can find by inspecting Makefile, the entry `DEBIAN_DEPS`),
you can make a Debian package:
- type `make DEBIAN`
- the package is `open-lmake_v25.02.7-1_<arch>.deb`
- you can install it with `sudo apt install ./open-lmake_v25.02.7-1_<arch>.deb`

Alternatively, you can untar `lmake.tar.gz` at any place.

Installing system-wide with Debian package will take care of placing both binaries and man pages in the standard directories. Alternatively, you can:

- put `/path/to/open-lmake/bin` in your `$PATH`

This will simplify the user experience but is not required.

# AppArmor

In order to implement namespace related features (the `lmake_view`, `repo_view`, `tmp_view`, `views` and `chroot` rule attributes), and if your system is configured with AppArmor,
adequate rights must be provided.

In that case, do the following :
- create a file `/etc/apparmor.d/open-lmake` with the following content :
	```
		abi <abit/4.0>
		include <tunables/global>
		profile open-lmake /**/{_bin/job_exec,bin/lautodep} flags=(unconfined) {
			userns,
		}
	```
- activate it with `sudo aa-apparamor_parser -r /etc/apparamor.d/open-lmake`
