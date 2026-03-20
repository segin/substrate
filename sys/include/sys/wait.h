#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H

#include <sys/types.h>

/* Options for waitpid */
#define WNOHANG     1
#define WUNTRACED   2
#define WCONTINUED  8

/* Macros for WEXITSTATUS etc. */
/* Exit status encoding:
 * High 8 bits: exit code
 * Low 7 bits: signal
 * Bit 7: core dump flag (0x80)
 * 
 * If stopped: 0x7f in low 8 bits (usually), then stop signal in high 8 bits?
 * Linux/POSIX varying.
 * Let's follow BSD/Linux common:
 * Exited: (status & 0x7f) == 0. Code is (status >> 8) & 0xff.
 * Signaled: (status & 0x7f) != 0 && (status & 0x7f) != 0x7f.
 * Stopped: (status & 0xff) == 0x7f. Signal is (status >> 8) & 0xff.
 */

#define _WSTATUS(x) (x)

#define WIFEXITED(x)    ((_WSTATUS(x) & 0x7f) == 0)
#define WEXITSTATUS(x)  ((_WSTATUS(x) >> 8) & 0xff)

#define WIFSIGNALED(x)  ((_WSTATUS(x) & 0x7f) != 0 && (_WSTATUS(x) & 0x7f) != 0x7f)
#define WTERMSIG(x)     (_WSTATUS(x) & 0x7f)
#define WCOREDUMP(x)    (_WSTATUS(x) & 0x80)

#define WIFSTOPPED(x)   ((_WSTATUS(x) & 0xff) == 0x7f)
#define WSTOPSIG(x)     ((_WSTATUS(x) >> 8) & 0xff)

#define WIFCONTINUED(x) ((_WSTATUS(x) == 0xffff)) // Wrapper flag usually

// idtype_t for waitid
typedef enum {
    P_ALL,
    P_PID,
    P_PGID
} idtype_t;

pid_t wait(int *status);
pid_t waitpid(pid_t pid, int *status, int options);
struct rusage;
pid_t wait4(pid_t pid, int *status, int options, struct rusage *rusage);

#endif
