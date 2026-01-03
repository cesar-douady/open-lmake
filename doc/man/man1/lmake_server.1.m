Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023-2026 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

Header(lmake_server,the server that actually executes B(lmake) commands)

.SH DESCRIPTION
.LP
B(lmake_server) actually executes B(lmake) commands, as well as B(ldebug), B(lmark) and B(lshow).

It takes no options nor arguments.
Supported ones are reserved for internal use.

It may be useful for performance reasons (for example if a lot of B(lshow) commands are issued) or to ensure it runs on a dedicated host.

.SH "EXIT STATUS"
.LP
B(lmake_server) exits with a status of zero if all commands were executed.
Else it exits with a non-zero status:
.LP
Item(B(1))  some targets could not be built
Item(B(2))  internal error, should not occur
Item(B(3))  I(Lmakefile.py) could not be read or contained an error
Item(B(4))  server could not be started
Item(B(5))  internal repo state was inconsistent
Item(B(6))  repo need to be cleaned, e.g. with B(git clean -ffdx)
Item(B(7))  adequate permissions were missing, typically write access
Item(B(8))  server crashed, should not occur
Item(B(10)) some syscall failed
Item(B(11)) bad usage : command line options and arguments coul not be parsed
Item(B(12)) bad repo version, repo need to be cleaned, e.g. with B(git clean -ffdx)

.SH EXAMPLES
.LP
V(lmake_server)

.SH FILES
CommonFiles

Footer
