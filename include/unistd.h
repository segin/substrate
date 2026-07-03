#ifndef _UNISTD_H
#define _UNISTD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <sys/syscall.h>
#include <sys/types.h>

/*
 * POSIX feature-test stamp.
 *
 * Substrate targets POSIX.1-2017 (a.k.a. Issue 7 TC2 = 200809L);
 * many third-party headers use _POSIX_VERSION (and the older
 * _POSIX2_VERSION / _XOPEN_VERSION) to decide whether to redeclare
 * libc primitives with pre-ANSI signatures.  GNU make's
 * src/makeint.h is the canonical example — without
 * _POSIX_VERSION it falls back to `long lseek ()` / `char *getcwd
 * (void)` K&R prototypes that conflict with substrate's ISO ones.
 */
#define _POSIX_VERSION   200809L
#define _POSIX2_VERSION  200809L
#define _XOPEN_VERSION   700

/*
 * POSIX.1b clock/timer options.  Substrate implements the per-process
 * interval timers (timer_create(2)), the monotonic clock, and the
 * process/thread CPU-time clocks (clock_gettime(CLOCK_PROCESS_CPUTIME_ID
 * / CLOCK_THREAD_CPUTIME_ID) is backed by per-process rusage), so the
 * corresponding option macros advertise support at the current POSIX
 * level.  Third-party code and the OPTS conformance tests gate whole
 * code paths on `#if !defined(_POSIX_CPUTIME) || _POSIX_CPUTIME == -1`,
 * so leaving these undefined silently disables the feature at compile
 * time even though the runtime supports it.
 */
#define _POSIX_TIMERS            200809L
#define _POSIX_MONOTONIC_CLOCK   200809L
#define _POSIX_CPUTIME           200809L
#define _POSIX_THREAD_CPUTIME    200809L
/* POSIX unnamed + named semaphores (sem_init/sem_open/sem_wait/sem_post/...)
 * are implemented (libc semaphore.c over the kernel ksem objects).  OPTS's
 * functional semaphore tests compile out to PTS_UNRESOLVED under
 * "ifndef _POSIX_SEMAPHORES", so leaving it undefined disables them at
 * compile time even though the runtime fully supports the option. */
#define _POSIX_SEMAPHORES        200809L

/* POSIX memory-management options.  Substrate implements mmap(2) with
 * enforced per-page protection (a write to a PROT_READ mapping faults —
 * pmap sets PTE_W only when VM_PROT_WRITE is requested), mprotect(2), and
 * mlockall/munlockall(2) (locking is a no-op truth: no swap, so every page
 * is already non-swappable).  OPTS gates whole tests on these option
 * macros — mmap/6 (memory protection) and munlockall/5 (memory locking)
 * compile out to PTS_UNSUPPORTED when they are undefined, even though the
 * runtime fully supports the feature. */
#define _POSIX_MEMORY_PROTECTION 200809L
#define _POSIX_MEMLOCK           200809L

/* POSIX sporadic-server scheduling policy (SCHED_SPORADIC + the
 * sched_ss_* sched_param members).  Substrate accepts and validates the
 * policy and its parameters at the sched_setscheduler(2)/sched_setparam(2)
 * API level (sys/kern/sched_posix.c); the substrate scheduler runs a
 * SCHED_SPORADIC thread like SCHED_FIFO at its sched_priority (no runtime
 * budget replenishment).  OPTS gates its whole SS test group on
 * "defined(_POSIX_SPORADIC_SERVER) && _POSIX_SPORADIC_SERVER != -1", so
 * leaving these undefined compiles those tests out to PTS_UNSUPPORTED. */
#define _POSIX_SPORADIC_SERVER          200809L
#define _POSIX_THREAD_SPORADIC_SERVER   200809L

/* POSIX-2017 §11.1.7: a c_cc[] slot set to _POSIX_VDISABLE disables
 * the associated special-character function.  Substrate matches the
 * Linux/glibc value of 0 (0xff is the BSD convention).  */
#define _POSIX_VDISABLE  0

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* lseek() whence values.  POSIX exposes these through <unistd.h>
 * as well as <stdio.h>; substrate historically had them only in
 * stdio.h, which broke BSD code (OpenSSH's bsd-flock.c, libdbm, …)
 * that includes only fcntl.h or unistd.h. */
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

typedef int pid_t;
// off_t and ssize_t defined in sys/types.h

[[noreturn]] void _exit(int status);
int fork(void);
int execve(const char *filename, char *const argv[], char *const envp[]);
int execv(const char *path, char *const argv[]);
int execvp(const char *file, char *const argv[]);
int execl(const char *path, const char *arg, ...);
int execlp(const char *file, const char *arg, ...);
int execle(const char *path, const char *arg, ...);
pid_t waitpid(pid_t pid, int *status, int options);

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
ssize_t pread(int fd, void *buf, size_t count, off_t offset);
ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
int close(int fd);
int mkstemp(char *__template);
int unlink(const char *pathname);
int unlinkat(int dirfd, const char *pathname, int flags);
int rmdir(const char *pathname);
int link(const char *oldpath, const char *newpath);
int symlink(const char *target, const char *linkpath);
ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);
int chdir(const char *path);
int chroot(const char *path);
char *getcwd(char *buf, size_t size);

int pipe(int pipefd[2]);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
void sync(void);
int fsync(int fd);
int fdatasync(int fd);
int fchdir(int fd);

/* confstr(3) name values (glibc-compatible subset). */
#define _CS_PATH                    0
#define _CS_GNU_LIBC_VERSION        2
#define _CS_GNU_LIBPTHREAD_VERSION  3

/* C99 / POSIX additions filled in by lib/c/src/posix_extra.c. */
size_t confstr(int name, char *buf, size_t len);
int    dup3(int oldfd, int newfd, int flags);
int    faccessat(int dirfd, const char *path, int mode, int flags);
int    getlogin_r(char *buf, size_t bufsize);
int    lockf(int fd, int cmd, off_t len);
int    nice(int inc);
int    pause(void);
int    pipe2(int pipefd[2], int flags);
ssize_t readlinkat(int dirfd, const char *path, char *buf, size_t bufsiz);
int    setegid(gid_t egid);
int    seteuid(uid_t euid);
int    setpgrp(void);
int    setregid(gid_t rgid, gid_t egid);
int    setreuid(uid_t ruid, uid_t euid);
void   swab(const void *src, void *dst, ssize_t nbytes);
int    symlinkat(const char *target, int newdirfd, const char *linkpath);
pid_t  vfork(void);

/* lockf cmds — POSIX 7. */
#define F_ULOCK 0
#define F_LOCK  1
#define F_TLOCK 2
#define F_TEST  3

int getpid(void);
pid_t getppid(void);
uid_t getuid(void);
gid_t getgid(void);
uid_t geteuid(void);
gid_t getegid(void);
int setuid(uid_t uid);
int setgid(gid_t gid);
pid_t getpgrp(void);
pid_t getpgid(pid_t pid);
pid_t getsid(pid_t pid);
int   initgroups(const char *user, gid_t group);
int setpgid(pid_t pid, pid_t pgid);
pid_t tcgetpgrp(int fd);
int tcsetpgrp(int fd, pid_t pgrp);
pid_t setsid(void);

/* BSD/glibc daemon(3) — fork, setsid, optional chdir(/) and reopen
 * stdio against /dev/null.  Returns 0 on success, -1 on failure. */
int   daemon(int nochdir, int noclose);

int access(const char *pathname, int mode);
int eaccess(const char *pathname, int mode);
int euidaccess(const char *pathname, int mode);
void *sbrk(intptr_t increment);
int   brk(void *addr);
#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0

int isatty(int fd);
char *ttyname(int fd);
int   ttyname_r(int fd, char *buf, size_t buflen);
char *mktemp(char *_template);

/* POSIX configurable path/file limit query.  Substrate returns
 * conservative defaults; the values are documented in
 * usr.man/man3/pathconf.3 (stub today). */
long pathconf(const char *path, int name);
long fpathconf(int fd, int name);
int getpagesize(void);

#define _PC_LINK_MAX           0
#define _PC_MAX_CANON          1
#define _PC_MAX_INPUT          2
#define _PC_NAME_MAX           3
#define _PC_PATH_MAX           4
#define _PC_PIPE_BUF           5
#define _PC_CHOWN_RESTRICTED   6
#define _PC_NO_TRUNC           7
#define _PC_VDISABLE           8
#define _PC_SYNC_IO            9
#define _PC_ASYNC_IO          10
#define _PC_PRIO_IO           11
#define _PC_FILESIZEBITS      13
#define _PC_REC_INCR_XFER_SIZE 14
#define _PC_REC_MAX_XFER_SIZE  15
#define _PC_REC_MIN_XFER_SIZE  16
#define _PC_REC_XFER_ALIGN     17
#define _PC_ALLOC_SIZE_MIN     18
#define _PC_SYMLINK_MAX        19
#define _PC_2_SYMLINKS         20

off_t lseek(int fd, off_t offset, int whence);
int ftruncate(int fd, off_t length);
int truncate(const char *path, off_t length);

/* LFS aliases — substrate's off_t is already 64-bit. */
#if defined(_LARGEFILE64_SOURCE) || defined(_GNU_SOURCE)
#define lseek64     lseek
#define ftruncate64 ftruncate
#define truncate64  truncate
#define pread64     pread
#define pwrite64    pwrite
#endif

unsigned int sleep(unsigned int seconds);
int usleep(useconds_t usec);
unsigned int alarm(unsigned int seconds);

int gethostname(char *name, size_t len);
int getdomainname(char *name, size_t len);
long gethostid(void);
int sethostid(long id);
int sethostname(const char *name, size_t len);

int chmod(const char *pathname, mode_t mode);
int chown(const char *pathname, uid_t owner, gid_t group);
int lchown(const char *pathname, uid_t owner, gid_t group);
int fchmod(int fd, mode_t mode);
int fchown(int fd, uid_t owner, gid_t group);
int fchownat(int dirfd, const char *pathname, uid_t owner, gid_t group, int flag);
char *getlogin(void);
int revoke(const char *path);
int getgroups(int size, gid_t list[]);
int setgroups(int size, const gid_t *list);

extern char *optarg;
extern int optind, opterr, optopt;
int getopt(int argc, char * const argv[], const char *optstring);

long syscall(long number, ...);

/* sysconf() constants */
#define _SC_ARG_MAX      0
#define _SC_CHILD_MAX    1
#define _SC_CLK_TCK      2
#define _SC_NGROUPS_MAX  3
#define _SC_OPEN_MAX     4
#define _SC_JOB_CONTROL  5
#define _SC_SAVED_IDS    6
#define _SC_VERSION      7
#define _SC_PAGESIZE     8
#define _SC_PAGE_SIZE    _SC_PAGESIZE
#define _SC_NPROCESSORS_CONF 9
#define _SC_NPROCESSORS_ONLN 10
#define _SC_PHYS_PAGES   11
#define _SC_GETPW_R_SIZE_MAX 12
#define _SC_GETGR_R_SIZE_MAX 13
/* POSIX asynchronous I/O option — substrate supports aio_* via librt, so
 * sysconf(_SC_ASYNCHRONOUS_IO) reports a positive value. */
#define _SC_ASYNCHRONOUS_IO  14
/* POSIX.1b clock options — sysconf() reports a positive value (200809L)
 * for each because substrate implements the monotonic clock and the
 * process/thread CPU-time clocks (see clock_gettime(2)). */
#define _SC_MONOTONIC_CLOCK  15
#define _SC_CPUTIME          16
#define _SC_THREAD_CPUTIME   17
/* POSIX.1b real-time signals — substrate implements SIGRTMIN..SIGRTMAX,
 * sigqueue(2) with an si_value payload, and a per-process RT-signal queue
 * (sys/include/sys/signal.h: RTSIG_QUEUE_MAX), so this option is present. */
#define _SC_REALTIME_SIGNALS 18
/* Minimum thread-stack size a substrate thread is actually created with.
 * libpthread uses a fixed 64 KiB stack (see PTHREAD_STACK_MIN); sysconf()
 * reports the same value at runtime. */
#define _SC_THREAD_STACK_MIN 19
/* Process-shared mutex/cond attribute.  Substrate's futex-backed locks are
 * best-effort across processes but NOT a guaranteed robust inter-process
 * lock (see <pthread.h>), so this option reports "unsupported" (-1).  The
 * PTHREAD_PROCESS_SHARED enum still exists for compile-time reference. */
#define _SC_THREAD_PROCESS_SHARED 20
/* Clock-selection option — clock_nanosleep(2) and pthread_condattr_setclock()
 * are implemented, so the option is present. */
#define _SC_CLOCK_SELECTION  21
/* Prioritized asynchronous I/O.  substrate's librt aio worker pool does not
 * honour per-request priorities (aio_reqprio is ignored), so unsupported. */
#define _SC_PRIORITIZED_IO   22
/* Maximum number of outstanding async I/O operations.  librt queues aio
 * requests on an unbounded (memory-limited) FIFO with no fixed ceiling, so
 * no maximum can be determined — reported as -1 (glibc convention). */
#define _SC_AIO_MAX          23
/* Maximum number of simultaneously queued real-time signals per process
 * (kernel RTSIG_QUEUE_MAX). */
#define _SC_SIGQUEUE_MAX     24
/* Maximum number of POSIX semaphores available (kernel ksem table, KSEM_MAX). */
#define _SC_SEM_NSEMS_MAX    25
/* Memory-mapped files — substrate implements mmap(2), so the option is present. */
#define _SC_MAPPED_FILES     26

long sysconf(int name);

/* getpass(3) — read a password from the controlling terminal with
 * echo suppressed.  Returns a pointer to a static buffer (overwritten
 * by subsequent calls) on success, NULL on error.  Marked obsolete
 * by POSIX.1-2001 but inetutils and openssh still use it. */
char *getpass(const char *prompt);

#ifdef __cplusplus
}
#endif
#endif
