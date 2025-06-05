<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Backends

Backends are in charge of actually launching jobs when the open-lmake engine has identified that it had to be run.
It is also in charge of :

- Killing jobs when the open-lmake engine has identified it had to be so.
- Scheduling jobs so as to optimize the runtime, based on some indications provided by the open-lmake engine.
- Rescheduling jobs when new scheduling indications becomes available.

A backend has to take decisions of 2 kinds:

- Is a job eligible for running ?
  From a dep perspective, the open-lmake engine guarantees it is so.
  But the job needs some resources to run and these resources may already be busy because of some other jobs already running.
- If several jobs are eligible, which one(s) to actually launch.

Each backend is autonomous in its decisions and has its own algorithm to take them.
However, generally speaking, they more or less work by following the following principles:

- For the first question, the backend maintain a pool of available resources and a job is eligible if its required resources can fit in the pool.
When launched, the required resources are subtracted from the pool and when terminated, they are returned to it.
- For the second question, each job has an associated pressure provided by the open-lmake engine and the backend actually launches the eligible job with the highest pressure.

The required resources are provided by the open-lmake engine to the backend as a `dict` which is the one of the job's rule after f-string interpretation.

The pressure is provided in the form of `float` computed as the accumulated ETE along the critical path to the final targets asked on the `lmake` command line.
To do that, future job ETE have to be estimated.
For jobs that have already run, last successful execution time is used.
When this information is not available, i.e. when the job has never run successfully, a moving average of the execution times of the jobs sharing the same rule is used as a best guess.

The backend also provides the current [ETA](eta.html) of the final targets to allow the backends from different repo to take the best collective decision.

In addition to dedicated resources, all backends manage the following 3 resources:

- `cpu` : The number of threads the job is expected to run in parallel. The backend is expected to reserve enough resources for such a number of threads to run smoothly.
- `mem` : The memory size the job is expected to need to run smoothly.
  The backend is expected to ensure that such memory is available for the job.
  Unit must be coherent with the one used in the configuration. It is MB by default.
- `tmp` : The size of necessary temporary disk space.
  By default temporary disk space is not managed, i.e. `$TMPDIR` is set (to a freshly created empty empty dir which is cleaned up after execution)
  with no size limit (other than the physical disk size) but no reservation is made in the backend.

## Resource buckets

It may be wise to quantify resources with relatively large steps for resources `mem` and `tmp`, especially if these may be computed with a formula.

The reason is linked to the way the backends select jobs.
When a backend (actually the local, SGE and slurm backends essentially work the same way) search for the next job to launch, it walks through the available jobs to
find the eligible one with the highest priority.
When doing that, only jobs with different resources need to be compared as for a given set of resources, they can be pre-ordered by priority.
As a consequence, the running time is proportional to the number of different resources.
If the `mem` and `tmp` needed space is computed from some metrics, it may be very well possible that each job has a different number, leading to a selection process
whose time is proportional to the number of waiting jobs, which can be very high (maybe millions).

To help reduce this overhead, one may want to put jobs into buckets with defined values for these resources.
This is done by rounding these resources for grouping jobs into buckets.

When job is launche, however, the exact resources are reserved. Rounding is just applied to group jobs into bucket and improve the management of the queues.

## backend conversion to local

If a backend cannot be configured because the environment does not allow it (typically missing the SGE or slurm daemons), then:

- A Warning message is emmitted at configuration time.
- Jobs supposed to start with such backends will be redirected to the local backend.
- Resources are mapped on a best-effort basis, and if a resource does not exist or is insufficient in the local backend, job is started so as to be alone on the local host.

## Local backend

The local backend launches jobs locally, on the host running the `lmake` command.
There is no cooperation between backends from different repos and the user has to ensure there is no global resource conflict.

This backend is configured by providing entries in the `lmake.config.backends.local` `dict`.
The key identifies the resource and the value is a `int` that identifies a quantity.

The local backend is used when either:

- The `backend` attribute is `'local'` (which is the default value).
- `lmake` is launched with the the `--local` option.
- The required backend is not supported or not available.

In the two latter cases, required resources are translated into local resources (best effort)
and if not possible (e.g. because a resource is not available locally or because special constraints cannot be translated), then only one such job can run at any given time.

### Configuration

The configuration provides the available resources :

- standard resoureces `cpu`, `mem` and `tmp`
- any user defined resource

Each rule whose `backend` attribute is `'local'` provides a `resources` attribute such that:

- The key identifies a resource (which must match a resource in the configuration).
- The value (possibly tailored by job through the use of the f-string syntax) is a `int` or a `str` that can be interpreted as `int`.

The variable available to the job as global variables (python case) or environment variables (shell case) contains the actual quantity of resources allocated to this job.

The local backend ensures that the sum of all the resources of the running jobs never overshoots the configured available quantity.

By default, the configuration contains the 2 generic resources: `cpu` and `mem` configured respectively as the overall number of available cpus and the overall available memory (in MB).

- `cpu` : The number of cpu as returned by `os.wched_getaffinity(0)`.
- `mem` : The physical memory size as returned by `s.sysconf('SC_PHYS_PAGES')*os.sysconf('SC_PAGE_SIZE')` in MB.

Each rule has a default `resources` attribute requiring one CPU.

## SGE backend

The SGE backend connects to a SGE daemon to schedule jobs, which allows:

- a global scheduling policy (while the local backend only sees jobs in its own repo).
- the capability to run jobs on remote hosts (while the local backend only run jobs on the local host).

### Command line option

The command line option passed with `-b` or `--backend` is ignored.

### Configuration

The configuration is composed of:

- `bin`  : The dir in which to find SGE executables such as `qsub`. This entry must be specified.
- `cell` : The cell used by the SGE daemon. This is translated into `$SGE_CELL` when SGE commands are called.
  By default, this is automatically determined by the SGE daemon.
- `cluster` : The cluster used by the SGE daemon. This is translated into `$SGE_CLUSTER` when SGE commands are called.
  By default, this is automatically determined by the SGE daemon.
- `default_prio` : the priority used to submit jobs to the SGE daemon if none is specified on the `lmake` command line.
- `n_max_queued_jobs` : open-lmake scatters jobs according to the required resources and only submit a few jobs to SGE for each set of asked resources.
  This is done to decrease the load of the SGE daemon as open-lmake might have millions of jobs to run and the typical case is that they tend to require only a small set of different resources
  (helped in this by the limited precision on CPU, memory and temporary disk space requirements).
  For each given set of resources, only the jobs with highest priorities are submitted to SGE, the other ones are retained by open-lmake so as to limit the number of waiting jobs in slurm queues
  (the number of running job is not limited, though).
  This attribute specifies the number of waiting jobs for each set of resources that open-lmake may submit to SGE.
  If too low, the schedule rate may decrease because by the time taken, when a job finishes, for open-lmake to submit a new job, slurm might have exhausted its waiting queue.
  If too high, the schedule rate may decrase because of the slurm daemon being overloaded.
  A reasonable value probably lies in the 10-100 range.
  Default is 10.
- `repo_key` : This is a string which is add in front of open-lmake job names to make SGE job names.
  This key is meant to be a short identifier of the repo.
  By default it is the base name of the repo followed by `:`.
  Note that SGE precludes some characters and these are replaced by close looking characters (e.g. `;` instead of `:`).
- `root`         : The root dir of the SGE daemon. This is translated into `$SGE_ROOT` when SGE commands are called. This entry must be specified.
- `cpu_resource` : This is the name of a resource used to require cpu's.
  For example if specified as `cpu_r` and the rule of a job contains `resources={'cpu':2}`, this is translated into `-l cpu_r=2` on the `qsub` command line.
- `mem_resource` : This is the name of a resource used to require memory in MB.
  For example if specified as `mem_r` and the rule of a job contains `resources={'mem':'10M'}`, this is translated into `-l mem_r=10` on the `qsub` command line.
- `tmp_resource` : This is the name of a resource used to require memory temporary disk space in MB.
  For example if specified as `tmp_r` and the rule of a job contains `resources={'tmp':'100M'}`, this is translated into `-l tmp_r=100` on the `qsub` command line.

### Resources

The `resources` rule attributes is composed of :

- standard resources `cpu`, `mem` and `tmp`.
- `hard` : `qsub` options to be used after a `-hard` option.
- `soft` : `qsub` options to be used after a `-soft` option.
- any other resource passed to the SGE daemon through the `-l` `qsub` option.

## Slurm backend

The slurm backend connects to a slurm daemon to schedule jobs, which allows :

- a global scheduling policy (while the local backend only sees jobs in its own repo).
- the capability to run jobs on remote hosts (while the local backend only run jobs on the local host).

### Command line option

The only option that can be passed from command line (`-b` or `--backend`) is the priority through the `-p` options of `qsub`.

Hence, the command line option must directly contain the priority to pass to `qsub`.

### Configuration

The configuration is composed of :

- `config` : The slurm configuration file to use to contact the slurm controller. By default, `/etc/slurm/slurm.conf` is used.
- `lib_slurm` : The slurm dynamic library. If no `/` appears, `$LD_LIBRARY_PATH` (as compiled in) and system default lib dirs are searched. By default, `libslurm.so` is used.
- `n_max_queued_jobs` : open-lmake scatters jobs according to the required resources and only submit a few jobs to slurm for each set of asked resources.
  This is done to decrease the load of the slurm daemon as open-lmake might have millions of jobs to run and the typical case is that they tend require only a small set of different resources
  (helped in this by the limited precision on CPU, memory and temporary disk space requirements).
  for each given set of resources, only the jobs with highest priorities are submitted to slurm, the other ones are retained by open-lmake so as to limit the number of waiting jobs in slurm queues
  (the number of running job is not limited, though).
  This attribute specifies the number of waiting jobs for each set of resources that open-lmake may submit to slurm.
  If too low, the schedule rate may decrease because by the time taken, when a job finishes, for open-lmake to submit a new job, slurm might have exhausted its waiting queue.
  If too high, the schedule rate may decrase because of the slurm daemon being overloaded.
  A reasonable value probably lies in the 10-100 range.
  Default is 10.
- `repo_key` : This is a string which is add in front of open-lmake job names to make slurm job names.
  This key is meant to be a short identifier of the repo.
  By default it is the base name of the repo followed by `:`.
- `use_nice`:
  open-lmake has and advantage over slurm in terms of knowledge: it knows the deps, the overall jobs necessary to reach the asked target and the history of the time taken by each job.
  This allows it to anticipate the needs and know, even globally when numerous `lmake` commands run, in the same repo or on several ones, which jobs should be given which priority.
  Note that open-lmake cannot leverage the dep capability of slurm as deps are dynamic by nature:
  - new deps can appear during job execution, adding new edges to the dep graph,
  - jobs can have to rerun, so a dependent job may not be able to start when its dep is done,
  - and a job can be steady, so a dependent job may not have to run at all.

  The way it works is th following:
  - First open-lmake computes and [ETA](eta.html) for each `lmake` command. This ETA is a date, it is absolute, and can be compared between commands running in different repos.
  - Then it computes a pressure for each job. The pressure is the time necessary to reach the asked target of the `lmake` command given the run time for all intermediate jobs
    (including the considered job).
  - The subtraction of the pressure from the ETA gives a reasonable and global estimate of when it is desirable to schedule a job, and hence can be used as a priority.

  The way to communicate this information is to set for each job a nice value that represents this priority.
  Because this may interfere with other jobs submitted by other means, this mechanism is made optional,
  although it is much better than other scheduling policies based on blind guesses of the futur (such as fair-share, qos, etc.).

There are 2 additional parameters that you can set in the `PriorityParams` entry of the slurm configuration in the form of param=value, separated by `,`:

- `time_origin`: as the communicated priority is a date, we need a reference point.
  This reference point should be in the past, not too far, to be sure that generated nice values are in the range `0` - `1<<31`.
- open-lmake sometimes generates dates in the past when it wrongly estimates a very short ETA with a high pressure.
  Taking a little bit of margin of a few days is more than necessary in all practical cases.
  Default value is 2023-01-01 00:00:00.
  Date is given in the format YYYY-MM-DD HH:MM optionally followed by +/-HH:MM to adjust for time zone.
  This is mostly ISO8601 except the T between date and time replaced by a space, which is more readable and corresponds to mainstream usage.
- `nice_factor`: this is the value that the nice value increases each second. It is a floating point value.
  If too high, the the nice value may wrap too often. If too low, job scheduling precision may suffer.
  The default value is `1` which seems to be a good compromise.

Overall, you can ignore these parameters for open-lmake internal needs, the default values work fine.
They have been implemented to have means to control interactions with jobs submitted to slurm from outside open-lmake.

### Resources

The `resources` rule attributes is composed of:

- standard resources `cpu`, `mem` and `tmp`.
- `excludes` `features`, `gres`, `licence`, `nodes`, `partition`, `qos`, `reserv` : these are passed as is to the slurm daemon.
  For heterogeneous jobs, these attribute names may be followed by an index identifying the task (for example `gres0`, `gres1`).
  The absence of index is equivalent to index 0.
- any other resource passed to the slurm daemon as `licenses` if such licenses are declared in the slurm configuration, else as `gres`.

### Command line option

The command line option passed with the `-b` or `--backend` option is a space separate list of options.
The following table describes supported option, with a description when it does not correspond to the identical option of `srun`.

| Short option | Long option   | Description         |
|--------------|---------------|---------------------|
| `-c`         | cpus-per-task | cpu resource to use |
|              | mem           | mem resource to use |
|              | tmp           | tmp resource to use |
| `-C`         | constraint    |                     |
| `-x`         | exclude       |                     |
|              | gres          |                     |
| `-L`         | licenses      |                     |
| `-w`         | nodelist      |                     |
| `-p`         | partition     |                     |
| `-q`         | qos           |                     |
|              | reservation   |                     |
| `-h`         | help          | print usage         )
