#ifndef _ELKS_SYSCALL_TABLE_H
#define _ELKS_SYSCALL_TABLE_H

/*
 * ELKS syscall numbers used by the Substrate ELKS personality.
 *
 * This table follows the ELKS/elksemu 16-bit userspace numbering that the
 * personality contract targets. Slots not defined here are reserved and are
 * expected to return -ENOSYS until explicitly implemented.
 */

#define ELKS_SYS_exit           1
#define ELKS_SYS_fork           2
#define ELKS_SYS_read           3
#define ELKS_SYS_write          4
#define ELKS_SYS_open           5
#define ELKS_SYS_close          6
#define ELKS_SYS_waitpid        7
#define ELKS_SYS_creat          8
#define ELKS_SYS_link           9
#define ELKS_SYS_unlink         10
#define ELKS_SYS_execve         11
#define ELKS_SYS_chdir          12
#define ELKS_SYS_time           13
#define ELKS_SYS_mknod          14
#define ELKS_SYS_chmod          15
#define ELKS_SYS_chown          16
#define ELKS_SYS_stat           18
#define ELKS_SYS_lseek          19
#define ELKS_SYS_getpid         20
#define ELKS_SYS_mount          21
#define ELKS_SYS_umount         22
#define ELKS_SYS_setuid         23
#define ELKS_SYS_getuid         24
#define ELKS_SYS_stime          25
#define ELKS_SYS_alarm          27
#define ELKS_SYS_fstat          28
#define ELKS_SYS_pause          29
#define ELKS_SYS_access         33
#define ELKS_SYS_sync           36
#define ELKS_SYS_kill           37
#define ELKS_SYS_rename         38
#define ELKS_SYS_mkdir          39
#define ELKS_SYS_rmdir          40
#define ELKS_SYS_dup            41
#define ELKS_SYS_pipe           42
#define ELKS_SYS_times          43
#define ELKS_SYS_brk            45
#define ELKS_SYS_setgid         46
#define ELKS_SYS_getgid         47
#define ELKS_SYS_signal         48
#define ELKS_SYS_ioctl          54
#define ELKS_SYS_fcntl          55
#define ELKS_SYS_umask          60
#define ELKS_SYS_dup2           63
#define ELKS_SYS_getppid        64
#define ELKS_SYS_getpgrp        65
#define ELKS_SYS_MAX            128

#endif /* _ELKS_SYSCALL_TABLE_H */
