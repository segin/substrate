/*
 * spawn.c — POSIX.1 posix_spawn / posix_spawnp.
 *
 * substrate has no dedicated kernel spawn primitive, so this is the classic
 * fork-then-exec implementation: the child applies the requested attributes
 * (process group / session / signal mask / default dispositions) and the
 * ordered file actions, then execs.  Any failure before exec makes the child
 * _exit(127), which the parent observes via the normal wait path.
 */

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

enum spawn_act_type { SPAWN_OPEN, SPAWN_CLOSE, SPAWN_DUP2, SPAWN_TCSETPGRP };

struct __spawn_action {
    enum spawn_act_type type;
    int    fd;
    int    newfd;
    int    oflag;
    mode_t mode;
    char  *path;
};

/* ---- attributes ---- */

int posix_spawnattr_init(posix_spawnattr_t *attr)
{
    memset(attr, 0, sizeof(*attr));
    return 0;
}

int posix_spawnattr_destroy(posix_spawnattr_t *attr)
{
    (void)attr;
    return 0;
}

int posix_spawnattr_getflags(const posix_spawnattr_t *restrict attr,
                             short *restrict flags)
{
    *flags = attr->__flags;
    return 0;
}

int posix_spawnattr_setflags(posix_spawnattr_t *attr, short flags)
{
    attr->__flags = flags;
    return 0;
}

int posix_spawnattr_getpgroup(const posix_spawnattr_t *restrict attr,
                              pid_t *restrict pgroup)
{
    *pgroup = attr->__pgrp;
    return 0;
}

int posix_spawnattr_setpgroup(posix_spawnattr_t *attr, pid_t pgroup)
{
    attr->__pgrp = pgroup;
    return 0;
}

int posix_spawnattr_getsigmask(const posix_spawnattr_t *restrict attr,
                               sigset_t *restrict sigmask)
{
    *sigmask = attr->__ss;
    return 0;
}

int posix_spawnattr_setsigmask(posix_spawnattr_t *restrict attr,
                               const sigset_t *restrict sigmask)
{
    attr->__ss = *sigmask;
    return 0;
}

int posix_spawnattr_getsigdefault(const posix_spawnattr_t *restrict attr,
                                  sigset_t *restrict sigdefault)
{
    *sigdefault = attr->__sd;
    return 0;
}

int posix_spawnattr_setsigdefault(posix_spawnattr_t *restrict attr,
                                  const sigset_t *restrict sigdefault)
{
    attr->__sd = *sigdefault;
    return 0;
}

int posix_spawnattr_getschedpolicy(const posix_spawnattr_t *restrict attr,
                                   int *restrict policy)
{
    *policy = attr->__policy;
    return 0;
}

int posix_spawnattr_setschedpolicy(posix_spawnattr_t *attr, int policy)
{
    attr->__policy = policy;
    return 0;
}

int posix_spawnattr_getschedparam(const posix_spawnattr_t *restrict attr,
                                  struct sched_param *restrict param)
{
    *param = attr->__sp;
    return 0;
}

int posix_spawnattr_setschedparam(posix_spawnattr_t *restrict attr,
                                  const struct sched_param *restrict param)
{
    attr->__sp = *param;
    return 0;
}

/* ---- file actions ---- */

static int fa_push(posix_spawn_file_actions_t *fa, const struct __spawn_action *a)
{
    struct __spawn_action *arr = fa->__actions;
    if (fa->__n == fa->__cap) {
        int ncap = fa->__cap ? fa->__cap * 2 : 8;
        struct __spawn_action *na = realloc(arr, ncap * sizeof(*na));
        if (!na) return ENOMEM;
        fa->__actions = na;
        fa->__cap = ncap;
        arr = na;
    }
    arr[fa->__n++] = *a;
    return 0;
}

int posix_spawn_file_actions_init(posix_spawn_file_actions_t *fa)
{
    memset(fa, 0, sizeof(*fa));
    return 0;
}

int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *fa)
{
    struct __spawn_action *arr = fa->__actions;
    for (int i = 0; i < fa->__n; i++)
        if (arr[i].type == SPAWN_OPEN)
            free(arr[i].path);
    free(arr);
    memset(fa, 0, sizeof(*fa));
    return 0;
}

int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t *restrict fa,
                                     int fildes, const char *restrict path,
                                     int oflag, mode_t mode)
{
    if (fildes < 0) return EBADF;
    struct __spawn_action a;
    memset(&a, 0, sizeof(a));
    a.type = SPAWN_OPEN; a.fd = fildes; a.oflag = oflag; a.mode = mode;
    a.path = strdup(path);
    if (!a.path) return ENOMEM;
    int r = fa_push(fa, &a);
    if (r) free(a.path);
    return r;
}

int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *fa, int fildes)
{
    if (fildes < 0) return EBADF;
    struct __spawn_action a;
    memset(&a, 0, sizeof(a));
    a.type = SPAWN_CLOSE; a.fd = fildes;
    return fa_push(fa, &a);
}

int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *fa,
                                     int fildes, int newfildes)
{
    if (fildes < 0 || newfildes < 0) return EBADF;
    struct __spawn_action a;
    memset(&a, 0, sizeof(a));
    a.type = SPAWN_DUP2; a.fd = fildes; a.newfd = newfildes;
    return fa_push(fa, &a);
}

int posix_spawn_file_actions_addtcsetpgrp_np(posix_spawn_file_actions_t *fa,
                                             int fildes)
{
    if (fildes < 0) return EBADF;
    struct __spawn_action a;
    memset(&a, 0, sizeof(a));
    a.type = SPAWN_TCSETPGRP; a.fd = fildes;
    return fa_push(fa, &a);
}

/* ---- the spawn itself ---- */

static int spawn_child_setup(const posix_spawn_file_actions_t *fa,
                             const posix_spawnattr_t *attr)
{
    if (attr) {
        short f = attr->__flags;
        if (f & POSIX_SPAWN_SETSID)
            (void)setsid();
        if (f & POSIX_SPAWN_SETPGROUP)
            if (setpgid(0, attr->__pgrp) != 0) return errno;
        if (f & POSIX_SPAWN_SETSIGDEF) {
            struct sigaction sa;
            memset(&sa, 0, sizeof(sa));
            sa.sa_handler = SIG_DFL;
            for (int s = 1; s < NSIG; s++)
                if (sigismember(&attr->__sd, s))
                    sigaction(s, &sa, NULL);
        }
        if (f & POSIX_SPAWN_SETSIGMASK)
            sigprocmask(SIG_SETMASK, &attr->__ss, NULL);
        if (f & POSIX_SPAWN_SETSCHEDPARAM)
            (void)sched_setparam(0, &attr->__sp);
        if (f & POSIX_SPAWN_SETSCHEDULER)
            (void)sched_setscheduler(0, attr->__policy, &attr->__sp);
    }

    if (fa) {
        struct __spawn_action *arr = fa->__actions;
        for (int i = 0; i < fa->__n; i++) {
            struct __spawn_action *a = &arr[i];
            switch (a->type) {
            case SPAWN_OPEN: {
                int fd = open(a->path, a->oflag, a->mode);
                if (fd < 0) return errno;
                if (fd != a->fd) {
                    if (dup2(fd, a->fd) < 0) { int e = errno; close(fd); return e; }
                    close(fd);
                }
                break;
            }
            case SPAWN_CLOSE:
                if (close(a->fd) < 0 && errno != EBADF) return errno;
                break;
            case SPAWN_DUP2:
                if (dup2(a->fd, a->newfd) < 0) return errno;
                break;
            case SPAWN_TCSETPGRP:
                (void)tcsetpgrp(a->fd, getpgrp());
                break;
            }
        }
    }
    return 0;
}

static int do_spawn(pid_t *pid, const char *path,
                    const posix_spawn_file_actions_t *fa,
                    const posix_spawnattr_t *attr,
                    char *const argv[], char *const envp[], int use_path)
{
    pid_t child = fork();
    if (child < 0)
        return errno;
    if (child == 0) {
        int err = spawn_child_setup(fa, attr);
        if (err == 0) {
            if (use_path)
                execvp(path, argv);
            else
                execve(path, argv, envp ? envp : environ);
            err = errno;
        }
        _exit(127);
        (void)err;
    }
    if (pid)
        *pid = child;
    return 0;
}

int posix_spawn(pid_t *restrict pid, const char *restrict path,
                const posix_spawn_file_actions_t *fa,
                const posix_spawnattr_t *restrict attr,
                char *const argv[restrict], char *const envp[restrict])
{
    return do_spawn(pid, path, fa, attr, argv, envp, 0);
}

int posix_spawnp(pid_t *restrict pid, const char *restrict file,
                 const posix_spawn_file_actions_t *fa,
                 const posix_spawnattr_t *restrict attr,
                 char *const argv[restrict], char *const envp[restrict])
{
    return do_spawn(pid, file, fa, attr, argv, envp, 1);
}
