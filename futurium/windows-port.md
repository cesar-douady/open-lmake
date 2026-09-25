AI-generated

# Port d'open-lmake sur Windows

## Contexte

Open-lmake ne tourne aujourd'hui que sous Linux, et pas par accident : `Makefile:14-16` refuse
explicitement de compiler ailleurs, et la fonctionnalité centrale du produit — la détection
automatique de dépendances par espionnage des accès fichiers — repose sur quatre mécanismes
strictement Linux : `LD_AUDIT`, `LD_PRELOAD`, `ptrace`+seccomp-BPF et seccomp user-notify.
L'objectif est de faire tourner open-lmake nativement sous Windows en conservant la sémantique de
correction (deps découvertes, état stable, early cutoff).

Décisions cadrantes prises avant planification :

| Sujet | Décision |
|---|---|
| Chaîne de compilation | **MinGW-w64 sous MSYS2** — binaires PE natifs, gcc C++20, `Makefile` + `_bin/sys_config` (bash) conservés |
| Périmètre du 1er jalon | **Cœur seulement** — serveur, store, backend local, autodep, règles shell/python. Hors périmètre : namespaces, `chroot_dir`, `repo_view`, `tmp_view`, `views`, backends slurm et SGE |
| Recettes shell | **`sh` POSIX embarqué** — les `Lmakefile.py` existants continuent de fonctionner |
| Privilèges autodep | **Mode utilisateur strict** — injection DLL + hooks `ntdll`, aucun droit admin, aucun driver |

## État des lieux

Quatre constats rendent ce port plus abordable qu'il n'y paraît.

**1. La surface système est très concentrée.** Sur ~49 000 lignes de C++, dix fichiers portent ~85 %
des appels POSIX : `autodep/syscall_tab.cc`, `utils.cc`, `process.cc`, `autodep/ld_common.x.cc`,
`rpc_job.cc`, `autodep/ptrace_seccomp.cc`, `disk.cc`, `utils.hh`, `fd.{hh,cc}`,
`autodep/record.cc`. Hors `src/autodep/`, les en-têtes Linux-only se réduisent à une quinzaine :
`sys/{epoll,eventfd,signalfd,inotify,sendfile,vfs,xattr,mount,single_threaded}.h`, `linux/magic.h`,
`linux/posix_acl*.h`, `link.h`, `netinet/tcp.h`, `endian.h`. Tout le reste — le store, l'algorithme
(`node.cc`/`job.cc`/`req.cc`), la sérialisation, le hash, les regex — est de la logique pure.

**2. Le mécanisme d'abstraction existe déjà.** `_bin/sys_config` (741 lignes de bash) sonde le
système et émet `sys_config.h` avec une trentaine de macros `HAS_*`/`CAN_*`. L'enum `AutodepMethod`
(`src/rpc_job.hh:25-44`) est **déjà conditionnel** sur ces macros et `Makefile:104` exclut déjà
`ld_audit.cc` du build quand il n'est pas supporté. Ajouter une méthode d'autodep optionnelle est un
geste idiomatique ici, pas une greffe. Le marqueur `PER_AUTODEP_METHOD` recense les `switch` à
mettre à jour.

**3. Il y a un précédent exploitable.** `museum/darwin_diff` (3669 lignes, 44 fichiers) est une
tentative de port macOS abandonnée. Elle n'est pas réutilisable telle quelle (antérieure au
renommage `src/lmakeserver/` → `src/lmake_server/`) mais elle **donne la carte des coutures**,
validée empiriquement : elle introduit `OS`/`IS_LINUX`/`IS_DARWIN`, `HAS_PTRACE`, `HAS_NAMESPACES`,
`HAS_EPOLL`, `HAS_KQUEUE`, `HAS_EVENTFD`, `HAS_SENDFILE`, `HAS_MREMAP`, et touche exactement
`disk.{cc,hh}`, `fd.{cc,hh}`, `process.cc`, `time.hh`, `trace.cc`, `utils.{cc,hh}`,
`non_portable.{cc,hh}`, `rpc_job.{cc,hh}` + `src/autodep/*`.

**4. Le format du store est sauf.** Contrairement à ce qu'on pourrait craindre, l'arbre préfixe
découpé sur `/` (`lmake_server/store.cc:73`) ne stocke que des noms **relatifs au repo** —
`node.cc:96`, `:331`, `:415`, `:440` l'assertent tous (`SWEAR(is_lcl(name_))`), et `node.cc:415`
marque explicitement non-constructible tout nom non local. Les chemins absolus n'apparaissent que
pour les dirs sources externes. Le travail sur les chemins se concentre donc dans l'algèbre de
`src/disk.hh` et à la frontière Win32, **pas dans le format sur disque**.

Trois bonnes surprises complémentaires :

- **Le RPC est déjà en TCP/IPv4** (`src/fd.cc:173-268`), pas en socket Unix : Winsock suffit, rien à
  réinventer côté passage de descripteurs.
- **Les threads sont du C++20 pur** — `jthread`, `stop_token`, `condition_variable_any`, `latch`,
  `binary_semaphore`. Aucun `pthread_create`. Entièrement portable.
- **Le code est quasi exempt d'extensions GCC** (2 `__builtin_*`) et tous les en-têtes POSIX sont
  centralisés dans **un seul fichier**, `src/std.hh` : c'est le point d'ancrage unique.

Deux anomalies relevées en passant, indépendantes du port : `src/rpc_job.cc:1195` teste
`#if HAS_SECCOMP`, macro définie nulle part (`sys_config` émet `CAN_AUTODEP_SECCOMP`) — code mort ;
et `process.cc:145-147` contient un `close_range` commenté.

## Correspondance Linux → Windows

### Autodep — le cœur du sujet

| Linux | Windows |
|---|---|
| `LD_PRELOAD`/`LD_AUDIT` : interposition de symboles par `ld.so` | **Injection d'une DLL** + **hooks en ligne sur les stubs `ntdll`** : `NtCreateFile`, `NtOpenFile`, `NtQueryAttributesFile`, `NtQueryFullAttributesFile`, `NtQueryDirectoryFileEx`, `NtSetInformationFile` (rename / delete-disposition / link), `NtCreateSection`, `NtCreateUserProcess`, `RtlSetCurrentDirectory_U` |
| `dlsym(RTLD_NEXT,…)` | Trampoline vers le prologue original. **MinHook** (BSD-2, compatible GPL-v3, se compile sous MinGW). Detours est MIT mais ne se construit officiellement qu'avec MSVC |
| Héritage de `LD_PRELOAD` à travers `fork`/`exec` | Hook de `NtCreateUserProcess`/`CreateProcessW` : forcer `CREATE_SUSPENDED`, injecter, puis reprendre si l'appelant ne l'avait pas demandé suspendu |
| `ptrace` / seccomp user-notify | **Abandonnés** — pas d'équivalent en mode utilisateur. (`ProcessInstrumentationCallback` est l'analogue de `PR_SET_SYSCALL_USER_DISPATCH`, non documenté et dépendant de l'architecture : même verdict que `TO_DO:284-287`) |
| `src/autodep/elf.hh` — rejoue la recherche de `ld.so` (`DT_NEEDED`, `$ORIGIN`, `RPATH`) pour capturer les chemins *essayés et absents* | **Rien à écrire.** Le chargeur Windows vit dans `ntdll` et passe lui-même par `NtOpenFile` : ses sondages sont visibles gratuitement par nos hooks. Simplification majeure |
| Backdoor par `readlinkat(MagicFd,"…/backdoor/<cmd>/<args>")` (`autodep/backdoor.hh`) | **Appel direct de fonction** : `ldepend`, `ltarget`, `clmake` tournent dans des process eux-mêmes injectés → `GetModuleHandleW` + `GetProcAddress`. Plus simple et plus robuste que le détournement de `readlink`. Le repli « pas d'autodep actif » existe déjà (`backdoor.hh:58-61`) |
| Canal rapide : FIFO `mkfifo` + atomicité `PIPE_BUF` | **Named pipe** `\\.\pipe\lmake-<id>` en `PIPE_TYPE_MESSAGE` — l'atomicité par message est native, meilleur équivalent que le FIFO |
| Canal lent : socket TCP | Inchangé (Winsock) |
| `Record::s_is_simple()` — filtre `/usr`, `/bin`, `/proc`… avant tout verrou et toute allocation | Même rôle, nouveaux préfixes : `\??\C:\Windows`, `C:\Program Files*`, `\Device\`, `…\AppData\Local\Temp`. **Chemin critique pour la perf** : les chemins arrivent en `UNICODE_STRING` NT, la comparaison doit rester sans allocation |

Les *objets d'action* de `Record` (`Solve`, `Open`, `Read`, `Lnk`, `Rename`, `Unlnk`, `Stat`,
`Chdir`, `Exec`, `ReadDir`, `Mkdir`…, `record.hh:216-499`), la discipline des macros `HDR`/`ORIG`
(`ld_common.x.cc:344-370`) et le protocole d'écriture en deux temps (`write=Maybe` puis `Confirm`,
`record.cc:222-228`) sont **réutilisables tels quels**. Ce sont les ~112 entrées de
`ENUMERATE_LIBCALLS` (`syscall_tab.hh:119-198`) qui sont à remplacer par ~25-40 hooks `Nt*`.

### Infrastructure système

| Linux | Windows |
|---|---|
| `Epoll<E>` = `epoll` + `signalfd` + `eventfd` + `inotify` (`fd.hh:262-414`) | **IOCP**. `add_pid` → handle de process ; `EventFd::wakeup` → `PostQueuedCompletionStatus` ; timeout → paramètre de `GetQueuedCompletionStatus` |
| `signalfd(SIGCHLD)` + la logique anti-coalescence de `wait()` (`fd.hh:377-401`) | Disparaît : un handle de process est signalé une fois, sans coalescence. **Simplification nette** |
| `SIGINT`/`SIGHUP` consommés via `signalfd` | `SetConsoleCtrlHandler` → `PostQueuedCompletionStatus` vers la même boucle |
| Handlers de crash sur `SIGSEGV`/`SIGBUS`/`SIGFPE`/`SIGILL` (`app.cc:82`) | `SetUnhandledExceptionFilter` / handler d'exception vectorisé |
| `inotify_add_watch` sur `LMAKE/server` (`process.cc:229-231`) | `ReadDirectoryChangesW` |
| `fork`/`vfork` + `dup2` + `execve` (`process.cc:107-150`) | `CreateProcessW` + `STARTUPINFOEX` + `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`. ⚠️ Le hook `pre_exec` (`process.hh:86`), utilisé pour le handshake sur fd 3 de `connect_to_server`, n'a pas d'équivalent : passer par l'héritage de handle |
| Groupes de process, `kill(-pid,SIGHUP)` (`process.cc:30-38`) | **Job object** + `TerminateJobObject`. `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` remplace avantageusement le namespace PID pour garantir qu'aucun démon ne survit au job |
| `getrusage(RUSAGE_CHILDREN)` (`job_exec.cc:368`) | `QueryInformationJobObject(JobObjectBasicAccountingInformation)` + `GetProcessMemoryInfo` |
| `sense_process` via `/proc/<pid>` ; `get_ppid` via `/proc/<pid>/status` | `OpenProcess` ; `NtQueryInformationProcess(ProcessBasicInformation)` |
| `/proc/self/exe` (`utils.cc:595`) | `GetModuleFileNameW(NULL,…)` |
| `/proc/self/fd/<n>` (`utils.cc:276,371,402`) | `GetFinalPathNameByHandleW(FILE_NAME_NORMALIZED)` — donne aussi la **casse réelle sur disque** |
| `statfs` + `linux/magic.h` → politique `FileSync` (`utils.cc:136`) | `GetVolumeInformationW` (nom du FS) |
| `dladdr1` + `link_map` + `addr2line` pour les backtraces (`utils.cc:687-711`) | Viser `HAS_STACKTRACE=1` : libstdc++ MinGW fournit `std::stacktrace` sur libbacktrace |
| `termios` pour sonder le thème du terminal (`client.cc:24-94`) | `GetConsoleMode`/`SetConsoleMode` + `ENABLE_VIRTUAL_TERMINAL_PROCESSING` |
| `umask`, `mode_t`, ACL POSIX (`utils.cc:168-207`, `disk.cc:499-517`) | Neutraliser : `get_umask()` → 0, `HAS_ACL=0` |
| `sysconf(_SC_PHYS_PAGES)`, `statvfs`, `sched_getaffinity` (`_lib/lmake/config_.src.py:14-22`) | `GlobalMemoryStatusEx` via `ctypes`, `shutil.disk_usage()`, `os.cpu_count()` |
| `/bin/bash` (constante `Bash`, `utils.hh:11`) | Chemin du `sh` embarqué, injecté par `sys_config` |

### Store mémoire-mappé

`RawFile<ThreadKey,Capacity>` (`store/raw_file.hh`) réserve tout l'espace d'adressage pire-cas en
`PROT_NONE`, puis remappe le fichier par-dessus en `MAP_FIXED` à chaque agrandissement — pour que
les pointeurs dans le store restent valides. L'équivalent Windows est **exact** :

| `raw_file.hh` | Windows |
|---|---|
| `mmap(nullptr,Capacity,PROT_NONE,MAP_NORESERVE\|MAP_ANONYMOUS)` `:41` | `VirtualAlloc2(…, MEM_RESERVE\|MEM_RESERVE_PLACEHOLDER)` |
| `ftruncate(fd,sz)` `:95` | `SetEndOfFile` + `CreateFileMappingW` |
| `mmap(base,sz,…,MAP_FIXED\|MAP_SHARED,fd,0)` `:101` | `MapViewOfFile3(…, base, …, MEM_REPLACE_PLACEHOLDER)` |
| découpage de la réservation | `VirtualFree(MEM_PRESERVE_PLACEHOLDER\|MEM_RELEASE)` |

Windows 10 1803+ requis. `PAGE_SZ` (constante de compilation issue de `getconf PAGE_SIZE`, utilisée
pour tous les arrondis de `raw_file.hh:64,71` et `trace.cc:52,71,104,126`) doit valoir **64 Kio** sur
Windows, la granularité d'allocation, et non 4 Kio.

### Sémantique de système de fichiers — les pièges

| Point | Situation Windows | Traitement |
|---|---|---|
| **Casse** | NTFS préserve la casse mais l'ignore à la comparaison. Le store indexe par chaîne exacte → deux orthographes = deux `Node`, donc incorrection | Canonicaliser via `GetFinalPathNameByHandleW`. Option avancée : casse sensible par répertoire (`FILE_CASE_SENSITIVE_INFORMATION`, droits requis) |
| **Séparateur et racine** | `is_abs()` est `file[0]=='/'` (`disk.hh:115`) ; `mk_canon`, `mk_lcl`, `mk_glb`, `lies_within` (`disk.cc:21-132`) supposent la syntaxe POSIX | Garder `/` comme séparateur **interne** (le store est sauf, cf. constat 4), étendre `is_abs` à `C:/…` et `//serveur/partage/…`, et convertir en forme `\` à la seule frontière Win32 |
| **`MAX_PATH` = 260** | Limite par défaut | Préfixe `\\?\` à la frontière (impose `\` et interdit `.`/`..`, donc canonicalisation préalable) + manifeste `longPathAware` |
| **Familles `*at()`** | `Fd::Cwd == AT_FDCWD` et chaque appel prend un `FileRef{Fd at, string file}` — modèle omniprésent | `OBJECT_ATTRIBUTES.RootDirectory` de `NtCreateFile` **est** l'équivalent natif d'`openat`. À défaut, résoudre le dirfd en chemin en amont (le code le fait déjà dans `SyncGuard`) |
| **Suppression/renommage de fichiers ouverts** | Interdit par défaut — or lmake efface ses targets avant de lancer un job | `FILE_DISPOSITION_POSIX_SEMANTICS` / `FILE_RENAME_POSIX_SEMANTICS` (Win10 1709+) |
| **Liens symboliques** | Nécessitent le mode développeur ou des droits admin | Défaut `config.link_support='none'` sous Windows — le paramètre existe déjà (`real_path.hh:25-30`). Gros allègement de `RealPath` |
| **`link()` comme verrou distribué** | `process.cc:219` crée le marqueur serveur par `link()` en s'appuyant sur `EEXIST` atomique. `utils.hh:62-69` documente que `fcntl`, `flock` et `mkdir` ont été rejetés (cassés sur NFSv4) | `CreateFileW(…, CREATE_NEW, …)` est atomique sur NTFS **et** SMB |
| **`symlink()` comme verrou** | `CodecLock` (`rpc_job_exec.cc:221-258`) encode le propriétaire dans la cible du lien et casse les verrous périmés par mtime | Même remplacement : fichier créé en `CREATE_NEW` dont le contenu porte le propriétaire |
| **Liens physiques / « uniquify »** | `rpc_job.cc:304-344` casse le partage avant écriture, identifié par `(st_dev,st_ino)` + `st_nlink` | `GetFileInformationByHandle` → `nNumberOfLinks`, `nFileIndex*`, `dwVolumeSerialNumber` (ou `FILE_ID_INFO`) ; `CreateHardLinkW` |
| **mtime nanoseconde** | `Ddate` lit `st_mtim` et **écrase les 3 bits de poids faible avec le `FileTag`** (`time.hh:285`) ; `FileSig` hache `(mtime,size)` | `FILETIME` est à 100 ns : le procédé tient, mais la granularité réelle de mise à jour NTFS est plus grossière. Atténué par le fait que l'analyse est *fondée sur le contenu* (CRC), pas sur la date |
| **cwd par lecteur** | Windows maintient un cwd par lettre de lecteur | À modéliser dans le `RealPath` Windows |
| **Noms courts 8.3, jonctions, UNC** | Alias multiples pour un même fichier | Normalisation systématique |
| **Loopback aléatoire** | `s_random_loopback()` (`fd.cc:88`) tire une adresse dans `127.0.0.0/8` pour élargir l'espace de ports. **Sous Windows, seul `127.0.0.1` est lié par défaut** | Forcer `LoopbackAddr` |
| **Plage de ports éphémères** | Lue dans `/proc/sys/net/ipv4/ip_local_port_range` (`fd.cc:79`) | Valeur par défaut Windows 49152-65535, ou lecture du registre |
| **`TCP_QUICKACK`** | Linux only (`fd.cc:236,262`) | Ignorer |
| **`renameat2(RENAME_EXCHANGE)`** | Pas d'équivalent | Confiné à l'autodep ; `disk.cc:394` n'utilise que `renameat` |

## Découpage par difficulté

### Trivial — recompile quasiment tel quel (~50 % des lignes)

`src/store/*` hors `raw_file.hh`, `src/lmake_server/{node,job,req,rule,job_data,cmd,config}.cc`,
`serialize.hh`, `msg.hh`, `hash.{cc,hh}` (xxHash déjà vendu dans `ext/`), `re.{cc,hh}`, `enum.hh`,
`time.{cc,hh}`, `rpc_client.hh`, `zfd.{cc,hh}` (moins les chemins rapides `sendfile`), les commandes
`lshow`/`lforget`/`lmark`/`lcollect`/`xxhsum`, et le noyau de `src/py.cc`
(`PyConfig_InitIsolatedConfig` est portable). Le RPC TCP ne demande que l'initialisation Winsock et
`closesocket`. `src/version.cc` (3111 lignes) est une archive générée : à ignorer.

### Moyen — traduction mécanique mais volumineuse

- **`src/disk.{cc,hh}`** — toutes les opérations fichier vers Win32, plus l'algèbre de chemins.
  Volumineux et truffé des pièges du tableau ci-dessus.
- **`src/fd.{cc,hh}`** — `Epoll<E>` vers IOCP. La couture est **remarquablement propre** : une
  quinzaine de méthodes, 6 sites d'instanciation, et seulement 2 endroits touchent les constantes
  `EPOLLIN`/`EPOLLHUP` brutes — tous deux dans `ptrace_seccomp.cc`, qu'on abandonne.
- **`src/process.{cc,hh}`** — `Child::spawn`, `AutoServer<T>::event_loop`, `connect_to_server`.
- **`src/store/raw_file.hh`** — ~60 lignes, API placeholders.
- **`src/trace.cc`** — trace circulaire sur mmap (même idiome que `raw_file`).
- **`src/utils.{cc,hh}`** — `Fd`/`AcFd`, `SyncGuard`, environnement, backtraces.
- **`src/lmake_server/backends/local.cc`** — lancement, attente, kill, `RLIMIT_NPROC` (à supprimer).
- **`_bin/sys_config`** — élaguer ~15 sondes sans objet (`LD_SO_LIB`, `STD_LIBRARY_PATH`,
  `USE_LIBC_START_MAIN`, `LIBC_MAP_STAT`, `MAX_PID`, `HAS_LD_AUDIT`, ptrace/seccomp/pidfd/
  `close_range`/`renameat2`/ACL…), en ajouter quelques-unes (version de Windows, `VirtualAlloc2`,
  `sh` embarqué), remplacer `source /etc/os-release` par une signature Windows — ce qui entraîne
  aussi le contrôle de démarrage de `lmake_server/main.cc:112-136`.
- **`_lib/lmake/config_.src.py`** et `_lib/lmake/sources.src.py` (chemin de `git`).

### Difficile — conception neuve, c'est là que se logeront les bugs

1. **La DLL d'interception** (`src/autodep/win_hooks.cc`, à créer). ~25-40 hooks `Nt*` en
   remplacement des ~112 entrées `ENUMERATE_LIBCALLS`. La logique de classification de `Record` est
   réutilisable, pas les entrées.
2. **L'injection.** `CreateProcess(CREATE_SUSPENDED)` + `VirtualAllocEx` +
   `CreateRemoteThread(LoadLibraryW)` est simple, mais laisse passer les imports statiques de l'exe,
   chargés avant l'installation des hooks. Non bloquant au départ (ce sont presque toujours des DLL
   système, que `s_is_simple` filtrerait de toute façon), à durcir par réécriture de la table
   d'imports du PE distant — la technique de `DetourCreateProcessWithDllEx`. Prévoir la variante
   WOW64 : un injecteur et une DLL par architecture, ce qui recycle la machinerie `HAS_32`.
3. **`RealPath` pour Windows** (`src/real_path.cc`). Points de reparse, jonctions, cwd par lecteur,
   `\\?\`, UNC, 8.3, insensibilité à la casse. Il faut y rétablir l'invariant « open-lmake vit dans
   le monde physique » de `doc/src/autodep.md`. **Module le plus risqué du port.**
4. **La canonicalisation des chemins**, parce qu'elle détermine l'**identité des nœuds**, donc la
   correction de tout l'algorithme et pas seulement de l'autodep.
5. **`s_is_simple` version Windows** (`record.cc:44-123`) — filtre en chemin critique.

### Hors périmètre, à neutraliser proprement

`autodep/ptrace_seccomp.{cc,hh}`, `ld_audit.cc`, `ld_preload*.cc`, `ld_common.x.cc`, `elf.hh`,
`non_portable.{cc,hh}` (registres ptrace par architecture), `JobSpace::enter` et tout le bloc
namespaces/mount/chroot/overlay de `rpc_job.cc:448-859`, les backends `slurm.cc`/`sge.cc` et
`ext/slurm/`, la génération de `world_32.h` (`Makefile:549-563`), `HAS_ACL`, le `syscall(SYS_getdents64)`
anti-cache Lustre (`utils.cc:404,412`).

⚠️ **Point de versioning** : l'enum `Comment` (`rpc_job_common.hh:167-272`) énumère chaque libcall
interceptée et vit à l'intérieur d'un bloc `START_OF_VERSIONING CACHE JOB REPO`. Y ajouter les
entrées Windows change le hash de version et invalide repos et caches. Ajouter en fin d'énumération
et assumer un bump, ou cloisonner par `#if`.

## Plan de travail

**M0 — Infrastructure de build.** ✅ *fait* (branche `windows-port`). Verrou `Makefile:14-16` remplacé
par une détection `HOST_OS`/`IS_WINDOWS` ; `_bin/sys_config` détecte l'OS, substitue une signature
`windows/<build>` à `/etc/os-release`, met `PAGE_SZ` à 64 Kio (granularité d'allocation) et émet
`IS_WINDOWS` dans `sys_config.h` ; `src/std.hh` scindé en `src/sys_posix.hh` (bloc POSIX repris à
l'identique) et `src/sys_win.hh` (en-têtes Win32 + les rares graphies POSIX que MinGW ne fournit
pas : `uint`/`ushort`/`ulong`/`in_addr_t`/`in_port_t`, `O_CLOEXEC`→`O_NOINHERIT`, `O_DIRECTORY`,
`O_NOFOLLOW`, `O_PATH`, `O_TMPFILE`, `O_NONBLOCK`, `AT_FDCWD`).
Chemin Linux vérifié inchangé : jeu d'`#include` identique au bit près.
*Correction de périmètre* : `bin/xxhsum` ne peut pas servir de livrable M0 — il lie
`LMAKE_BASIC_OBJS` (`Makefile:298`), c'est-à-dire `disk`, `fd`, `process`, `trace`, `utils`, soit
toute la couche M1. **Aucun binaire n'est atteignable sous cette couche** : M0 est du pur
échafaudage, le premier exécutable arrive en fin de M1.

**M1 — Couche système.** `disk`, `fd` (IOCP), `process` (CreateProcess + job objects),
`store/raw_file` (placeholders), `trace`. *Livrable : `src/store/unit_test` passe ; `_bin/lmake_dump`
lit un store.*

**M2 — Serveur sans exécution.** `lmake_server` démarre, lit un `Lmakefile.py`, répond à
`lmake`/`lshow`. Partir avec `HAS_PY_DYN=0` pour éviter `clmake.pyd` : le repli pur-python
`_lib/lmake/py_clmake.src.py`, qui appelle `ldepend`/`ltarget` en sous-process, existe déjà.
*Livrable : `lshow -i` sur un repo vide.*

**M3 — Exécution sans autodep.** Backend local, `job_exec`, `AutodepMethod::None`, `sh` embarqué,
job objects. *Livrable : un `Lmakefile.py` à deps statiques se construit.*

**M4 — Autodep, chemin nominal.** DLL + MinHook + injection + hooks `NtCreateFile`/`NtOpenFile`/
`NtQueryAttributesFile`, named pipe rapide, `RealPath` Windows, `s_is_simple` Windows.
*Livrable : `examples/hello_world.dir` passe.*

**M5 — Autodep complet.** Propagation aux petits-enfants, rename/unlink/link/readdir/exec, backdoor
par `GetProcAddress`, `ldepend`/`ltarget`/`lcheck_deps`/`ldecode`/`lencode`. *Livrable :
`examples/cc.dir` passe.*

**M6 — Suite de tests.** Trier les 227 tests de `unit_tests/` : le mécanisme `skipped` existe déjà
(`Makefile:839`, 28 conditions de saut en place). Marquer sautés `namespaces`, `overlay`, `chroot`,
`ptrace`, `io_uring`, `jemalloc`, `vfork*`, `ld_library_path`, `exe_32bits`, `slurm`, `sge`. Adapter
le harnais `_lib/ut.py` (il utilise `os.mkfifo`, `os.symlink`, `os.setpgrp`, `os.killpg`) et
`_bin/ut_launch` (`preexec_fn=os.setpgrp`).

**M7 — Durcissement.** Réécriture de la table d'imports, WOW64/32 bits, casse, chemins longs,
`clmake.pyd`, empaquetage.

## Vérification

- `make` à la racine construit tout et lance les tests (`Makefile:817-859`) ; chaque test est
  auto-portant et relançable seul.
- `src/store/unit_test` et `src/store/big_test.py` (2 000 000 de nœuds) valident la couche store et
  le remapping sous charge — c'est le test qui exercera le mieux l'API placeholders.
- `examples/hello_world.dir/run` puis `examples/cc.dir/run` : les deux scénarios de bout en bout du
  README.
- Non-régression Linux : ce port ne doit rien changer au comportement Linux. Le cloisonnement passe
  par les macros de `sys_config.h`, jamais par modification de la logique commune. Faire tourner la
  CI docker existante (16 images) à chaque jalon.
- Deux tests Windows à écrire : (a) un job dont l'exécutable importe statiquement une DLL **située
  dans le repo**, pour vérifier la fenêtre d'injection ; (b) un job accédant au même fichier sous
  deux orthographes de casse, pour vérifier la canonicalisation.

## Risques

1. **Le remapping du store n'est pas atomique sous Windows.** `mmap(MAP_FIXED)` remplace une
   projection en une opération ; `MapViewOfFile3` sur placeholder impose de démapper d'abord. Si un
   autre thread lit pendant l'agrandissement, il touche de la mémoire non mappée. Le code a déjà un
   `chk_thread()` (`raw_file.hh:78`) qui contraint les écritures à un seul thread, mais pas les
   lectures. Repli : projeter par tranches de taille fixe, jamais remappées, seulement ajoutées.
2. **Fenêtre d'injection** — détaillée en « Difficile » ci-dessus et couverte par le M7.
3. **CPython MinGW ↔ CPython MSVC.** `clmake.pyd` compilé MinGW chargé dans un CPython officiel
   fonctionne tant qu'aucun objet de CRT ne traverse la frontière. Le projet n'utilise
   délibérément pas les flux C++ (`doc/developers.md`, section Streams), ce qui joue en notre
   faveur. Mitigé de toute façon par `HAS_PY_DYN=0` jusqu'au M7.
4. **Les antivirus** traitent l'injection de DLL et le patch de `ntdll` comme un comportement
   suspect. À tester tôt avec Defender actif : c'est un risque d'adoption autant que technique.
5. **Performance.** Sous Linux, le filtrage précoce par nom est ce qui rend les méthodes `ld`
   rapides. Il faut conserver cette propriété sur les `UNICODE_STRING` NT, sans allocation.
6. **Granularité des dates NTFS.** `FileSig` repose sur `(mtime,size)`. Une granularité plus
   grossière augmente le risque de faux « inchangé ». À surveiller ; l'analyse par CRC du contenu
   limite l'exposition.
