#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>
#include <sys/syscall.h>
#include <sys/types.h>

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

typedef int pid_t;
typedef int ssize_t;
// off_t defined in sys/types.h

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

unsigned int sleep(unsigned int seconds);

int gethostname(char *name, size_t len);
int sethostname(const char *name, size_t len);


#endif
