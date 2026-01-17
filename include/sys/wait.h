#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H

#include <sys/types.h>

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

#endif
