Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023-2025 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

Header(lcache_server,the server that actually executes requests to daemon-based caches)

.SH DESCRIPTION
.LP
B(lcache_server) actually executes requests to daemon-based caches.

It takes no options and a single argument which is the dir containing the cached data and meta-data.
Supported options are reserved for internal use.

It may be useful to run it as a daemon, for exemple to ensure it runs on a dedicated host.

.SH "EXIT STATUS"
.LP
B(lmake_server) exits with a status of zero if all commands were executed.
Else it exits with a non-zero status:
.LP
Item(B(2))  internal error, should not occur
Item(B(3))  I(LMAKE/config.py) could not be read or contained an error
Item(B(4))  server could not be started
Item(B(5))  internal repo state was inconsistent
Item(B(7))  adequate permissions were missing, typically write access
Item(B(8))  server crashed, should not occur
Item(B(10)) some syscall failed
Item(B(11)) bad usage : command line options and arguments coul not be parsed
Item(B(12)) bad cache version, dir need to be cleaned and reinitialized

.SH EXAMPLES
.LP
V(lcache_server)

.SH FILES
CommonFiles

Footer
