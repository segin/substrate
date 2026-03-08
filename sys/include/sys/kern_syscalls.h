#ifndef _SYS_KERN_SYSCALLS_H
#define _SYS_KERN_SYSCALLS_H

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/signal.h>
#include <sys/utsname.h>

/* Forward declarations */
struct thr_param;
struct pollfd;
struct sys_procinfo;
typedef struct sys_procinfo sys_procinfo_t;
struct timespec;
struct timeval;
struct timezone;
struct itimerval;
struct tms;

/* Internal kernel versions of syscalls that take kernel pointers */
struct sigaction;


/* Internal kernel versions of syscalls that take kernel pointers */
int kern_acct(const char *path);
int kern_open(const char *path, int flags, int mode);
int kern_read(int fd, char *buf, int len);
int kern_write(int fd, const char *buf, int len);
int kern_close(int fd);
int kern_stat(const char *path, struct stat *buf);
int kern_lstat(const char *path, struct stat *buf);
int kern_fstat(int fd, struct stat *buf);
int kern_lseek(int fd, off_t offset, int whence);
int kern_sigaction(int sig, const struct sigaction *act, struct sigaction *oact);
int kern_sigaltstack(const stack_t *ss, stack_t *oss);
int kern_getdents(unsigned int fd, void *dirp, unsigned int count);
int kern_getdents64(unsigned int fd, void *dirp, unsigned int count);
int kern_uname(struct utsname *buf);
int kern_thr_new(struct thr_param *param, int param_size);
int kern_chroot(const char *path);
int kern_mkdir(const char *p, int m);
int kern_stat(const char *path, struct stat *buf);
int kern_lstat(const char *path, struct stat *buf);
int kern_poll(struct pollfd *fds, unsigned int nfds, int timeout);
int kern_fstat(int fd, struct stat *buf);
int kern_ioctl(int fd, uint32_t request, void *arg);
int kern_unlink(const char *path);
int kern_link(const char *oldpath, const char *newpath);
int kern_readlink(const char *pathname, char *buf, size_t bufsiz);
int kern_access(const char *path, int mode);
int kern_pipe(int *fds);
struct rusage;
int kern_wait4(pid_t pid, int *status, int options, struct rusage *rusage);
int kern_waitpid(int pid, int *status, int options);
int kern_execve(const char *f, char *const a[], char *const e[]);
int kern_mount(const char *source, const char *target, const char *fstype, unsigned long flags, void *data);
int kern_umount(const char *target);
int kern_chdir(const char *path);
int kern_getcwd(char *buf, size_t size);
int kern_proc_info(pid_t pid, sys_procinfo_t *info);
int kern_proc_list(pid_t *pids, size_t count);
int kern_hostname(char *buf, size_t len);
/* Time syscalls */
time_t kern_time(time_t *tloc);
int kern_stime(time_t *t);
int kern_gettimeofday(struct timeval *tv, struct timezone *tz);
int kern_clock_gettime(clockid_t clk_id, struct timespec *tp);
clock_t kern_times(struct tms *buf);
unsigned int kern_alarm(unsigned int sec);
int kern_getitimer(int which, struct itimerval *curr_value);
int kern_setitimer(int which, const struct itimerval *new_value, struct itimerval *old_value);

/* Signal related internal versions */
int kern_sigprocmask(int how, const uint32_t *set, uint32_t *oset);
int kern_sigpending(uint32_t *set);
int kern_sigsuspend(const uint32_t *mask);
int kern_sigwait(const uint32_t *set, int *sig);
int kern_sigtimedwait(const uint32_t *set, siginfo_t *info, const struct timespec *timeout);

#endif
