Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023-2026 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

Header(ldebug,run a job in a debug environment)

.SH DESCRIPTION
.LP
B(ldebug) generates a script that precisely mimics the job execution under C(lmake) control, and runs it.
.LP
Once the script is generated, its path is printed.
It can then be freely modified and executed without needing to run B(ldebug) again.
.LP
The job must have been run with B(lmake) before (but may not have finished, it just needs to have started) because a lot of information is generated at that time.
The precise way the job is launched is controled by the configuration B(lmake.config.debug[<key>]) which provides the name of a module to import.
This module must contain a (gen_script) function taking a description of the job provided as keyword arguments and returning the script to execute.
Several such scripts exist in I(lib/lmake_debug) in the installation dir and can serv as examples to start with.
.LP
When B(ldebug) is run, the debug script is generated in a file within the I(LMAKE) dir and, unless I(--no-exec), it is executed.

ClientGeneralities()

ClientOptions(job)

SpecificOptions

Item(B(-k) I(key),B(--key)=I(key))
Use key to find the function generating the debug script.
Default is B('') (the empty string), which looks by default (unless overridden in I(Lmakefile.py)) in the I(lmake_debug/default.py) module.

Item(B(-n),B(--no-exec))
Dont run the debug script, stop after generation.

Item(B(-t),B(--std-tmp))
By default the tmp dir used during job execution is the one provided by the B($TMPDIR) variable for the job if there is one.
If its value is B(...) (python ellipsis), the value provided by the local backend (and not the backend for the job as debug execution is local) is use.
If this does not lead to a value, the standard value is used : I(LMAKE/debug/<job_id>/tmp).
When this option is used, the tmp dir is forced to its standard value, regardless of job and backend environment.

Item(B(-T) I(abs_dir),B(--tmp-dir)=I(abs_dir))
When this option is used, it forces the tmp dir for job execution to its value.

.SH STANDARD METHODS
.LP
Unless overridden in the configuration (through the I(dict) B(lmake.config.debug)), the following standard debug methods are provided :
.nr TW 0 Comment( prevent debian packaging tool from grumbling about TW not being defined )
.TS
tab(!) ;
c           c                     l                                     l                          c
_           _                     _                                     _                          _
cB          cI                    l                                     l                          c
.
Key       ! Module              ! python job                           ! shell job               ! Note
<default> ! lmake_debug.default ! run under pdb control                ! run with the B(-x) flag ! (1)
g         ! lmake_debug.gdb     ! interpreter is run under gdb control ! idem                    ! (2)
u         ! lmake_debug.pudb    ! run under pudb control               ! Not supported           ! (3)
e         ! lmake_debug.enter   ! dont run                             ! idem                    ! (4)
n         ! lmake_debug.none    ! run normally with no debug support   ! idem
.TE
.IP (1)
Running with the B(-x) flag usually produces a trace of executed commands.
.IP (2)
Alias B(r) is redefined to run with adequate redirections.
.IP (3)
Providing a working B(pudb) with redirected stdin/stdout necessitated to patch it. This patch is dynamically applied as part of the job start up procedure.
.IP (4)
An interactive shell (as provided by B($SHELL), which defaults to I(/bin/bash)) is open in the job environment (environment, cwd, namespace, chroot, mounts, tmp, ...).
.IP
B($HOME) and B($SHLVL) are kept from actual environment to ease interactive session.
In particular B($SHLVL) can be used to provide a differentiated prompt if adequately defined in the start up file (usually I(~/.bashrc)).

.LP
In addition to running the job, these standard modules provide the following environment variables to the job :
Item(B($LMAKE_DEBUG_KEY))    The key provided by the B(-k) or B(--key) option.
Item(B($LMAKE_DEBUG_STDIN))  The file connected as stdin to B(ldebug) when it was launched (usually a tty) if the job has its stdin redirected (in case the B(dep) rule attribute is defined).
Item(B($LMAKE_DEBUG_STDOUT)) The file connected as stdout to B(ldebug) when it was launched (usually a tty) if the job has its stdout redirected (in case the B(target) rule attribute is defined).

.SH "EXIT STATUS"
.LP
B(ldebug) exits with a status of zero if debug session could be run with no error/debug files could be generated.
If a debug session was launched, the exit code is the one of the debug session.
Else it exits with a non-zero status:
.LP
Item(B(2))  internal error, should not occur
Item(B(4))  server could not be started
Item(B(5))  internal repo state was inconsistent
Item(B(6))  repo need to be cleaned, e.g. with B(git clean -ffdx)
Item(B(7))  adequate permissions were missing, typically write access
Item(B(8))  server crashed, should not occur
Item(B(10)) some syscall failed (including debug session could not be B(exec)'ed)
Item(B(11)) bad usage : command line options and arguments coul not be parsed
Item(B(12)) bad repo version, repo need to be cleaned, e.g. with B(git clean -ffdx)

.SH EXAMPLES

.LP
V(ldebug -n my_job) : generate script and cmd files in I(LMAKE/debug/<job_id>) but do not run it.
.LP
V(ldebug -e my_job) : enter in an interactive shell session with the necessary environment to execute job, but execute nothing.

.SH FILES
.LP
Debug script files are generated in the I(LMAKE/debug) dir as I(LMAKE/debug/<job id>/script). Associated files are besides the the script files.

CommonFiles
.LP
I(lib/lmake_debug/*.py) files in the installation dir are used by default to generate debug scripts.
.LP
I(~/.bashrc) is usually executed when entering in the job environment.

Footer
