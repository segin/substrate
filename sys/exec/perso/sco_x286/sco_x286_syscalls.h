/*
 * sco_x286_syscalls.h - SCO Xenix/286 system call numbers.
 *
 * Xenix/286 numbers its system calls exactly like System V Release 2, and
 * hangs everything Xenix added off a single multiplexed entry -- call 40,
 * with the sub-function in %ah.  The values below were read out of the SCO
 * Xenix 286 Development System's own libc (2.2.1a): every stub in there is
 *
 *     _name:  mov  $N,%ax
 *             jmp  __syscall
 *
 * so the archive member name gives the call and the immediate gives N.
 */
#ifndef _SCO_X286_SYSCALLS_H
#define _SCO_X286_SYSCALLS_H

#define X286_SYS_exit        1
#define X286_SYS_fork        2
#define X286_SYS_read        3
#define X286_SYS_write       4
#define X286_SYS_open        5
#define X286_SYS_close       6
#define X286_SYS_wait        7
#define X286_SYS_creat       8
#define X286_SYS_link        9
#define X286_SYS_unlink      10
#define X286_SYS_exec        11
#define X286_SYS_chdir       12
#define X286_SYS_time        13
#define X286_SYS_mknod       14
#define X286_SYS_chmod       15
#define X286_SYS_chown       16
#define X286_SYS_brk         17
#define X286_SYS_stat        18
#define X286_SYS_lseek       19
#define X286_SYS_getpid      20
#define X286_SYS_mount       21
#define X286_SYS_umount      22
#define X286_SYS_setuid      23
#define X286_SYS_getuid      24
#define X286_SYS_stime       25
#define X286_SYS_ptrace      26
#define X286_SYS_alarm       27
#define X286_SYS_fstat       28
#define X286_SYS_pause       29
#define X286_SYS_utime       30
#define X286_SYS_stty        31
#define X286_SYS_gtty        32
#define X286_SYS_access      33
#define X286_SYS_nice        34
#define X286_SYS_statfs      35
#define X286_SYS_sync        36
#define X286_SYS_kill        37
#define X286_SYS_fstatfs     38
#define X286_SYS_setpgrp     39
#define X286_SYS_xenix       40   /* multiplexer; sub-function in %ah */
#define X286_SYS_dup         41
#define X286_SYS_pipe        42
#define X286_SYS_times       43
#define X286_SYS_profil      44
#define X286_SYS_plock       45   /* "xlock" in the Xenix libc */
#define X286_SYS_setgid      46
#define X286_SYS_getgid      47
#define X286_SYS_signal      48
#define X286_SYS_msgsys      49
#define X286_SYS_sysi86      50
#define X286_SYS_acct        51
#define X286_SYS_shmsys      52   /* "phys" on Xenix/286 */
#define X286_SYS_semsys      53
#define X286_SYS_ioctl       54
#define X286_SYS_uadmin      55
#define X286_SYS_utssys      57   /* uname(0) / ustat(2) */
#define X286_SYS_execve      59
#define X286_SYS_umask       60
#define X286_SYS_chroot      61
#define X286_SYS_fcntl       62
#define X286_SYS_ulimit      63

/* Sub-functions of X286_SYS_xenix, taken from %ah. */
#define X286_XSYS_locking    1
#define X286_XSYS_creatsem   2
#define X286_XSYS_opensem    3
#define X286_XSYS_sigsem     4
#define X286_XSYS_waitsem    5
#define X286_XSYS_nbwaitsem  6
#define X286_XSYS_rdchk      7
#define X286_XSYS_stkgrow    8   /* crt0's __stkgrow, on stack overflow */
#define X286_XSYS_chsize     10
#define X286_XSYS_ftime      11
#define X286_XSYS_nap        12
#define X286_XSYS_sdget      13
#define X286_XSYS_sdfree     14
#define X286_XSYS_sdenter    15
#define X286_XSYS_sdleave    16
#define X286_XSYS_sdgetv     17
#define X286_XSYS_sdwaitv    18
#define X286_XSYS_brkctl     19
#define X286_XSYS_msgctl     22
#define X286_XSYS_msgget     23
#define X286_XSYS_msgsnd     24
#define X286_XSYS_msgrcv     25
#define X286_XSYS_semctl     26
#define X286_XSYS_semget     27
#define X286_XSYS_semop      28
#define X286_XSYS_shmctl     29
#define X286_XSYS_shmget     30
#define X286_XSYS_shmat      31
#define X286_XSYS_proctl     32
#define X286_XSYS_execseg    33

/* brkctl(2) commands, from Xenix <sys/brk.h>. */
#define X286_BR_ARGSEG       001   /* grow/shrink the segment named by ptr */
#define X286_BR_NEWSEG       002   /* allocate a brand new segment */
#define X286_BR_IMPSEG       003   /* implied: the last data segment */
#define X286_BR_FREESEG      004   /* free the segment named by ptr */
#define X286_BR_HUGE         0100  /* modifier: huge context */

#endif /* _SCO_X286_SYSCALLS_H */
