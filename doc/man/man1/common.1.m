Comment(
	This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
	Copyright (c) 2023-2025 Doliam
	This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
	This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
)

define(`OpenLmake',`I(open-lmake)')

define(`Title',`
	define(`Name',``$1'')
	.TH translit(Name,a-z,A-Z) 1 "System(`date "+%d %B %Y"')" "Doliam" "User Commands"
	.SH NAME
	Name - `$2'
')

define(`Synopsys',`
	.SH SYNOPSIS
	B(Name) [I(OPTION)]... [I(FILE)]...
')

define(`Header',`
	Title(`$1',`$2')
	Synopsys
')

define(`ClientGeneralities',`
	.LP
	B(Name) manages a fully coherent dir called a repo.
	When it starts, it first determines the root of the repo (cf. B(FILES) below).
	.LP
	Arguments and reports are systematically localized to the current working dir.
	For example, if you launch B(Name b) from dir B(a) in your repo, the argument is file I(a/b) from the root of the repo
	and reports containing file names (initially seen from the root of the repo) will be shown relative the the current working dir.
	ifelse(`$1',color,`
		.LP
		If launched from a terminal, output is colored.
		Colors are different depending on whether terminal is normal (black on white) or reverse (white on black) video.
		These colors can be configured.
		The colors bears a semantic:
		Bullet Green means success.
		Bullet Orange means possible error, depending on future (if error is confirmed, it will be repeated in red).
		Bullet Red means error.
		Bullet Magenta means warning.
		Bullet Blue means notes.
		Bullet Gray means information of secondary importance.
		Bullet Uncolored means general output.  In some occasion, it may be colored by user code (e.g. gcc generates colored error messages).
		.LP
		If B($LMAKE_VIDEO) is defined, it is processed as if provided to the B(--video) option.
	')
')

define(`ClientOptions',`
	.SH COMMON OPTIONS
	.LP
	These options are common to all tools of the OpenLmake set of utilities :
	Item(B(--version))
	Print version and exit.
	Version is in the form "year.month.tag (key)" where "year.month" forms the major version, "tag" the minor version and "key" is indicates the format of the persistent information.
	Item(B(-h),B(--help))
	Print a short help and exit. It is composed of:
	.RS
	Bullet The command line synoptic.
	Bullet Version as described above.
	Bullet A line for each supported option with its short name, long name, whether it has an argument and a short explanation.
	.RE
	ifelse(`$1',job,`
		Item(B(-J),B(--job))
		Passed arguments are interpreted as job names rather than as file names.
		Job names are the names that appear, for example, on start and done lines when B(lmake) executes a job.
		Item(B(-R) I(rule),B(--rule)=I(rule))
		When the I(--job) option is used, this options allows the specification of a rule, given by its name.
		This is necessary when the job name is ambiguous as several rules may lead to the same job name.
	')
	Item(B(-q),B(--quiet))
	Do not generate user oriented messages.
	Strictly generate what is asked.
	This is practical if output is meant for automatic processing.
	Item(B(-S),B(--sync))
	Ensure server is launched (i.e. do not connect to an existing server) and wait for its termination.
	This is exceptionally useful in scripts that modify I(Lmakefile.py).
	Item(B(-v),B(--verbose))
	Generate more prolix output.
	ifelse(`$2',color,`
		Item(B(-V) I(mode),B(--video)=I(mode))
		Explicitly ask for a video mode instead of interrogating connected terminal.
		If mode starts with B(n) or B(N), normal video (black on white) is assumed.
		If it starts with B(r) or B(R), reverse video (white on black) is assumed.
		Else output is not colorized.
		video mode has an impact on generated colors as nice looking colors are not the same in each case.
	')
')

define(`SpecificOptions',`
	.SH SPECIFIC OPTIONS
	.LP
	These options are specific to B(Name) :
')

define(`SubCommands',`
	.SH SUB-COMMANDS
	.LP
	A single sub-command must be provided :
')
define(`CommonFiles',`
	.LP
	The files I(Lmakefile.py) or I(Lmakefile/__init__.py) are searched in the current dir and in parent dirs.
	If a single one is found, this determines the root of the repo.
	If several are found, the existence of an I(LMAKE) dir is checked.
	If a single one is found, this determines the root of the repo.
	In other cases, B(Name) will not start.
')

define(`SeeAlsoSection',`
	.SH "SEE ALSO"
	.LP
	ifelse(Name,lautodep,        ,`C(lautodep),'                      )
	ifelse(Name,lcheck_deps,     ,`C(lcheck_deps),'                   )
	ifelse(Name,lcollect,        ,`C(lcollect),'                      )
	ifelse(Name,ldebug,          ,`C(ldebug),'                        )
	ifelse(Name,ldecode,         ,`C(ldecode),'                       )
	ifelse(Name,ldepend,         ,`C(ldepend),'                       )
	ifelse(Name,ldircache_repair,,`C(ldircache_repair),'              )
	ifelse(Name,lencode,         ,`C(lencode),'                       )
	ifelse(Name,lforget,         ,`C(lforget),'                       )
	ifelse(Name,lmake,           ,`C(lmake),'                         )
	ifelse(Name,lmark,           ,`C(lmark),'                         )
	ifelse(Name,lrepair,         ,`C(lrepair),'                       )
	ifelse(Name,lrun_cc,         ,`C(lrun_cc),'                       )
	ifelse(Name,lshow,           ,`C(lshow),'                         )
	ifelse(Name,ltarget,         ,`C(ltarget)ifelse(Name,xxhsum,,`,')')
	ifelse(Name,xxhsum,          ,`C(xxhsum)'                         )
	.LP
	The python module B(lmake).
	.LP
	The full OpenLmake documentation in I(<open-lmake-installation-dir>/docs/index.html).
')

define(`Copyright',`
	.SH "COPYRIGHT"
	`Copyright' `\(co' 2023-2025, Doliam.
	This file is part of OpenLmake.
	.LP
	OpenLmake is free software; you can redistribute it and/or modify it under the
	terms of the GNU General Public License as published by the Free Software
	Foundation, version 3 of the License.
	.LP
	OpenLmake is distributed in the hope that it will be useful, but WITHOUT ANY
	WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
	A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
	.LP
	You should have received a copy of the GNU General Public License along with
	this program.  If not, see
	.IR http://www.gnu.org/licenses/ .
')

define(`Footer',`
	SeeAlsoSection
	Copyright
')
