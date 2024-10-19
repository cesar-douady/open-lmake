
define(`CommonOptions',`
.LP
Options common to all tools of the I(open-lmake) set of utilities :
.TP
B(-h), B(--help)
Print a short help and exit.
.TP
B(-J), B(--job)
Passed arguments are interpreted as job names rather than as file names.
Job names are the names that appear, for example, on start and done lines when B(lmake) executes a job.
.TP
B(-R) I(rule), B(--rule)=I(rule)
When the B(-J) option is used, this options allows the specification of a rule, given by its name.
This is necessary when the job name is ambiguous as several rules may lead to the same job name.
.TP
B(-q), B(--quiet)
Do not generate user oriented messages.
Strictly generate what is asked.
This is practical if output is meant for automatic processing.
.TP
B(-S), B(--sync)
Ensure server is launched (i.e. do not connect to an existing server) and wait for its termination.
.TP
B(-V) I(mode), B(--video)=I(mode)
Explicitly ask for a video mode instead of interrogating connected terminal.
If mode starts with B(n) or B(N), normal video (black on white) is assumed.
If it starts with B(r) or B(R), reverse video (white on black) is assumed.
Else output is not colorized.
video mode has an impact on generated colors as nice looking colors are not the same in each case.
')
