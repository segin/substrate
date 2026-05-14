/*
 * lib/kvm/kvm.c — BSD libkvm source-compatibility shim, implemented
 * entirely in userspace using libsys.  See <kvm.h> for the design
 * goals and what's deliberately unsupported.
 *
 * All entry points share a kvm_t descriptor that owns:
 *
 *   - errstr[]    — last error message; kvm_geterr() returns this.
 *   - procs       — cached kinfo_proc array from kvm_getprocs() (so
 *                   the BSD convention of "pointer valid until next
 *                   call" works).  Reallocated as needed.
 *   - argv/envv   — char**-and-flat-buffer pair from the last
 *                   kvm_getargv() / kvm_getenvv() respectively.
 *   - vmmap/files — same lifetime model as procs for the per-proc
 *                   VM mapping and open-file lists.
 *
 * kvm_open() doesn't actually open anything on substrate today —
 * we never look at /dev/kmem or a kernel core.  It just allocates
 * the descriptor.  This matches FreeBSD-the-library's contract:
 * callers always check for NULL and rely on the live kernel
 * giving them sane answers via the indirection.
 */

#include <kvm.h>
#include <sys/sysinfo.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#define ERRSTR_MAX KVM_ERRSTR_SIZE

struct __kvm {
    char  errstr[ERRSTR_MAX];
    char *errbuf;            /* points to errstr OR the caller's
                              * buffer from kvm_openfiles(errbuf=...) */
    size_t errbuf_size;

    /* Per-call caches.  Cleared on each new call to the matching
     * entry point.  Freed on kvm_close. */
    struct kinfo_proc    *procs;
    int                   procs_cnt;

    char                **argv;        /* NULL-terminated */
    char                 *argv_flat;   /* backing storage for argv strings */

    char                **envv;
    char                 *envv_flat;

    struct kinfo_vmentry *vmmap;
    int                   vmmap_cnt;

    struct kinfo_file    *files;
    int                   files_cnt;
};

/* Format an error into kd's buffer (which may be the caller's
 * buffer from kvm_openfiles() or our internal errstr).  vsnprintf
 * truncates if the buffer is shorter than KVM_ERRSTR_SIZE. */
static void
kvm_seterr(kvm_t *kd, const char *fmt, ...)
{
    if (!kd) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(kd->errbuf, kd->errbuf_size, fmt, ap);
    va_end(ap);
}

/* ===================================================================
 * Lifecycle
 * =================================================================== */
kvm_t *
kvm_open(const char *execfile, const char *corefile,
         const char *swapfile, int flags, const char *errstr)
{
    /* errstr is the program name to prefix error messages with —
     * BSD convention.  We just ignore it; the substrate impl
     * doesn't print, only buffers. */
    (void)errstr;
    return kvm_openfiles(execfile, corefile, swapfile, flags, NULL);
}

kvm_t *
kvm_openfiles(const char *execfile, const char *corefile,
              const char *swapfile, int flags, char *errbuf)
{
    /* Substrate has no /dev/kmem and no kernel-core inspector;
     * refuse if the caller asked for one. */
    if (execfile != NULL || corefile != NULL || swapfile != NULL) {
        if (errbuf) {
            snprintf(errbuf, ERRSTR_MAX,
                     "kvm_openfiles: substrate libkvm is live-only "
                     "(no exec/core/swap inspection)");
        }
        return NULL;
    }
    (void)flags;

    kvm_t *kd = calloc(1, sizeof(*kd));
    if (!kd) {
        if (errbuf) {
            snprintf(errbuf, ERRSTR_MAX, "kvm_openfiles: out of memory");
        }
        return NULL;
    }
    if (errbuf) {
        kd->errbuf = errbuf;
        kd->errbuf_size = ERRSTR_MAX;
    } else {
        kd->errbuf = kd->errstr;
        kd->errbuf_size = sizeof(kd->errstr);
    }
    return kd;
}

int
kvm_close(kvm_t *kd)
{
    if (!kd) return 0;
    free(kd->procs);
    free(kd->argv); free(kd->argv_flat);
    free(kd->envv); free(kd->envv_flat);
    free(kd->vmmap);
    free(kd->files);
    free(kd);
    return 0;
}

char *
kvm_geterr(kvm_t *kd)
{
    if (!kd) return (char *)"kvm_geterr: NULL descriptor";
    return kd->errbuf;
}

/* ===================================================================
 * Kernel memory access — deliberately stubbed.
 * =================================================================== */
ssize_t
kvm_read(kvm_t *kd, unsigned long addr, void *buf, size_t len)
{
    (void)addr; (void)buf; (void)len;
    kvm_seterr(kd, "kvm_read: no /dev/kmem on substrate");
    return -1;
}

ssize_t
kvm_write(kvm_t *kd, unsigned long addr, const void *buf, size_t len)
{
    (void)addr; (void)buf; (void)len;
    kvm_seterr(kd, "kvm_write: kernel memory writes not permitted");
    return -1;
}

/* ===================================================================
 * kvm_nlist — best-effort against /proc/kallsyms.
 *
 * /proc/kallsyms format: "%lx %c %s\n", repeated.  We do a linear
 * scan for each requested symbol; this is O(N * M) but kvm_nlist
 * is rarely called with N > 10 and M (kallsyms line count) is a
 * few thousand on substrate.  Returns 0 if every entry resolved,
 * else the number of unresolved entries (BSD convention).
 * =================================================================== */
int
kvm_nlist(kvm_t *kd, struct nlist *nl)
{
    if (!kd || !nl) return -1;

    int unresolved = 0;
    for (struct nlist *p = nl; p->n_name != NULL; p++) {
        p->n_type = 0;
        p->n_value = 0;
        unresolved++;
    }
    if (unresolved == 0) {
        return 0;
    }

    FILE *f = fopen("/proc/kallsyms", "r");
    if (!f) {
        kvm_seterr(kd, "kvm_nlist: /proc/kallsyms unavailable");
        return unresolved;
    }

    char line[256];
    while (fgets(line, sizeof(line), f) != NULL && unresolved > 0) {
        unsigned long addr;
        char type;
        char name[128];
        if (sscanf(line, "%lx %c %127s", &addr, &type, name) != 3) {
            continue;
        }
        for (struct nlist *p = nl; p->n_name != NULL; p++) {
            if (p->n_value != 0) continue;
            if (strcmp(name, p->n_name) == 0) {
                p->n_value = addr;
                p->n_type  = (unsigned char)type;
                unresolved--;
                break;
            }
        }
    }
    fclose(f);
    return unresolved;
}

/* ===================================================================
 * Process enumeration
 * =================================================================== */
static void
sys_to_kinfo(const sys_procinfo_t *si, struct kinfo_proc *kp)
{
    memset(kp, 0, sizeof(*kp));
    kp->ki_structsize = sizeof(*kp);
    kp->ki_pid    = si->pid;
    kp->ki_ppid   = si->ppid;
    kp->ki_pgid   = si->pgid;
    kp->ki_sid    = si->sid;
    kp->ki_uid    = si->euid;
    kp->ki_ruid   = si->uid;
    kp->ki_rgid   = si->gid;
    kp->ki_nice   = (int)si->nice - 20;       /* BSD nice is signed */
    kp->ki_tdev   = si->tty;
    kp->ki_start  = si->start_time;
    kp->ki_utime  = si->user_time;
    kp->ki_stime  = si->sys_time;
    kp->ki_runtime = si->user_time + si->sys_time;
    kp->ki_size   = si->vsize;
    kp->ki_rssize = si->rss;
    kp->ki_perso  = si->perso_id;
    kp->ki_bitness = si->bitness;

    /* state codes: substrate uses 'R'/'S'/'Z'/'T' as BSD does. */
    kp->ki_stat = (char)si->state;
    kp->ki_state[0] = (char)si->state;
    kp->ki_state[1] = '\0';

    /* comm — copy short name, NUL-terminate */
    size_t n = strnlen(si->name, sizeof(si->name));
    if (n >= sizeof(kp->ki_comm)) n = sizeof(kp->ki_comm) - 1;
    memcpy(kp->ki_comm, si->name, n);
    kp->ki_comm[n] = '\0';
}

static int
proc_match(const sys_procinfo_t *si, int op, int arg)
{
    switch (op) {
    case KERN_PROC_ALL:     return 1;
    case KERN_PROC_PID:     return si->pid == arg;
    case KERN_PROC_PGRP:    return si->pgid == arg;
    case KERN_PROC_SESSION: return si->sid == arg;
    case KERN_PROC_TTY:     return si->tty == arg;
    case KERN_PROC_UID:     return (int)si->euid == arg;
    case KERN_PROC_RUID:    return (int)si->uid == arg;
    default:                return 0;
    }
}

struct kinfo_proc *
kvm_getprocs(kvm_t *kd, int op, int arg, int *cnt)
{
    if (!kd) {
        if (cnt) *cnt = 0;
        return NULL;
    }
    /* Free any cached array; BSD says the pointer is invalidated. */
    free(kd->procs);
    kd->procs = NULL;
    kd->procs_cnt = 0;

    int total = sys_proc_count();
    if (total <= 0) {
        kvm_seterr(kd, "kvm_getprocs: sys_proc_count returned %d", total);
        if (cnt) *cnt = 0;
        return NULL;
    }

    pid_t *pids = calloc((size_t)total, sizeof(*pids));
    if (!pids) {
        kvm_seterr(kd, "kvm_getprocs: out of memory");
        if (cnt) *cnt = 0;
        return NULL;
    }
    int got = sys_proc_list(pids, (size_t)total);
    if (got < 0) {
        kvm_seterr(kd, "kvm_getprocs: sys_proc_list failed");
        free(pids);
        if (cnt) *cnt = 0;
        return NULL;
    }

    struct kinfo_proc *out = calloc((size_t)got, sizeof(*out));
    if (!out) {
        free(pids);
        kvm_seterr(kd, "kvm_getprocs: out of memory");
        if (cnt) *cnt = 0;
        return NULL;
    }

    int n = 0;
    for (int i = 0; i < got; i++) {
        sys_procinfo_t si;
        if (sys_proc_info(pids[i], &si) != 0) {
            continue;                /* process exited between calls — skip */
        }
        if (!proc_match(&si, op, arg)) {
            continue;
        }
        sys_to_kinfo(&si, &out[n++]);
    }
    free(pids);

    kd->procs = out;
    kd->procs_cnt = n;
    if (cnt) *cnt = n;
    return out;
}

struct kinfo_proc *
kvm_getproc2(kvm_t *kd, int op, int arg, size_t elem_size, int *cnt)
{
    if (elem_size != sizeof(struct kinfo_proc)) {
        kvm_seterr(kd, "kvm_getproc2: elem_size %zu != %zu",
                   elem_size, sizeof(struct kinfo_proc));
        if (cnt) *cnt = 0;
        return NULL;
    }
    return kvm_getprocs(kd, op, arg, cnt);
}

/* ===================================================================
 * argv / envv
 *
 * sys_proc_cmdline returns an array of char* pointers and a count.
 * It allocates its own storage; we copy into a flat backing buffer
 * we own so the BSD "lifetime until next call" contract holds.
 * =================================================================== */
static char **
fetch_strings(kvm_t *kd, pid_t pid, int (*fetch)(pid_t, char **, size_t *),
              char ***out_argv_field, char **out_flat_field,
              int nchr, const char *errctx)
{
    /* Always free any previous cache; BSD invalidates the pointer
     * on subsequent calls. */
    free(*out_argv_field);
    free(*out_flat_field);
    *out_argv_field = NULL;
    *out_flat_field = NULL;

    /* First probe the count; sys_proc_cmdline/environ both accept
     * NULL argv but require a non-NULL argc. */
    size_t cnt = 0;
    if (fetch(pid, NULL, &cnt) != 0) {
        kvm_seterr(kd, "%s: probe failed for pid %d", errctx, (int)pid);
        return NULL;
    }
    if (cnt == 0) {
        /* still need a one-entry NULL-terminated argv */
        char **argv = calloc(1, sizeof(char *));
        *out_argv_field = argv;
        return argv;
    }

    char **argv = calloc(cnt + 1, sizeof(char *));
    if (!argv) {
        kvm_seterr(kd, "%s: out of memory", errctx);
        return NULL;
    }
    if (fetch(pid, argv, &cnt) != 0) {
        kvm_seterr(kd, "%s: fetch failed for pid %d", errctx, (int)pid);
        free(argv);
        return NULL;
    }

    /* sys_proc_cmdline returns pointers into its own heap.  Copy
     * each string into our flat buffer, then redirect argv[i] to
     * the copies. */
    size_t total = 0;
    for (size_t i = 0; i < cnt; i++) {
        total += strlen(argv[i]) + 1;
    }
    if (nchr > 0 && (size_t)nchr < total) {
        total = (size_t)nchr;
    }

    char *flat = malloc(total + 1);
    if (!flat) {
        free(argv);
        kvm_seterr(kd, "%s: out of memory", errctx);
        return NULL;
    }
    char *p = flat;
    char *limit = flat + total;
    for (size_t i = 0; i < cnt; i++) {
        size_t l = strlen(argv[i]) + 1;
        if (p + l > limit) {
            /* truncate */
            size_t room = limit > p ? (size_t)(limit - p) : 0;
            if (room == 0) break;
            memcpy(p, argv[i], room - 1);
            p[room - 1] = '\0';
            argv[i] = p;
            p += room;
            argv[++i] = NULL;
            cnt = i;
            break;
        }
        memcpy(p, argv[i], l);
        argv[i] = p;
        p += l;
    }
    *limit = '\0';
    argv[cnt] = NULL;

    *out_argv_field = argv;
    *out_flat_field = flat;
    return argv;
}

char **
kvm_getargv(kvm_t *kd, const struct kinfo_proc *p, int nchr)
{
    if (!kd || !p) return NULL;
    return fetch_strings(kd, p->ki_pid, sys_proc_cmdline,
                         &kd->argv, &kd->argv_flat, nchr,
                         "kvm_getargv");
}

char **
kvm_getenvv(kvm_t *kd, const struct kinfo_proc *p, int nchr)
{
    if (!kd || !p) return NULL;
    return fetch_strings(kd, p->ki_pid, sys_proc_environ,
                         &kd->envv, &kd->envv_flat, nchr,
                         "kvm_getenvv");
}

/* ===================================================================
 * VM map + open files
 * =================================================================== */
struct kinfo_vmentry *
kvm_getvmmap(kvm_t *kd, const struct kinfo_proc *p, int *cnt)
{
    if (!kd || !p) {
        if (cnt) *cnt = 0;
        return NULL;
    }
    free(kd->vmmap);
    kd->vmmap = NULL;
    kd->vmmap_cnt = 0;

    size_t count = 0;
    if (sys_proc_maps(p->ki_pid, NULL, &count) != 0 || count == 0) {
        if (cnt) *cnt = 0;
        return NULL;
    }
    sys_map_t *maps = calloc(count, sizeof(*maps));
    if (!maps) {
        kvm_seterr(kd, "kvm_getvmmap: out of memory");
        if (cnt) *cnt = 0;
        return NULL;
    }
    if (sys_proc_maps(p->ki_pid, maps, &count) != 0) {
        free(maps);
        kvm_seterr(kd, "kvm_getvmmap: sys_proc_maps failed");
        if (cnt) *cnt = 0;
        return NULL;
    }

    struct kinfo_vmentry *out = calloc(count, sizeof(*out));
    if (!out) {
        free(maps);
        kvm_seterr(kd, "kvm_getvmmap: out of memory");
        if (cnt) *cnt = 0;
        return NULL;
    }
    for (size_t i = 0; i < count; i++) {
        out[i].kve_structsize = sizeof(out[i]);
        out[i].kve_start = maps[i].start;
        out[i].kve_end   = maps[i].end;
        out[i].kve_flags = maps[i].flags;
        /* sys_map_t::flags lower three bits already follow PROT_* */
        out[i].kve_protection = maps[i].flags & 7;
        size_t n = strnlen(maps[i].name, sizeof(maps[i].name));
        if (n >= sizeof(out[i].kve_path)) n = sizeof(out[i].kve_path) - 1;
        memcpy(out[i].kve_path, maps[i].name, n);
        out[i].kve_path[n] = '\0';
    }
    free(maps);

    kd->vmmap = out;
    kd->vmmap_cnt = (int)count;
    if (cnt) *cnt = (int)count;
    return out;
}

struct kinfo_file *
kvm_getfiles(kvm_t *kd, int op, int arg, int *cnt)
{
    if (!kd) {
        if (cnt) *cnt = 0;
        return NULL;
    }
    /* The only KERN_FILE_BYPID-style we support is "all files of a
     * given pid" — op == KERN_PROC_PID with arg = pid.  FreeBSD's
     * full surface is broader; that's a later iteration. */
    if (op != KERN_PROC_PID) {
        kvm_seterr(kd, "kvm_getfiles: only KERN_PROC_PID is supported");
        if (cnt) *cnt = 0;
        return NULL;
    }

    free(kd->files);
    kd->files = NULL;
    kd->files_cnt = 0;

    size_t count = 0;
    if (sys_proc_fds(arg, NULL, &count) != 0 || count == 0) {
        if (cnt) *cnt = 0;
        return NULL;
    }
    sys_fd_t *fds = calloc(count, sizeof(*fds));
    if (!fds) {
        kvm_seterr(kd, "kvm_getfiles: out of memory");
        if (cnt) *cnt = 0;
        return NULL;
    }
    if (sys_proc_fds(arg, fds, &count) != 0) {
        free(fds);
        kvm_seterr(kd, "kvm_getfiles: sys_proc_fds failed");
        if (cnt) *cnt = 0;
        return NULL;
    }

    struct kinfo_file *out = calloc(count, sizeof(*out));
    if (!out) {
        free(fds);
        kvm_seterr(kd, "kvm_getfiles: out of memory");
        if (cnt) *cnt = 0;
        return NULL;
    }
    for (size_t i = 0; i < count; i++) {
        out[i].kf_structsize = sizeof(out[i]);
        out[i].kf_fd     = fds[i].fd;
        out[i].kf_type   = KF_TYPE_VNODE;     /* no introspection yet */
        out[i].kf_flags  = (int)fds[i].flags;
        size_t n = strnlen(fds[i].path, sizeof(fds[i].path));
        if (n >= sizeof(out[i].kf_path)) n = sizeof(out[i].kf_path) - 1;
        memcpy(out[i].kf_path, fds[i].path, n);
        out[i].kf_path[n] = '\0';
    }
    free(fds);

    kd->files = out;
    kd->files_cnt = (int)count;
    if (cnt) *cnt = (int)count;
    return out;
}
