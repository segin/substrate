# BSD personality remediation checklist (2026-07-23)

Source: `docs/perso-audit-2026-07.md`. 26 findings.

- [ ] **P01** (critical) [sys/exec/perso/perso_openbsd.c:130] Signal syscalls route through native dispatch with NO BSD-to-native signal-numbe
- [x] **P02** (high) [sys/arch/i386/syscall.c:52] BSD errno translation table omits the entire socket/network errno range (and EIN
- [x] **P03** (high) [sys/arch/i386/syscall.c:62] EOVERFLOW is not translated to the BSD number (falls through as EPROGMISMATCH)
- [ ] **P04** (high) [sys/exec/perso/freebsd/freebsd_sig.c:227] Kernel-generated signals delivered to FreeBSD processes are not number-translate
- [ ] **P05** (high) [sys/exec/perso/freebsd/freebsd_sig.c:73] FreeBSD personality never translates BSD<->native signal numbers (kernel-generat
- [ ] **P06** (high) [sys/exec/perso/freebsd/freebsd_syscalls.h:54] setgid(2) mapped to syscall 46 instead of 181 — modern setgid unhandled, old sig
- [ ] **P07** (high) [sys/exec/perso/freebsd/freebsd_user.h:205] freebsd13_stat timestamps use 64-bit tv_sec + omit __STAT_TIME_T_EXT words, so e
- [ ] **P08** (high) [sys/exec/perso/netbsd/netbsd_sig.c:157] NetBSD signal frame built in obsolete sigcontext layout; handler receives NULL s
- [ ] **P09** (high) [sys/exec/perso/netbsd/netbsd_syscalls.h:296] ____shmctl50 wired at syscall 512 — real NetBSD number is 443, so SysV shmctl is
- [ ] **P10** (high) [sys/exec/perso/openbsd/openbsd_user.h:19] sc_trapno/sc_err placed between sc_eax and sc_eip — FreeBSD ordering used for an
- [ ] **P11** (high) [sys/exec/perso/perso_netbsd.c:405] getdents(390, __getdents30) returns Linux-layout dirent, not NetBSD struct diren
- [ ] **P12** (high) [sys/exec/perso/perso_openbsd.c:76] getrusage copies out 88-byte rusage with 16-byte timevals — overruns OpenBSD's 8
- [ ] **P13** (high) [sys/exec/perso/perso_openbsd.c:130] sa_flags passed to native sigaction untranslated — every SA_* bit is remapped to
- [x] **P14** (medium) [sys/arch/i386/syscall.c:60] EOWNERDEAD/ENOTRECOVERABLE use FreeBSD values for NetBSD (shared table cannot be
- [ ] **P15** (medium) [sys/exec/perso/compat.c:1363] fbsd_lflag termios table uses wrong FreeBSD bit values for TOSTOP and FLUSHO
- [ ] **P16** (medium) [sys/exec/perso/perso_freebsd.c:65] COMPAT10 pipe (42) routed to native sys_pipe, but FreeBSD pipe(2) takes no args 
- [ ] **P17** (medium) [sys/exec/perso/perso_netbsd.c:50] getrusage(117, compat_50) fills 88-byte native rusage into a 72-byte struct rusa
- [x] **P18** (medium) [sys/exec/perso/perso_openbsd.c:123] getppid mapped to sys_getpid — returns the caller's own PID instead of the paren
- [x] **P19** (low) [sys/arch/i386/syscall.c:62] bsd_errno_xlate omits EOVERFLOW, mistranslating it to EPROGMISMATCH
- [ ] **P20** (low) [sys/exec/perso/freebsd/freebsd_syscalls.h:66] mincore numbered 76 instead of 78
- [ ] **P21** (low) [sys/exec/perso/freebsd/freebsd_syscalls.h:131] setresuid(2) (syscall 311) has no handler; 311 is instead claimed by an unused u
- [x] **P22** (low) [sys/exec/perso/netbsd/netbsd_sig.c:307] NetBSD sigprocmask/sigpending write only 4 of the 16 bytes of the caller's sigse
- [ ] **P23** (low) [sys/exec/perso/netbsd/netbsd_syscalls.h:239] nanosleep mapped to syscall 196, which is really compat_12 getdirentries
- [ ] **P24** (low) [sys/exec/perso/netbsd/netbsd_user.h:131] netbsd_stat (struct stat12, syscalls 188-190) omits st_lspare: sizeof 92 vs 96
- [x] **P25** (low) [sys/exec/perso/perso_freebsd.c:44] COMPAT lseek (19) wired to padded 64-bit handler; syscall 19 is 32-bit no-pad lo
- [x] **P26** (low) [sys/exec/perso/perso_netbsd.c:360] compat_43 recvmsg/sendmsg (113/114) dispatched to native msghdr handler; omsghdr
