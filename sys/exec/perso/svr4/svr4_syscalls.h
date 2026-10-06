/*
 * svr4_syscalls.h - UNIX System V Release 4 (i386) system call numbers.
 *
 * Calls 1 to 63 are System V's from the beginning and are numbered as
 * Xenix numbers them (exec/perso/xenix/xenix286_syscalls.h); these are the
 * ones Release 3 and Release 4 added, and the multiplexed calls' codes.
 * Reference: <sys/syscall.h> of UNIX System V/386 Release 4.0.
 */
#ifndef _SVR4_SYSCALLS_H
#define _SVR4_SYSCALLS_H

#define SVR4_SYS_open         5
#define SVR4_SYS_brk          17
#define SVR4_SYS_kill         37
#define SVR4_SYS_pgrpsys      39
#define SVR4_SYS_signal       48
#define SVR4_SYS_sysi86       50
#define SVR4_SYS_ioctl        54
#define SVR4_SYS_fsync        58
#define SVR4_SYS_fcntl        62
#define SVR4_SYS_rmdir        79
#define SVR4_SYS_mkdir        80
#define SVR4_SYS_getdents     81
#define SVR4_SYS_getmsg       85
#define SVR4_SYS_putmsg       86
#define SVR4_SYS_poll         87
#define SVR4_SYS_lstat        88
#define SVR4_SYS_symlink      89
#define SVR4_SYS_readlink     90
#define SVR4_SYS_setgroups    91
#define SVR4_SYS_getgroups    92
#define SVR4_SYS_fchmod       93
#define SVR4_SYS_fchown       94
#define SVR4_SYS_sigprocmask  95
#define SVR4_SYS_sigsuspend   96
#define SVR4_SYS_sigaltstack  97
#define SVR4_SYS_sigaction    98
#define SVR4_SYS_sigpending   99
#define SVR4_SYS_context      100
#define SVR4_SYS_waitsys      107
#define SVR4_SYS_hrtsys       109
#define SVR4_SYS_mmap         115
#define SVR4_SYS_mprotect     116
#define SVR4_SYS_munmap       117
#define SVR4_SYS_vfork        119
#define SVR4_SYS_fchdir       120
#define SVR4_SYS_readv        121
#define SVR4_SYS_writev       122
#define SVR4_SYS_xstat        123
#define SVR4_SYS_lxstat       124
#define SVR4_SYS_fxstat       125
#define SVR4_SYS_xmknod       126
#define SVR4_SYS_setrlimit    128
#define SVR4_SYS_getrlimit    129
#define SVR4_SYS_lchown       130
#define SVR4_SYS_memcntl      131
#define SVR4_SYS_rename       134
#define SVR4_SYS_uname        135
#define SVR4_SYS_setegid      136
#define SVR4_SYS_sysconfig    137
#define SVR4_SYS_systeminfo   139
#define SVR4_SYS_seteuid      141

/* sysi86(2) */
#define SVR4_SI86FPHW         40
#define SVR4_FP_387           3

/* pgrpsys(2) */
/* hrtsys(2): the function, hrtcntl()'s commands, and the standard clock. */
#define SVR4_HRT_CNTL         0
#define SVR4_HRT_GETRES       0
#define SVR4_HRT_TOFD         1
#define SVR4_CLK_STD          1

#define SVR4_PGRP_getpgrp     0
#define SVR4_PGRP_setpgrp     1
#define SVR4_PGRP_getsid      2
#define SVR4_PGRP_setsid      3
#define SVR4_PGRP_getpgid     4
#define SVR4_PGRP_setpgid     5

/* open(2) and fcntl(F_SETFL) flags */
#define SVR4_O_NDELAY         0x0004
#define SVR4_O_APPEND         0x0008
#define SVR4_O_SYNC           0x0010
#define SVR4_O_NONBLOCK       0x0080
#define SVR4_O_CREAT          0x0100
#define SVR4_O_TRUNC          0x0200
#define SVR4_O_EXCL           0x0400
#define SVR4_O_NOCTTY         0x0800

/* sigaction(2) flags */
#define SVR4_SA_ONSTACK       0x00000001
#define SVR4_SA_RESETHAND     0x00000002
#define SVR4_SA_RESTART       0x00000004
#define SVR4_SA_SIGINFO       0x00000008
#define SVR4_SA_NODEFER       0x00000010
#define SVR4_SA_NOCLDWAIT     0x00010000
#define SVR4_SA_NOCLDSTOP     0x00020000

/* waitid(2): id types, options, and si_code for SIGCLD */
#define SVR4_P_PID            0
#define SVR4_P_PGID           2
#define SVR4_P_ALL            7
#define SVR4_WUNTRACED        0004
#define SVR4_WCONTINUED       0010
#define SVR4_WNOHANG          0100
#define SVR4_CLD_EXITED       1
#define SVR4_CLD_KILLED       2
#define SVR4_CLD_DUMPED       3
#define SVR4_CLD_STOPPED      5
#define SVR4_CLD_CONTINUED    6
#define SVR4_SIGCLD           18

/* ioctl(2): termios, window size, process group */
#define SVR4_TCGETS           0x540D
#define SVR4_TCSETS           0x540E
#define SVR4_TCSETSW          0x540F
#define SVR4_TCSETSF          0x5410
#define SVR4_TIOCSWINSZ       0x5467
#define SVR4_TIOCGWINSZ       0x5468
#define SVR4_TIOCGPGRP        0x7414
#define SVR4_TIOCSPGRP        0x7415
#define SVR4_NCCS             19

/* getrlimit(2) resources that are numbered differently */
#define SVR4_RLIMIT_NOFILE    5
#define SVR4_RLIMIT_VMEM      6

/* mmap(2) flags */
#define SVR4_MAP_SHARED       0x01
#define SVR4_MAP_PRIVATE      0x02
#define SVR4_MAP_FIXED        0x10

/* sysconfig(2) */
#define SVR4_CONFIG_NGROUPS   2
#define SVR4_CONFIG_CHILD_MAX 3
#define SVR4_CONFIG_OPEN_FILES 4
#define SVR4_CONFIG_POSIX_VER 5
#define SVR4_CONFIG_PAGESIZE  6
#define SVR4_CONFIG_CLK_TCK   7

/* systeminfo(2) */
#define SVR4_SI_SYSNAME       1
#define SVR4_SI_HOSTNAME      2
#define SVR4_SI_RELEASE       3
#define SVR4_SI_VERSION       4
#define SVR4_SI_MACHINE       5
#define SVR4_SI_ARCHITECTURE  6

/* errno values that differ from substrate's */
#define SVR4_EDEADLK          45
#define SVR4_ENAMETOOLONG     78
#define SVR4_EOVERFLOW        79
#define SVR4_ENOSYS           89
#define SVR4_ELOOP            90
#define SVR4_ENOTEMPTY        93
#define SVR4_EOPNOTSUPP       122
#define SVR4_ETIMEDOUT        145

#endif /* _SVR4_SYSCALLS_H */
