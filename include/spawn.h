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

int posix_spawn(pid_t *restrict pid, const char *restrict path,
                const posix_spawn_file_actions_t *file_actions,
                const posix_spawnattr_t *restrict attrp,
                char *const argv[restrict], char *const envp[restrict]);
int posix_spawnp(pid_t *restrict pid, const char *restrict file,
                 const posix_spawn_file_actions_t *file_actions,
                 const posix_spawnattr_t *restrict attrp,
                 char *const argv[restrict], char *const envp[restrict]);

int posix_spawnattr_init(posix_spawnattr_t *attr);
int posix_spawnattr_destroy(posix_spawnattr_t *attr);
int posix_spawnattr_getflags(const posix_spawnattr_t *restrict attr,
                             short *restrict flags);
int posix_spawnattr_setflags(posix_spawnattr_t *attr, short flags);
int posix_spawnattr_getpgroup(const posix_spawnattr_t *restrict attr,
                              pid_t *restrict pgroup);
int posix_spawnattr_setpgroup(posix_spawnattr_t *attr, pid_t pgroup);
int posix_spawnattr_getsigmask(const posix_spawnattr_t *restrict attr,
                               sigset_t *restrict sigmask);
int posix_spawnattr_setsigmask(posix_spawnattr_t *restrict attr,
                               const sigset_t *restrict sigmask);
int posix_spawnattr_getsigdefault(const posix_spawnattr_t *restrict attr,
                                  sigset_t *restrict sigdefault);
int posix_spawnattr_setsigdefault(posix_spawnattr_t *restrict attr,
                                  const sigset_t *restrict sigdefault);
int posix_spawnattr_getschedpolicy(const posix_spawnattr_t *restrict attr,
                                   int *restrict policy);
int posix_spawnattr_setschedpolicy(posix_spawnattr_t *attr, int policy);
int posix_spawnattr_getschedparam(const posix_spawnattr_t *restrict attr,
                                  struct sched_param *restrict param);
int posix_spawnattr_setschedparam(posix_spawnattr_t *restrict attr,
                                  const struct sched_param *restrict param);

int posix_spawn_file_actions_init(posix_spawn_file_actions_t *file_actions);
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *file_actions);
int posix_spawn_file_actions_addopen(
        posix_spawn_file_actions_t *restrict file_actions, int fildes,
        const char *restrict path, int oflag, mode_t mode);
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
