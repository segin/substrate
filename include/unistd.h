#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>
#include <sys/syscall.h>
#include <sys/types.h>

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
pid_t waitpid(pid_t pid, int *status, int options);

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
int mkstemp(char *template);
int unlink(const char *pathname);
int rmdir(const char *pathname);
int link(const char *oldpath, const char *newpath);
ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);
int chdir(const char *path);
char *getcwd(char *buf, size_t size);

int pipe(int pipefd[2]);
int dup2(int oldfd, int newfd);
void sync(void);

int getpid(void);
pid_t getppid(void);
uid_t getuid(void);
gid_t getgid(void);
uid_t geteuid(void);
gid_t getegid(void);
int setuid(uid_t uid);
int setgid(gid_t gid);

int access(const char *pathname, int mode);
#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0

int isatty(int fd);
char *ttyname(int fd);

off_t lseek(int fd, off_t offset, int whence);
int ftruncate(int fd, off_t length);
int truncate(const char *path, off_t length);

unsigned int sleep(unsigned int seconds);
int usleep(useconds_t usec);
unsigned int alarm(unsigned int seconds);

int gethostname(char *name, size_t len);
int sethostname(const char *name, size_t len);

int chown(const char *pathname, uid_t owner, gid_t group);
int fchmod(int fd, mode_t mode);
int fchown(int fd, uid_t owner, gid_t group);
char *getlogin(void);
int getgroups(int size, gid_t list[]);
int setgroups(int size, const gid_t *list);

extern char *optarg;
extern int optind, opterr, optopt;
int getopt(int argc, char * const argv[], const char *optstring);

long syscall(long number, ...);

#endif
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

