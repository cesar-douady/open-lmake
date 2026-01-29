Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023-2026 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

Title(lcodec_repair,repair a open-lmake codec table dir)
.SH SYNOPSIS
B(lcodec_repair) [I(-n)|I(--dry-run)] [I(-f)|I(--force)] [I(-r)|I(--reconstruct)] I(dir)

.SH DESCRIPTION
.LP
B(lcodec_repair) is meant to repair a codec table dir.
Its argument specifies the dir to repair.
.LP
This may be usedful because you experience incoherent behaviors and this is less agressive than setting up a fresh dir.
.LP
Also, if you want to dismiss or modify some entries, you can remove or modify any part of the table and run this command to restore a coherent state.
The structure of the codec table dir is fairly simple. Each entry consist of 2 files:
.LP
Item(decoding): a file named after B(<context>/<code>.decode) contains the associated value.
Item(encoding): a symbolic link named after B(<context>/<val_crc>.encode) points to the decoding file.
.LP
Note that managing the decoding files is much easier as you do not have to handle checksums. Using the I(-r) or I(--reconstruct) option will restore fully coherent encoding files.
.LP
When running, B(lcodec_repair) first prints what it is about to do before actually proceeding with the actions.
.LP
If I(-r) or I(--reconstruct) is not specified, only fully coherent entries are kept, that is entries with coherent decoding and encoding files.
.LP
If I(-r) or I(--reconstruct) is specified, only decoding files are considered as source of information and the encoding side is reconstructed as necessary.
When using this option, it is possible for 2 entries with different codes to have the same associated value.
In that case, only a single code is kept, which is chosen by giving preference to user given codes, then to shorter codes.

.SH "EXIT STATUS"
.LP
B(lcache_repair) exits with a status of zero if the cache was successfully repaired.
Else it exits with a non-zero status:
.LP
Item(B(2))  internal error, should not occur
Item(B(7))  adequate permissions were missing, typically write access
Item(B(10)) some syscall failed
Item(B(11)) bad usage : command line options and arguments coul not be parsed
Item(B(12)) bad cache version, cache needs to be cleaned

.SH OPTIONS
.LP
Item(B(-n),B(--dry-run))     Report actions to carry out but do not actually perform them.
Item(B(-f),B(--force))       Dont ask for user confirmation before performing the actions.
Item(B(-r),B(--reconstruct)) Reconstruct encoding files from decoding files.

ClientGeneralities()

.SH EXAMPLES
.LP
V(lcodec_repair /path/to/codec_table)

.SH FILES
CommonFiles

Footer
