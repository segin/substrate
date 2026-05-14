/*
 * lib/proc/readproc.c — procps-ng userspace shim.  See
 * <proc/readproc.h> for the design notes.
 *
 * Two layers:
 *
 *   1. openproc / readproc / closeproc — process iterator.
 *      Backed by sys_proc_list + sys_proc_info.  Filtering by
 *      PROC_PID / PROC_UID happens after the sys_proc_info
 *      fetch (cheap; we already have the data).  PROC_FILL*
 *      flags decide whether we go through the more expensive
 *      sys_proc_cmdline / sys_proc_environ paths.
 *
 *   2. meminfo / cpuinfo / uptime / loadavg — file scanners that
 *      read the corresponding /proc entries with stdio.  No state
 *      between calls; safe to call from anywhere.
 *
 * Memory ownership for proc_t::cmdline and proc_t::environ:
 * readproc() allocates the argv/envv pointer arrays and a single
 * flat backing buffer per call.  The previous call's allocation is
 * freed on the next readproc() for that same proc_t — caller MUST
 * NOT free them by hand.  closeproc() frees the iterator but
 * does NOT touch proc_t arrays the caller has stashed; the
 * convention there matches procps-ng.
 */

#include <proc/readproc.h>
#include <sys/sysinfo.h>
#include <stdarg.h>

/* The kernel-side sys/sysinfo.h shadows the userland one under
 * -nostdinc with -Isys/include earlier in the path; declare the
 * libsys entry point we need rather than play header-order games. */
extern int sys_cpu_count(void);

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>

/* ===================================================================
 * Iterator
 * =================================================================== */
PROCTAB *
openproc(int flags, ...)
{
    PROCTAB *pt = calloc(1, sizeof(*pt));
    if (!pt) return NULL;
    pt->flags = flags;
    pt->cursor = 0;

    /* Variadic args: present for PROC_PID and PROC_UID.  procps-ng
     * defines the order as "pid list" if PROC_PID set, "uid list"
     * + count if PROC_UID set.  We accept them in that order. */
    va_list ap;
    va_start(ap, flags);
    if (flags & PROC_PID) {
        pid_t *src = va_arg(ap, pid_t *);
        int n = 0;
        if (src) while (src[n] != 0) n++;
        pt->pid_list = calloc((size_t)n + 1, sizeof(pid_t));
        if (pt->pid_list && src) memcpy(pt->pid_list, src, (size_t)n * sizeof(pid_t));
    }
    if (flags & PROC_UID) {
        uid_t *src = va_arg(ap, uid_t *);
        int n = va_arg(ap, int);
        if (n > 0) {
            pt->uid_list = calloc((size_t)n, sizeof(uid_t));
            if (pt->uid_list && src) memcpy(pt->uid_list, src, (size_t)n * sizeof(uid_t));
            pt->uid_list_n = n;
        }
    }
    va_end(ap);

    /* Snapshot the current process list.  Re-fetched per openproc;
     * readproc walks through it. */
    int total = sys_proc_count();
    if (total < 0) total = 0;
    pt->snapshot = calloc((size_t)total + 1, sizeof(pid_t));
    if (!pt->snapshot) {
        closeproc(pt);
        return NULL;
    }
    int got = sys_proc_list(pt->snapshot, (size_t)total);
    pt->snapshot_n = got > 0 ? got : 0;

    return pt;
}

void
closeproc(PROCTAB *pt)
{
    if (!pt) return;
    free(pt->pid_list);
    free(pt->uid_list);
    free(pt->snapshot);
    free(pt);
}

/* Filter a sys_procinfo_t through the iterator's PROC_PID /
 * PROC_UID lists.  Returns 1 if it matches, 0 if it should be
 * skipped. */
static int
filter_proc(const PROCTAB *pt, const sys_procinfo_t *si)
{
    if (pt->pid_list) {
        int matched = 0;
        for (int i = 0; pt->pid_list[i] != 0; i++) {
            if (pt->pid_list[i] == si->pid) { matched = 1; break; }
        }
        if (!matched) return 0;
    }
    if (pt->uid_list && pt->uid_list_n > 0) {
        int matched = 0;
        for (int i = 0; i < pt->uid_list_n; i++) {
            if (pt->uid_list[i] == si->euid) { matched = 1; break; }
        }
        if (!matched) return 0;
    }
    return 1;
}

static int
fetch_strings(pid_t pid, int (*fetch)(pid_t, char **, size_t *),
              char ***out_argv)
{
    /* Free any previous result the caller had on this proc_t. */
    if (*out_argv) {
        free((*out_argv)[0]);   /* not actually safe — see below */
        free(*out_argv);
        *out_argv = NULL;
    }

    size_t cnt = 0;
    if (fetch(pid, NULL, &cnt) != 0) return -1;
    if (cnt == 0) {
        *out_argv = calloc(1, sizeof(char *));
        return *out_argv ? 0 : -1;
    }
    char **argv = calloc(cnt + 1, sizeof(char *));
    if (!argv) return -1;
    if (fetch(pid, argv, &cnt) != 0) {
        free(argv);
        return -1;
    }

    /*
     * sys_proc_cmdline/sys_proc_environ allocate each entry on the
     * caller's behalf.  To keep ownership simple we coalesce them
     * into a single flat buffer the caller frees by free()ing
     * argv[0] (per procps-ng convention).
     */
    size_t total = 0;
    for (size_t i = 0; i < cnt; i++) total += strlen(argv[i]) + 1;
    char *flat = malloc(total ? total : 1);
    if (!flat) {
        free(argv);
        return -1;
    }
    char *p = flat;
    for (size_t i = 0; i < cnt; i++) {
        size_t l = strlen(argv[i]) + 1;
        memcpy(p, argv[i], l);
        argv[i] = p;
        p += l;
    }
    argv[cnt] = NULL;
    *out_argv = argv;
    return 0;
}

static void
fill_proc(proc_t *p, const sys_procinfo_t *si)
{
    p->tid = p->tgid = si->pid;
    p->ppid = si->ppid;
    p->pgrp = si->pgid;
    p->session = si->sid;
    p->tty = si->tty;
    p->tpgid = 0;

    p->ruid = si->uid;  p->euid = si->euid; p->suid = si->uid; p->fuid = si->uid;
    p->rgid = si->gid;  p->egid = si->egid; p->sgid = si->gid; p->fgid = si->gid;

    p->state = (char)si->state;
    p->priority = 0;
    p->nice = (int)si->nice - 20;

    p->start_time = si->start_time;
    p->utime = si->user_time;
    p->stime = si->sys_time;

    p->size = si->vsize;
    p->resident = si->rss;

    size_t n = strnlen(si->name, sizeof(si->name));
    if (n >= sizeof(p->cmd)) n = sizeof(p->cmd) - 1;
    memcpy(p->cmd, si->name, n);
    p->cmd[n] = '\0';

    p->sub_perso = si->perso_id;
    p->sub_bitness = si->bitness;
}

proc_t *
readproc(PROCTAB *pt, proc_t *p)
{
    if (!pt || !p) return NULL;
    while (pt->cursor < pt->snapshot_n) {
        pid_t pid = pt->snapshot[pt->cursor++];
        sys_procinfo_t si;
        if (sys_proc_info(pid, &si) != 0) continue;
        if (!filter_proc(pt, &si)) continue;

        fill_proc(p, &si);

        if ((pt->flags & PROC_FILLARG) || (pt->flags & PROC_FILLCOM)) {
            (void)fetch_strings(pid, sys_proc_cmdline, &p->cmdline);
        }
        if (pt->flags & PROC_FILLENV) {
            (void)fetch_strings(pid, sys_proc_environ, &p->environ);
        }
        return p;
    }
    return NULL;
}

proc_t **
readproctab(int flags, ...)
{
    PROCTAB *pt;
    va_list ap;
    va_start(ap, flags);
    if (flags & PROC_PID) {
        pid_t *src = va_arg(ap, pid_t *);
        pt = openproc(flags, src);
    } else if (flags & PROC_UID) {
        uid_t *src = va_arg(ap, uid_t *);
        int n = va_arg(ap, int);
        pt = openproc(flags, src, n);
    } else {
        pt = openproc(flags);
    }
    va_end(ap);
    if (!pt) return NULL;

    proc_t **out = NULL;
    int cap = 0, n = 0;
    proc_t *slot = NULL;
    for (;;) {
        if (!slot) {
            slot = calloc(1, sizeof(*slot));
            if (!slot) break;
        }
        if (!readproc(pt, slot)) {
            free(slot);
            break;
        }
        if (n + 1 >= cap) {
            int new_cap = cap ? cap * 2 : 8;
            proc_t **nout = realloc(out, (size_t)new_cap * sizeof(*nout));
            if (!nout) { free(slot); break; }
            out = nout;
            cap = new_cap;
        }
        out[n++] = slot;
        slot = NULL;
    }
    if (out) out[n] = NULL;
    closeproc(pt);
    return out;
}

int
look_up_our_self(proc_t *p)
{
    if (!p) return -1;
    sys_procinfo_t si;
    if (sys_proc_info(getpid(), &si) != 0) return -1;
    fill_proc(p, &si);
    return 0;
}

proc_t *
get_proc_stats(pid_t pid, proc_t *p)
{
    if (!p) return NULL;
    sys_procinfo_t si;
    if (sys_proc_info(pid, &si) != 0) return NULL;
    fill_proc(p, &si);
    return p;
}

/* ===================================================================
 * System-wide accessors
 * =================================================================== */
static int
read_kv_kb(const char *path, const char *key, unsigned long *out)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[256];
    size_t keylen = strlen(key);
    int hit = -1;
    while (fgets(line, sizeof(line), f) != NULL) {
        if (strncmp(line, key, keylen) == 0 && line[keylen] == ':') {
            char *p = line + keylen + 1;
            while (*p == ' ' || *p == '\t') p++;
            *out = strtoul(p, NULL, 10);
            hit = 0;
            break;
        }
    }
    fclose(f);
    return hit;
}

int
meminfo(meminfo_t *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    /* Each field is independently optional — MemTotal is the only
     * one we treat as required for "the file is real". */
    (void)read_kv_kb("/proc/meminfo", "MemTotal",     &out->mem_total);
    (void)read_kv_kb("/proc/meminfo", "MemFree",      &out->mem_free);
    (void)read_kv_kb("/proc/meminfo", "MemAvailable", &out->mem_available);
    (void)read_kv_kb("/proc/meminfo", "Buffers",      &out->buffers);
    (void)read_kv_kb("/proc/meminfo", "Cached",       &out->cached);
    (void)read_kv_kb("/proc/meminfo", "SwapTotal",    &out->swap_total);
    (void)read_kv_kb("/proc/meminfo", "SwapFree",     &out->swap_free);
    /* MemTotal is the only field we really care about; the rest
     * are optional.  Report success if it parsed. */
    return out->mem_total > 0 ? 0 : -1;
}

int
cpuinfo(cpuinfo_t *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    out->cpus_online = sys_cpu_count();
    out->cpus_configured = out->cpus_online;

    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) {
        return out->cpus_online > 0 ? 0 : -1;
    }
    char line[256];
    while (fgets(line, sizeof(line), f) != NULL) {
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon++ = '\0';
        /* trim trailing whitespace from key */
        char *k = line;
        char *end = strchr(k, '\0') - 1;
        while (end > k && (*end == ' ' || *end == '\t')) *end-- = '\0';
        while (*colon == ' ' || *colon == '\t') colon++;
        /* trim line break */
        char *nl = strpbrk(colon, "\r\n");
        if (nl) *nl = '\0';

        if (strcmp(k, "vendor_id") == 0 && !out->vendor[0]) {
            snprintf(out->vendor, sizeof(out->vendor), "%s", colon);
        } else if (strcmp(k, "model name") == 0 && !out->model[0]) {
            snprintf(out->model, sizeof(out->model), "%s", colon);
        } else if (strcmp(k, "cpu MHz") == 0 && out->mhz == 0.0) {
            out->mhz = strtod(colon, NULL);
        }
    }
    fclose(f);
    return 0;
}

int
uptime(double *total, double *idle)
{
    FILE *f = fopen("/proc/uptime", "r");
    if (!f) return -1;
    double t = 0, i = 0;
    int got = fscanf(f, "%lf %lf", &t, &i);
    fclose(f);
    if (got < 1) return -1;
    if (total) *total = t;
    if (idle)  *idle  = i;
    return 0;
}

int
loadavg(double *one, double *five, double *fifteen)
{
    FILE *f = fopen("/proc/loadavg", "r");
    if (!f) return -1;
    double a = 0, b = 0, c = 0;
    int got = fscanf(f, "%lf %lf %lf", &a, &b, &c);
    fclose(f);
    if (got < 3) return -1;
    if (one)     *one     = a;
    if (five)    *five    = b;
    if (fifteen) *fifteen = c;
    return 0;
}
