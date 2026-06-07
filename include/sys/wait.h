#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/resource.h>

/* Options for waitpid */
#define WNOHANG     1
#define WUNTRACED   2
#define WCONTINUED  8

/* Exit status encoding:
 * High 8 bits: exit code
 * Low 7 bits: signal
 * Bit 7: core dump flag (0x80)
 * Stopped: (status & 0xff) == 0x7f
 */
#define WIFEXITED(s)    (((s) & 0x7f) == 0)
#define WEXITSTATUS(s)  (((s) >> 8) & 0xff)

#define WIFSIGNALED(s)  (((s) & 0x7f) != 0 && ((s) & 0x7f) != 0x7f)
#define WTERMSIG(s)     ((s) & 0x7f)
#define WCOREDUMP(s)    ((s) & 0x80)

#define WIFSTOPPED(s)   (((s) & 0xff) == 0x7f)
#define WSTOPSIG(s)     (((s) >> 8) & 0xff)

#define WIFCONTINUED(s) ((s) == 0xffff)

pid_t wait(int *status);
pid_t waitpid(pid_t pid, int *status, int options);
pid_t wait4(pid_t pid, int *status, int options, struct rusage *rusage);
pid_t wait3(int *status, int options, struct rusage *rusage);

/* C99/POSIX waitid(3p) — kernel SYS_WAITID isn't wired yet, so the
 * libc emulation runs over waitpid; see lib/c/src/posix_extra.c. */
typedef enum {
    P_ALL  = 0,
    P_PID  = 1,
    P_PGID = 2
} idtype_t;

#define WEXITED     0x04
#define WSTOPPED    0x10
#define WNOWAIT     0x20

#define CLD_EXITED    1
#define CLD_KILLED    2
#define CLD_DUMPED    3
#define CLD_TRAPPED   4
#define CLD_STOPPED   5
#define CLD_CONTINUED 6

#include <signal.h>     /* for siginfo_t */
int waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options);

#ifdef __cplusplus
}
#endif
#endif
