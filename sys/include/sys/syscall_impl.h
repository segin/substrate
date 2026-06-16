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
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/sysinfo.h>

struct thr_param;
struct itimerval;
struct timeval;
struct timezone;
struct timespec;
struct tms;

/* Process management */
extern int sys_exit(int);
extern int sys_fork(void);
extern int sys_vfork(void);
extern int sys_execve(const char*, char**, char**);
extern int sys_waitpid(int, int*, int);
extern int sys_wait4(pid_t, int*, int, struct rusage*);
extern int sys_getpid(void);
extern int sys_getppid(void);
extern int sys_setsid(void);
extern int sys_setlogin(const char *);
extern int sys_revoke(const char *);
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
extern int sys_seteuid(int);
extern int sys_setegid(int);
extern int sys_setreuid(int, int);
extern int sys_setregid(int, int);
extern int sys_setresuid(int, int, int);
extern int sys_setresgid(int, int, int);
extern int sys_getresuid(uint32_t*, uint32_t*, uint32_t*);
extern int sys_getresgid(uint32_t*, uint32_t*, uint32_t*);
extern int sys_getsid(int);

/* File I/O - NOTE: native uses 64-bit types! Foreign personalities need wrappers */
extern ssize_t sys_read(int, char*, size_t);
extern ssize_t sys_write(int, const char*, size_t);
extern int sys_getrandom(void*, size_t, unsigned int);
extern int sys_open(const char*, int, int);
extern int sys_openat(int, const char*, int, int);
extern int sys_close(int);
extern int sys_creat(const char*, int);
extern int sys_dup(int);
extern int sys_dup2(int, int);
extern int sys_dup3(int, int, int);
extern int sys_pipe(int*);
extern int sys_pipe2(int*, int);
extern int sys_fcntl(int, int, int);
extern int sys_ioctl(int, uint32_t, void*);
extern int sys_readlink(const char*, char*, size_t);
extern int sys_readlinkat(int, const char*, char*, size_t);
extern int sys_lchown(const char*, int, int);
extern int sys_lchownat(int, const char*, int, int, int);
extern int sys_fchmod(int, int);
extern int sys_fchown(int, int, int);
extern int sys_fchownat(int, const char*, int, int, int);

/* lseek/truncate - NATIVE uses 64-bit offset split into hi/lo */
extern int64_t sys_lseek(int, uint32_t, uint32_t, int);
extern int sys_truncate(const char*, uint32_t, uint32_t);
extern int sys_ftruncate(int, uint32_t, uint32_t);

/* stat family - NATIVE uses 64-bit struct stat */
extern int sys_stat(const char*, void*);
extern int sys_lstat(const char*, void*);
extern int sys_fstat(int, void*);
extern int sys_fstatat(int, const char*, void*, int);
extern int sys_utimensat(int, const char *, const void *, int);
extern int sys_futimens(int, const void *);

/* xattr family — read side implemented for ext2/4, others NULL-hook
 * and return -ENOTSUP; write side is ENOSYS until a writer ports.  */
extern int sys_getxattr(const char *, const char *, void *, size_t);
extern int sys_lgetxattr(const char *, const char *, void *, size_t);
extern int sys_fgetxattr(int,          const char *, void *, size_t);
extern int sys_listxattr(const char *, char *, size_t);
extern int sys_llistxattr(const char *, char *, size_t);
extern int sys_flistxattr(int, char *, size_t);
extern int sys_setxattr(const char *, const char *, const void *, size_t, int);
extern int sys_lsetxattr(const char *, const char *, const void *, size_t, int);
extern int sys_fsetxattr(int,          const char *, const void *, size_t, int);
extern int sys_removexattr(const char *, const char *);
extern int sys_lremovexattr(const char *, const char *);
extern int sys_fremovexattr(int, const char *);
extern int sys_statfs(const char*, void*);
extern int sys_fstatfs(int, void*);
extern int sys_statvfs(const char*, void*);
extern int sys_fstatvfs(int, void*);

/* File system operations */
extern int sys_link(const char*, const char*);
extern int sys_rename(const char*, const char*);
extern int sys_symlink(const char*, const char*);
extern int sys_unlink(const char*);
extern int sys_unlinkat(int, const char*, int);
extern int sys_mkdir(const char*, int);
extern int sys_mkdirat(int, const char*, int);
extern int sys_rmdir(const char*);
extern int sys_mknod(const char*, int, int);
extern int sys_chmod(const char*, int);
extern int sys_chown(const char*, int, int);
extern int sys_lchmod(const char*, int);
extern int sys_fchmodat(int, const char*, int, int);
extern int sys_chdir(const char*);
extern int sys_fchdir(int);
extern int sys_chroot(const char*);
extern int sys_fchroot(int);
extern int sys_access(const char*, int);
extern int sys_utime(const char*, void*);
extern int sys_sync(void);
extern int sys_mount(const char*, const char*, const char*, unsigned long, void*);
extern int sys_umount(const char*);
extern int sys_umount2(const char*, int);
extern int sys_getdents(unsigned int, void*, unsigned int);
extern int sys_getdents64(unsigned int, void*, unsigned int);
extern int sys_getcwd(char*, size_t);

/* Memory management */
extern void *sys_mmap(void*, size_t, int, int, int, uint64_t);
extern int sys_munmap(void*, size_t);
extern int sys_mprotect(void*, size_t, int);
extern int sys_msync(void*, size_t, int);
extern void *sys_brk(void *);

/* System V semaphores (sys/kern/ipc_sem.c) */
struct sembuf;
extern int sys_semget(key_t, int, int);
extern int sys_semop(int, struct sembuf *, size_t);
extern int sys_semctl(int, int, int, uintptr_t);

/* Signals */
extern int sys_kill(int, int);
extern int sys_signal(int, void*);
extern int sys_sigaction(int, const void*, void*);
extern int sys_sigprocmask(int, const void*, void*);
extern int sys_sigaltstack(const void*, void*);
extern int sys_sigpending(void*);
extern int sys_sigsuspend(const void*);
extern int sys_sigret(void);
extern unsigned int sys_alarm(unsigned int);
extern int sys_getitimer(int, struct itimerval *);
extern int sys_setitimer(int, const struct itimerval *, struct itimerval *);
extern int sys_pause(void);
extern int sys_nice(int);

/* Time */
extern time_t sys_time(time_t*);
extern int sys_stime(time_t*);
extern clock_t sys_times(struct tms*);
extern int sys_nanosleep(void*, void*);
extern int sys_gettimeofday(struct timeval*, struct timezone*);
extern int sys_clock_gettime(int, struct timespec*);

/* Other */
extern int sys_uname(void*);
extern int sys_acct(const char*);
extern int sys_poll(void*, unsigned int, int);
extern int sys_futex(int*, int, int, void*, int*, int);
extern int sys_set_thread_area(void*);
extern int sys_modify_ldt(int, void*, unsigned long);
extern int sys_fsync(int);
extern int sys_umask(int);
extern int sys_reboot(int);
extern int sys_sysctl(int *name, unsigned int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen);

extern int sys_thr_exit(void*);
extern int sys_thr_join(tid_t, void**);
extern int sys_thr_self(void);
extern int sys_thr_new(struct thr_param*, int);
extern int sys_thr_kill(long, int);
extern int sys_thr_kill2(pid_t, long, int);
struct timespec;
extern int sys_thr_suspend(const struct timespec *);
extern int sys_thr_wake(long);
extern int sys_thr_set_name(long, const char *);
extern int sys_yield(void);
struct pmap_stats;
extern int sys_pmap_stats(struct pmap_stats*);
extern int sys_proc_info(pid_t, sys_procinfo_t*);
extern int sys_proc_list(pid_t*, size_t);
extern int sys_proc_count(void);
extern int sys_proc_threads(pid_t, tid_t*, size_t*);
struct sys_thrinfo;
extern int sys_proc_thr_count(pid_t);
extern int sys_proc_thr_list(pid_t, struct sys_thrinfo*, size_t);
extern int sys_proc_fds(pid_t, sys_fd_t*, size_t*);
extern int sys_proc_maps(pid_t, sys_map_t*, size_t*);
extern int sys_proc_cwd(pid_t, char*, size_t);
extern int sys_proc_exe(pid_t, char*, size_t);
extern int sys_proc_cmdline(pid_t, char**, size_t*);
extern int sys_proc_environ(pid_t, char**, size_t*);
extern int sys_cpu_count(void);
extern int sys_hostname(char*, size_t);
extern int sys_rt_sigreturn(void*);
extern int sys_sigreturn(void*);
struct sysinfo;
extern int sys_sysinfo(struct sysinfo*);

/* Process info */
extern int sys_getrusage(int, struct rusage*);
extern int sys_getpriority(int, int);
extern int sys_setpriority(int, int, int);
extern int sys_sysarch(int, void*);
extern int freebsd_sys_uname(void*);

/* FreeBSD personality stubs */
extern int sys_profil(void*, unsigned int, unsigned int, unsigned int);
extern int sys_madvise(void*, size_t, int);
extern int sys_minherit(void*, size_t, int);
extern int sys_getrlimit(int, void*);
extern int sys_setrlimit(int, const void*);
extern int sys_issetugid(void);
extern int sys_cap_getmode(unsigned int*);
extern ssize_t sys_readv(int, const void*, int);
extern ssize_t sys_writev(int, const void*, int);
extern int sys_getgroups(int, void*);
extern int sys_setgroups(int, const void*);
extern int sys_getlogin(char*, unsigned int);
/* sys_thr_kill prototype moved up next to the rest of the sys_thr_* set */
extern int sys_umtx_op(void*, int, unsigned long, void*, void*);
extern int sys_clock_nanosleep(int, int, const void*, void*);
extern int sys_pselect(int, void*, void*, void*, const void*, const void*);
extern int sys_ppoll(void*, unsigned int, const void*, const void*);
extern int sys_select(int, void*, void*, void*, void*);
extern int sys_wait6(int, int, int*, int, void*, void*);
extern int sys_fdatasync(int);
extern int sys_accept(int, void*, int*);
extern int sys_accept4(int, void*, int*, int);
extern int sys_bind(int, const void*, int);
extern int sys_listen(int, int);
extern int sys_socket(int, int, int);
extern int sys_connect(int, const void*, int);
extern ssize_t sys_sendto(int, const void*, size_t, int, const void*, int);
extern ssize_t sys_recvfrom(int, void*, size_t, int, void*, int*);
extern int sys_getsockname(int, void*, int*);
extern int sys_getpeername(int, void*, int*);
extern int sys_getsockopt(int, int, int, void*, int*);
extern int sys_setsockopt(int, int, int, const void*, int);
extern ssize_t sys_recvmsg(int, void*, int);
extern ssize_t sys_sendmsg(int, const void*, int);
extern ssize_t sys_send(int, const void*, size_t, int);
extern ssize_t sys_recv(int, void*, size_t, int);
extern int sys_sysctlbyname(const char*, void*, size_t*, void*, size_t);
extern int sys_sigwaitinfo(const void*, void*);
extern int sys_getdtablesize(void);
extern int sys_pathconf(const char*, int);
extern int sys_shutdown(int, int);
extern int sys_socketpair(int, int, int, int*);
extern int sys_msync(void*, size_t, int);
extern int sys_pdfork(int*, int);
extern int freebsd_sys_sysctl(int*, unsigned int, void*, size_t*, void*, size_t);

#endif /* _SYS_SYSCALL_IMPL_H */
