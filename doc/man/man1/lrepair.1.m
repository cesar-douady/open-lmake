Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023-2025 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

Title(lrepair,repair a OpenLmake repository)
.SH SYNOPSYS
B(lrepair)

.SH DESCRIPTION
.LP
B(lrepair) is meant to repair internal OpenLmake book-keeping in case of catastrophic crash, either because of system failure or a bug in OpenLmake.
Most of the time, OpenLmake does not need resort to heavy reparation as its internal book-keeping is fairly robust, but in some rare cases, this may be necessary.
In such a case, OpenLmake will suggest the use of B(lrepair).
.LP
In case of incoherent behavior, the user may decide to run B(lrepair).
.LP
This process is pretty long, the goal being to avoid having to restart from a fresh repository, which may incur a severe cost in terms of compute power.
.LP
Once B(lrepair) is done, it generates some suggestions about what to do with the freshly repaired repository, including step back and forget about the repairation.

ClientGeneralities()

.SH FILES
CommonFiles

Footer
