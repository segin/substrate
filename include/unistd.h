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

/* POSIX-2017 §11.1.7: a c_cc[] slot set to _POSIX_VDISABLE disables
 * the associated special-character function.  Substrate matches the
 * Linux/glibc value of 0 (0xff is the BSD convention).  */
#define _POSIX_VDISABLE  0

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

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
char *getcwd(char *buf, size_t size);

int pipe(int pipefd[2]);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
void sync(void);
int fsync(int fd);
int fdatasync(int fd);
int fchdir(int fd);

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
int setpgid(pid_t pid, pid_t pgid);
pid_t tcgetpgrp(int fd);
int tcsetpgrp(int fd, pid_t pgrp);
pid_t setsid(void);

/* BSD/glibc daemon(3) — fork, setsid, optional chdir(/) and reopen
 * stdio against /dev/null.  Returns 0 on success, -1 on failure. */
int   daemon(int nochdir, int noclose);

int access(const char *pathname, int mode);
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

unsigned int sleep(unsigned int seconds);
int usleep(useconds_t usec);
unsigned int alarm(unsigned int seconds);

int gethostname(char *name, size_t len);
int sethostname(const char *name, size_t len);

int chmod(const char *pathname, mode_t mode);
int chown(const char *pathname, uid_t owner, gid_t group);
int lchown(const char *pathname, uid_t owner, gid_t group);
int fchmod(int fd, mode_t mode);
int fchown(int fd, uid_t owner, gid_t group);
int fchownat(int dirfd, const char *pathname, uid_t owner, gid_t group, int flag);
char *getlogin(void);
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

long sysconf(int name);

#ifdef __cplusplus
}
#endif
#endif
