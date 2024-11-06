Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

Title(xxhsum,compute a fast *non crypto-robust* message digest)
.SH SYNOPSYS
B(xxhsum) I(file)

.SH DESCRIPTION
.LP
XXH is a very high performance, high quality checksum generation algorithm internally used by OpenLmake to find out whether file was actually modified or not when its date is changed.
.LP
XXH is B(not) crypto-robust.
This means that you can defeat it if you write a code specially aimed at this purpose, but not otherwise, by chance.
.LP
The version used in OpenLmake is 56/64 bits : Checksums are computed on 64 bits but when comparing 2 checksums, if they match on 56 (lsb) bits but not on the full 64 bits,
OpenLmake will consider we enter into a I(danger zone) and will stop and report the problem.
Theoretical computation gives that you can generate thousands of files per second for thousands of year before entering the I(danger zone).
.LP
B(xxhsum) allow you to generate the checksum of a file, as distinguished by OpenLmake.
.LP
Because OpenLmake handles symbolic links as themselves and not as the file they point to (i.e. OpenLmake works in the physical world), B(xxhsum) does not follow symbolic links.
The generated checksums are put in different spaces whether they are regular files or symbolic links.
.LP
Also, the execute permission bit is used to compute the checksum of regular files.
OpenLmake does not handle permission bits (read and write permissions), but the execute bit is a semantic bit (possibly in addition to security) and thus is managed as file content.
.LP
Directories and other awkward files (i.e. neither a regular file or a symbolic link) are handled as if they did not exist.
.LP
A checksum is a 16-hex digit number followed by B(-R) for regular files or B(-L) for symbolic links (e.g. 1234567890123456-R).
The following special cases produce dedicated outputs :
Bullet file does not exist                         : output is B(none)
Bullet file is a non-executable regular empty file : output is B(empty-R)

.SH "EXIT STATUS"
.LP
B(xxhsum) exits with a status of zero if the checksum could be computed.
Else it exits with a non-zero status.

.SH OUTPUT
.LP
The checksum on a single line.

SeeAlsoSection

Copyright
.LP
For the xxh library :
.LP
`Copyright' `\(co' 2012-2023 Yann Collet
.LP
BSD 2-Clause License (https://www.opensource.org/licenses/bsd-license.php)
.LP
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
Bullet
Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
Bullet
Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the following disclaimer
in the documentation and/or other materials provided with the
distribution.
.LP
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
.LP
You can contact the author at:
Bullet xxHash homepage: https://www.xxhash.com
Bullet xxHash source repository: https://github.com/Cyan4973/xxHash
