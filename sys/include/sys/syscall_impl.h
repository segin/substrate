/*
 * syscall_impl.h - Shared syscall extern declarations for personalities
 *
 * This header provides extern declarations for all kernel syscalls used by
 * personalities. Include this instead of duplicating extern lists.
 */

#ifndef _SYS_SYSCALL_IMPL_H
#define _SYS_SYSCALL_IMPL_H

#include <stddef.h>
#include <stdint.h>

/* Process management */
extern int sys_exit(int);
extern int sys_fork(void);
extern int sys_vfork(void);
extern int sys_execve(const char*, char**, char**);
extern int sys_waitpid(int, int*, int);
extern int sys_getpid(void);
extern int sys_getppid(void);
extern int sys_setsid(void);
extern int sys_getpgrp(void);
extern int sys_setpgid(int, int);
extern int sys_getpgid(int);
extern int sys_clone(uint32_t, void*, int*, void*, int*);
extern int sys_ptrace(int, int, int, int);
extern int sys_ulimit(int, long);
extern int sys_prof(void*, size_t, unsigned long, unsigned int);

/* User/group management */
extern int sys_getuid(void);
extern int sys_geteuid(void);
extern int sys_setuid(int);
extern int sys_getgid(void);
extern int sys_getegid(void);
extern int sys_setgid(int);

/* File I/O - NOTE: native uses 64-bit types! Foreign personalities need wrappers */
extern int sys_read(int, char*, int);
extern int sys_write(int, const char*, int);
extern int sys_open(const char*, int, int);
extern int sys_close(int);
extern int sys_creat(const char*, int);
extern int sys_dup(int);
extern int sys_dup2(int, int);
extern int sys_pipe(int*);
extern int sys_fcntl(int, int, int);
extern int sys_ioctl(int, uint32_t, void*);
extern int sys_readlink(const char*, char*, size_t);
extern int sys_lchown(const char*, int, int);

/* lseek - NATIVE uses 64-bit offset split into hi/lo */
extern int64_t sys_lseek(int, uint32_t, uint32_t, int);

/* stat family - NATIVE uses 64-bit struct stat */
extern int sys_stat(const char*, void*);
extern int sys_lstat(const char*, void*);
extern int sys_fstat(int, void*);
extern int sys_statfs(const char*, void*);
extern int sys_fstatfs(int, void*);

/* File system operations */
extern int sys_link(const char*, const char*);
extern int sys_unlink(const char*);
extern int sys_mkdir(const char*, int);
extern int sys_rmdir(const char*);
extern int sys_mknod(const char*, int, int);
extern int sys_chmod(const char*, int);
extern int sys_chdir(const char*);
extern int sys_fchdir(int);
extern int sys_chroot(const char*);
extern int sys_access(const char*, int);
extern int sys_utime(const char*, void*);
extern int sys_sync(void);
extern int sys_mount(const char*, const char*, const char*, unsigned long, void*);
extern int sys_umount(const char*);
extern int sys_getdents(unsigned int, void*, unsigned int);
extern int sys_getcwd(char*, size_t);

/* Memory management */
extern void *sys_mmap(void*, size_t, int, int, int, uint64_t);
extern int sys_munmap(void*, size_t);
extern int sys_mprotect(void*, size_t, int);
extern int sys_msync(void*, size_t, int);
extern int sys_brk(uint32_t);

/* Signals */
extern int sys_kill(int, int);
extern int sys_signal(int, void*);
extern int sys_sigaction(int, const void*, void*);
extern int sys_sigprocmask(int, const void*, void*);
extern int sys_sigaltstack(const void*, void*);
extern int sys_sigpending(void*);
extern int sys_sigsuspend(const void*);
extern int sys_sigret(void);
extern int sys_alarm(unsigned int);
extern int sys_pause(void);
extern int sys_nice(int);

/* Time */
extern int64_t sys_time(int64_t*);
extern int sys_stime(uint32_t*);
extern int sys_times(void*);
extern int sys_nanosleep(void*, void*);
extern int sys_gettimeofday(void*, void*);

/* Other */
extern int sys_uname(void*);
extern int sys_acct(const char*);
extern int sys_poll(void*, unsigned int, int);
extern int sys_futex(int*, int, int, void*, int*, int);
extern int sys_set_thread_area(void*);
extern int sys_modify_ldt(int, void*, unsigned long);
extern int sys_fsync(int);
extern int sys_umask(int);

/* Native specific */
struct thr_param;
extern int sys_thr_new(struct thr_param*, int);
struct pmap_stats;
extern int sys_pmap_stats(struct pmap_stats*);
extern int sys_proc_info(int, void*);
extern int sys_proc_list(int*, size_t);
extern int sys_proc_count(void);
extern int sys_cpu_count(void);
extern int sys_hostname(char*, size_t);
extern int sys_rt_sigreturn(void*);
extern int sys_sigreturn(void*);

#endif /* _SYS_SYSCALL_IMPL_H */
