AI-generated

# ebpf-autodep.patch

- Programme noyau src/autodep/ebpf.bpf.c : 
  - sys_enter/sys_exit, getname_flags (capture des chemins) 
  - 7 hooks io_* (openat2/statx/renameat/unlinkat/mkdirat/linkat/symlinkat) 
  - task_newtask/sched_process_exit 
  - résolution du cwd en noyau (marche de dentries) 
  - auto-enregistrement du sous-arbre
- Consommateur src/autodep/ebpf.cc : chargeur 2 phases + rejeu des événements via syscall_tab (d'où la parité).
- syscall_tab : point d'entrée AutodepReplay::replay servant la mémoire du tracee depuis l'événement.
- Build : sonde CAN_AUTODEP_EBPF (clang + libbpf + bpftool + BTF), règles Makefile (vmlinux.h → .bpf.o → squelette), -lbpf limité à job_exec/lautodep (pas le serveur).

Verrous techniques résolus (tous des rejets du vérificateur eBPF, pas des permissions)

- lecture de ctx->args[i] en boucle → pointeur ctx modifié ; corrigé par lectures volatile à offset constant.
- Record::s_mutex non tenu ; le consommateur le tient comme ptrace.
- Namespace de pid WSL : les pids BPF (init) ≠ ceux de lautodep ; corrigé par auto-enregistrement noyau + bpf_get_ns_current_pid_tgid.
- Processus courts exités avant le drain : /proc/<pid>/cwd disparu ; corrigé par résolution du cwd en noyau.
- écritures à offset variable → bornage par masque + slots à offset constant.

Limites restantes (honnêtes, pour une suite)

- Chemins relatifs à un dirfd (openat avec un vrai dfd, pas AT_FDCWD) : 
  - encore résolus via /proc côté conso → perdus pour un descendant déjà exité 
  - Seul AT_FDCWD est résolu en noyau. (gcc utilise surtout AT_FDCWD, mais pas toujours.)
  - a noter : si un process a exited avant quedirfd ait ete interprete, ledit process n'a pas pu exploiter le retour de ce openat et ce n'est pas une dep. 
    idealement, il faudrait retourner une erreur et garantir que l'action ne soit pas executee pour eviter de creer une target
- 32 bits, et namespaces chroot/repo_view/kill_daemons non gérés.
- Perf : sys_enter tourne pour toute la machine (filtrage s_is_simple en noyau à ajouter).
- ebpf n'est pas dans lmake.autodeps (sinon les tests itérant les méthodes échoueraient faute de root en CI). Sélectionnable via autodep='ebpf'.

# ebpf-daemon.patch

- implement a deamon so job_exec does not need to be root

# ebpf-simple-filter.patch

- include is_simple filter in kernel
