/*
 * <spawn.h> — POSIX.1 process spawning (posix_spawn / posix_spawnp).
 *
 * substrate implements these in libc over fork + the requested attribute and
 * file-action steps + execve/execvp; there is no dedicated kernel spawn path.
 */
#ifndef _SPAWN_H
#define _SPAWN_H

#include <sys/types.h>
#include <sched.h>
#include <signal.h>

#ifdef __cplusplus
extern "C" {
#endif

/* posix_spawnattr flags. */
#define POSIX_SPAWN_RESETIDS      0x01
#define POSIX_SPAWN_SETPGROUP     0x02
#define POSIX_SPAWN_SETSIGDEF     0x04
#define POSIX_SPAWN_SETSIGMASK    0x08
#define POSIX_SPAWN_SETSCHEDPARAM 0x10
#define POSIX_SPAWN_SETSCHEDULER  0x20
#define POSIX_SPAWN_USEVFORK      0x40
#define POSIX_SPAWN_SETSID        0x80

typedef struct {
    short              __flags;
    pid_t              __pgrp;
    sigset_t           __sd;        /* SETSIGDEF set */
    sigset_t           __ss;        /* SETSIGMASK set */
    int                __policy;
    struct sched_param __sp;
} posix_spawnattr_t;

typedef struct {
    void  *__actions;   /* opaque: struct __spawn_action * */
    int    __n;
    int    __cap;
} posix_spawn_file_actions_t;

int posix_spawn(pid_t *__restrict pid, const char *__restrict path,
                const posix_spawn_file_actions_t *file_actions,
                const posix_spawnattr_t *__restrict attrp,
                char *const argv[__restrict], char *const envp[__restrict]);
int posix_spawnp(pid_t *__restrict pid, const char *__restrict file,
                 const posix_spawn_file_actions_t *file_actions,
                 const posix_spawnattr_t *__restrict attrp,
                 char *const argv[__restrict], char *const envp[__restrict]);

int posix_spawnattr_init(posix_spawnattr_t *attr);
int posix_spawnattr_destroy(posix_spawnattr_t *attr);
int posix_spawnattr_getflags(const posix_spawnattr_t *__restrict attr,
                             short *__restrict flags);
int posix_spawnattr_setflags(posix_spawnattr_t *attr, short flags);
int posix_spawnattr_getpgroup(const posix_spawnattr_t *__restrict attr,
                              pid_t *__restrict pgroup);
int posix_spawnattr_setpgroup(posix_spawnattr_t *attr, pid_t pgroup);
int posix_spawnattr_getsigmask(const posix_spawnattr_t *__restrict attr,
                               sigset_t *__restrict sigmask);
int posix_spawnattr_setsigmask(posix_spawnattr_t *__restrict attr,
                               const sigset_t *__restrict sigmask);
int posix_spawnattr_getsigdefault(const posix_spawnattr_t *__restrict attr,
                                  sigset_t *__restrict sigdefault);
int posix_spawnattr_setsigdefault(posix_spawnattr_t *__restrict attr,
                                  const sigset_t *__restrict sigdefault);
int posix_spawnattr_getschedpolicy(const posix_spawnattr_t *__restrict attr,
                                   int *__restrict policy);
int posix_spawnattr_setschedpolicy(posix_spawnattr_t *attr, int policy);
int posix_spawnattr_getschedparam(const posix_spawnattr_t *__restrict attr,
                                  struct sched_param *__restrict param);
int posix_spawnattr_setschedparam(posix_spawnattr_t *__restrict attr,
                                  const struct sched_param *__restrict param);

int posix_spawn_file_actions_init(posix_spawn_file_actions_t *file_actions);
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *file_actions);
int posix_spawn_file_actions_addopen(
        posix_spawn_file_actions_t *__restrict file_actions, int fildes,
        const char *__restrict path, int oflag, mode_t mode);
int posix_spawn_file_actions_addclose(
        posix_spawn_file_actions_t *file_actions, int fildes);
int posix_spawn_file_actions_adddup2(
        posix_spawn_file_actions_t *file_actions, int fildes, int newfildes);
int posix_spawn_file_actions_addtcsetpgrp_np(
        posix_spawn_file_actions_t *file_actions, int fildes);

#ifdef __cplusplus
}
#endif
#endif /* _SPAWN_H */
