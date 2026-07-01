#ifndef _SYSCALL_H
#define _SYSCALL_H

#include <stdint.h>
/* idt.h was historically included here but no symbol from it is
 * actually used by syscall.h, and the relative include leaks a
 * kernel-internal header into every userspace TU that pulls in
 * <unistd.h>.  Drop it. */
#include <sys/ldt.h>

// Define max syscalls
#define MAX_SYSCALLS 600   /* FreeBSD has ~576 syscalls */

// Syscall Numbers
#define SYS_EXIT    1
#define SYS_FORK    2
#define SYS_READ    3
#define SYS_WRITE   4
#define SYS_OPEN    5
#define SYS_CLOSE   6
#define SYS_WAITPID 7
#define SYS_CREAT   8
#define SYS_LINK    9
#define SYS_UNLINK  10
#define SYS_EXECVE  11
#define SYS_CHDIR   12
#define SYS_TIME    13
#define SYS_MKNOD   14
#define SYS_CHMOD   15
#define SYS_CHOWN   16   /* V7 chown — follows symlinks (POSIX) */
#define SYS_LSEEK   19
#define SYS_GETPID  20
#define SYS_MOUNT   21
#define SYS_UMOUNT  22
#define SYS_SETUID  23
#define SYS_GETUID  24
#define SYS_STIME   25
#define SYS_PTRACE  26
#define SYS_ALARM   27
#define SYS_ACCESS  33
#define SYS_SELECT  85
#define SYS_SYNC    36
#define SYS_KILL    37
#define SYS_RENAME  38
#define SYS_MKDIR   39
#define SYS_RMDIR   40
#define SYS_DUP     41
#define SYS_PIPE    42
#define SYS_TIMES   43
#define SYS_BRK     45
#define SYS_SETGID  46
#define SYS_GETGID  47
#define SYS_SIGNAL  48
#define SYS_GETEUID 49
#define SYS_GETEGID 50
#define SYS_ACCT    51
#define SYS_IOCTL   54
#define SYS_CHROOT  61
#define SYS_DUP2    63
#define SYS_FCNTL   55
#define SYS_READLINK 85
#define SYS_SYMLINK  83
#define SYS_REBOOT   88
#define SYS_MMAP    90
#define SYS_MUNMAP  91
#define SYS_TRUNCATE 92
#define SYS_FTRUNCATE 93
#define SYS_FCHMOD  94
#define SYS_FCHOWN  95
#define SYS_SETPRIORITY 96
#define SYS_FCHOWNAT  260
#define SYS_LCHOWNAT  261
#define SYS_LCHOWN    263  /* lchown — does NOT follow symlinks (BSD addition).
                            * V7 had no lchown; FreeBSD/NetBSD use 254/275 but
                            * Substrate already has SYS_PROC_ENVIRON at 254 and
                            * 275 is free elsewhere; drop it next to the at-
                            * family.  The canonical chown lives at 16 (V7). */
#define SYS_LCHMOD    264
#define SYS_FCHMODAT  297
#define SYS_GETGROUPS 80
#define SYS_SETGROUPS 81
#define SYS_WAIT4   114
#define SYS_SETITIMER 104
#define SYS_GETITIMER 105
#define SYS_GETPRIORITY 100
#define SYS_UMASK   60
#define SYS_STAT    106
#define SYS_LSTAT   107
#define SYS_FSTAT   108
#define SYS_SYSINFO 116
#define SYS_SIGRETURN 119
#define SYS_GETPPID 64
#define SYS_SIGACTION 67
#define SYS_SIGPROCMASK 126
#define SYS_SIGPENDING 73
#define SYS_SIGSUSPEND 72
#define SYS_CLONE   120
#define SYS_UNAME   122
#define SYS_MODIFY_LDT 123
#define SYS_GETDENTS 141
#define SYS_MSYNC   144
#define SYS_NANOSLEEP 162
#define SYS_GETCWD  183
#define SYS_SIGALTSTACK 186
#define SYS_POLL    209
#define SYS_FUTEX   240
#define SYS_SYSCTL  202
#define SYS_PMAP_STATS 241
#define SYS_GET_COW_STATS SYS_PMAP_STATS
#define SYS_SCHED_YIELD   298   /* sched_yield(2) — voluntary CPU yield */
#define SYS_PROC_THR_COUNT 320  /* count threads in pid */
#define SYS_PROC_THR_LIST  321  /* fill sys_thrinfo_t array; size must == count * sizeof */
#define SYS_THR_EXIT      431
#define SYS_THR_SELF      432
#define SYS_THR_KILL      433   /* FreeBSD-compat: signal a thread in current proc */
#define SYS_THR_SUSPEND   442   /* FreeBSD-compat: park current thread until thr_wake */
#define SYS_THR_WAKE      443   /* FreeBSD-compat: wake a parked thread */
#define SYS_THR_NEW       455
#define SYS_THR_JOIN      457
#define SYS_THR_SET_NAME  464   /* FreeBSD-compat: set thread name (visible in ps/gdb) */
#define SYS_THR_KILL2     481   /* FreeBSD-compat: signal a thread in another proc */
#define SYS_PROC_INFO 242
#define SYS_PROC_LIST 243
#define SYS_PROC_COUNT 244
#define SYS_CPU_COUNT 245
#define SYS_HOSTNAME 246
#define SYS_RT_SIGRETURN 247
#define SYS_PROC_THREADS 248
#define SYS_PROC_FDS     249
#define SYS_PROC_MAPS    250
#define SYS_PROC_CWD     251
#define SYS_PROC_EXE     252
#define SYS_PROC_CMDLINE 253
#define SYS_PROC_ENVIRON 254
#define SYS_VM_STATS     255
#define SYS_PROC_PERS_NAME 256 /* lookup personality name by id (was hardcoded 360 in wrapper) */
#define SYS_CLOCK_GETTIME 265
#define SYS_GETRANDOM    266
/* Detailed VM information beyond the SYS_VM_STATS summary.  Each
 * call has its own number so userland can probe support per-feature
 * via the standard ENOSYS path; multiplexing through SYS_VM_STATS
 * with a sub-op was rejected to keep the typed-syscall convention. */
#define SYS_VM_INFO      270 /* per-zone (DMA / Normal / HighMem) breakdown */
#define SYS_VM_SWAP      271 /* enumerate swap devices */
#define SYS_VM_BUFFERS   272 /* bio cache buffer stats */
#define SYS_VM_SLABS     273 /* UMA zone stats */
#define SYS_OPENAT       295
#define SYS_MKDIRAT      296
#define SYS_FSTATAT      300
#define SYS_UNLINKAT     301
#define SYS_mlock       150
#define SYS_munlock     151
#define SYS_SETSID      147
#define SYS_GETSID      310
#define SYS_SETPGID     181
#define SYS_GETPGID     182
#define SYS_GETRUSAGE   117
#define SYS_STATFS      157
#define SYS_FSTATFS     158
#define SYS_STATVFS     159
#define SYS_FSTATVFS    160
#define SYS_UMOUNT2     161 /* umount(2) with flags (MNT_FORCE etc.) */
#define SYS_PIPE2       163 /* pipe2(int[2], int flags) */
#define SYS_DUP3        164 /* dup3(int old, int new, int flags) */
#define SYS_VM86        113 /* enter virtual 8086 mode (sys_vm86) */
#define SYS_SET_GSBASE  274 /* install per-thread TLS base (sys_set_gsbase) */

/* AF_UNIX sockets layer (sys/net/af_unix.c).
 * Numbers chosen to avoid collisions with SYS_FSTATAT(300), SYS_UNLINKAT(301),
 * SYS_GETSID(310), and SYS_PROC_THR_*(320,321). */
#define SYS_SOCKET      360
#define SYS_SOCKETPAIR  361
#define SYS_BIND        362
#define SYS_LISTEN      363
#define SYS_ACCEPT      364
#define SYS_CONNECT     365
#define SYS_SEND        366
#define SYS_RECV        367
#define SYS_SENDTO      368
#define SYS_RECVFROM    369
#define SYS_SENDMSG     370
#define SYS_RECVMSG     371
#define SYS_SHUTDOWN    372
#define SYS_GETSOCKNAME 373
#define SYS_GETPEERNAME 374
#define SYS_GETSOCKOPT  375
#define SYS_SETSOCKOPT  376
#define SYS_ACCEPT4     377

#define SYS_UTIMENSAT   380
#define SYS_FUTIMENS    381

/* Extended attribute (xattr) family.  Linux-style namespaced names
 * ("user.foo", "security.selinux", etc.).  Substrate read-side is
 * fully implemented; write-side (set/remove) is ENOSYS for now.  */
#define SYS_GETXATTR    390
#define SYS_LGETXATTR   391
#define SYS_FGETXATTR   392
#define SYS_LISTXATTR   393
#define SYS_LLISTXATTR  394
#define SYS_FLISTXATTR  395
#define SYS_SETXATTR    396
#define SYS_LSETXATTR   397
#define SYS_FSETXATTR   398
#define SYS_REMOVEXATTR 399
#define SYS_LREMOVEXATTR 400
#define SYS_FREMOVEXATTR 401
#define SYS_SEMGET      402
#define SYS_SEMOP       403
#define SYS_SEMCTL      404
#define SYS_SHMGET      405
#define SYS_SHMAT       406
#define SYS_SHMDT       407
#define SYS_SHMCTL      408

/* POSIX process scheduling (sys/kern/sched_posix.c).  Kept in sync with
 * include/arch/i386/syscall.h — the build actually resolves
 * <arch/i386/syscall.h> to that userland copy, but mirror the numbers
 * here so this header stays a faithful reference. */
#define SYS_SCHED_SETPARAM          416
#define SYS_SCHED_GETPARAM          417
#define SYS_SCHED_SETSCHEDULER      418
#define SYS_SCHED_GETSCHEDULER      419
#define SYS_SCHED_GET_PRIORITY_MAX  420
#define SYS_SCHED_GET_PRIORITY_MIN  421
#define SYS_SCHED_RR_GET_INTERVAL   422

void syscall_init(void);

// GDT TLS entries
#define GDT_TLS_ENTRIES 3
#define GDT_TLS_START 6

#endif
