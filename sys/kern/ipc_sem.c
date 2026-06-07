/*
 * ipc_sem.c — System V semaphores (semget/semop/semctl).
 *
 * A fixed table of SEMMNI semaphore sets, each owning a kmalloc'd array of
 * `struct ksem`.  Keys map to ids by linear scan (SEMMNI is small).  A semid
 * encodes a slot index and a per-slot sequence number so a stale id from a
 * removed-and-recreated set is rejected (EIDRM/EINVAL).
 *
 * semop() applies its whole op list atomically: a temp copy of the affected
 * semvals is advanced through the ops, and only committed if every op can
 * proceed.  If any op would block and none carry IPC_NOWAIT, the caller sleeps
 * interruptibly on the set's wait channel until another operation changes it.
 *
 * SEM_UNDO is tracked per (pid, set) and reversed at process exit via
 * sem_proc_cleanup(), called from proc_exit().
 *
 * The core (kern_sem*) works on kernel memory only and makes no assumption
 * about the userspace semid_ds/semun layout, so the native and the Linux/BSD
 * personalities can each marshal their own ABI on top of it.
 */

#include <sys/sem.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/copy.h>
#include <sys/kern_syscalls.h>
#include <vm/vm_kmem.h>
#include <kern/sleepq.h>
#include <kern/sched.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>

struct ksem {
    int   val;        /* current value */
    int   pid;        /* pid of last successful op */
    int   ncnt;       /* threads blocked waiting for val to increase */
    int   zcnt;       /* threads blocked waiting for val to reach 0 */
};

struct ksemset {
    int             in_use;
    key_t           key;
    unsigned short  seq;       /* bumped each (re)allocation of this slot */
    struct ipc_perm perm;
    int             nsems;
    struct ksem    *sems;
    time_t          otime;
    time_t          ctime;
};

struct sem_undo {
    struct sem_undo *next;
    int    pid;
    int    index;     /* slot index */
    unsigned short seq;
    int    nsems;
    short *adj;       /* [nsems]: value to ADD back at exit */
};

static struct ksemset semsets[SEMMNI];
static struct sem_undo *undo_list;
static mutex_t  sem_lock;
static int      sem_ready;

void sem_init(void)
{
    if (sem_ready)
        return;
    mutex_init(&sem_lock, "sysv_sem");
    sem_ready = 1;
}

static void sem_lazy_init(void)
{
    if (!sem_ready)
        sem_init();
}

/* id <-> slot encoding: id = seq * SEMMNI + index. */
static int sem_make_id(int index, unsigned short seq)
{
    return (int)seq * SEMMNI + index;
}

/* Resolve a semid to its in-use slot, or NULL with *err set. */
static struct ksemset *sem_lookup(int semid, int *err)
{
    if (semid < 0) { *err = EINVAL; return NULL; }
    int index = semid % SEMMNI;
    unsigned short seq = (unsigned short)(semid / SEMMNI);
    struct ksemset *s = &semsets[index];
    if (!s->in_use) { *err = EINVAL; return NULL; }
    if (s->seq != seq) { *err = EIDRM; return NULL; }
    return s;
}

/* Permission check.  want = read flag 4 / alter flag 2 (octal-style, matching
 * the low mode bits).  Root bypasses. */
static int sem_perm_ok(struct ksemset *s, int want)
{
    process_t *p = current_process;
    if (!p || p->euid == 0)
        return 1;
    mode_t mode = s->perm.mode;
    int granted;
    if (p->euid == s->perm.uid || p->euid == s->perm.cuid)
        granted = (mode >> 6) & 7;
    else if (p->egid == s->perm.gid || p->egid == s->perm.cgid)
        granted = (mode >> 3) & 7;
    else
        granted = mode & 7;
    return (granted & want) == want;
}

static time_t sem_now(void)
{
    return kern_time(NULL);
}

/* ---- SEM_UNDO bookkeeping (caller holds sem_lock) ---- */

static struct sem_undo *sem_undo_find(int pid, int index, unsigned short seq,
                                      int nsems, int create)
{
    struct sem_undo *u;
    for (u = undo_list; u; u = u->next)
        if (u->pid == pid && u->index == index && u->seq == seq)
            return u;
    if (!create)
        return NULL;
    u = (struct sem_undo *)kmalloc(sizeof(*u));
    if (!u)
        return NULL;
    u->adj = (short *)kmalloc(sizeof(short) * nsems);
    if (!u->adj) { kfree(u, sizeof(*u)); return NULL; }
    memset(u->adj, 0, sizeof(short) * nsems);
    u->pid = pid; u->index = index; u->seq = seq; u->nsems = nsems;
    u->next = undo_list; undo_list = u;
    return u;
}

/* Drop every undo record referring to slot `index`. */
static void sem_undo_drop_set(int index)
{
    struct sem_undo **pp = &undo_list;
    while (*pp) {
        struct sem_undo *u = *pp;
        if (u->index == index) {
            *pp = u->next;
            kfree(u->adj, sizeof(short) * u->nsems);
            kfree(u, sizeof(*u));
        } else {
            pp = &u->next;
        }
    }
}

static void sem_free_set(struct ksemset *s)
{
    int index = (int)(s - semsets);
    sem_undo_drop_set(index);
    if (s->sems)
        kfree(s->sems, sizeof(struct ksem) * s->nsems);
    s->sems = NULL;
    s->in_use = 0;
    s->nsems = 0;
    s->key = 0;
    s->seq++;               /* invalidate outstanding ids */
}

/* ---- core ---- */

int kern_semget(key_t key, int nsems, int semflg)
{
    sem_lazy_init();
    mutex_lock(&sem_lock);

    struct ksemset *s = NULL;
    int free_index = -1;

    if (key != IPC_PRIVATE) {
        for (int i = 0; i < SEMMNI; i++) {
            if (semsets[i].in_use && semsets[i].key == key) { s = &semsets[i]; break; }
            if (!semsets[i].in_use && free_index < 0) free_index = i;
        }
        if (s) {
            int err = 0;
            if ((semflg & IPC_CREAT) && (semflg & IPC_EXCL)) err = EEXIST;
            else if (nsems > 0 && nsems > s->nsems)          err = EINVAL;
            else if (!sem_perm_ok(s, 4 | (2)))               err = EACCES;
            int ret = err ? -err : sem_make_id((int)(s - semsets), s->seq);
            mutex_unlock(&sem_lock);
            return ret;
        }
        if (!(semflg & IPC_CREAT)) { mutex_unlock(&sem_lock); return -ENOENT; }
    } else {
        for (int i = 0; i < SEMMNI; i++)
            if (!semsets[i].in_use) { free_index = i; break; }
    }

    /* Create a new set. */
    if (nsems <= 0 || nsems > SEMMSL) { mutex_unlock(&sem_lock); return -EINVAL; }
    if (free_index < 0)              { mutex_unlock(&sem_lock); return -ENOSPC; }

    s = &semsets[free_index];
    s->sems = (struct ksem *)kmalloc(sizeof(struct ksem) * nsems);
    if (!s->sems) { mutex_unlock(&sem_lock); return -ENOMEM; }
    memset(s->sems, 0, sizeof(struct ksem) * nsems);

    process_t *p = current_process;
    s->in_use      = 1;
    s->key         = key;
    s->nsems       = nsems;
    s->perm.__key  = key;
    s->perm.uid    = s->perm.cuid = p ? p->euid : 0;
    s->perm.gid    = s->perm.cgid = p ? p->egid : 0;
    s->perm.mode   = semflg & 0777;
    s->perm.__seq  = s->seq;
    s->otime       = 0;
    s->ctime       = sem_now();

    int id = sem_make_id(free_index, s->seq);
    mutex_unlock(&sem_lock);
    return id;
}

/* Try to apply all ops to a temp copy; return 1 if feasible, 0 if would block,
 * or -errno on a hard error.  On success, *needs_undo reflects any SEM_UNDO. */
static int sem_try(struct ksemset *s, const struct sembuf *ops, size_t n,
                   int *tmp)
{
    for (int i = 0; i < s->nsems; i++)
        tmp[i] = s->sems[i].val;

    for (size_t i = 0; i < n; i++) {
        int num = ops[i].sem_num;
        int op  = ops[i].sem_op;
        if (num < 0 || num >= s->nsems)
            return -EFBIG;
        if (op > 0) {
            if (tmp[num] + op > SEMVMX) return -ERANGE;
            tmp[num] += op;
        } else if (op < 0) {
            if (tmp[num] + op < 0) return 0;     /* would block */
            tmp[num] += op;
        } else { /* op == 0 */
            if (tmp[num] != 0) return 0;          /* would block */
        }
    }
    return 1;
}

/* Mark this caller as a waiter on the sems its op list currently can't satisfy
 * (op<0 short of value -> semncnt; op==0 on nonzero -> semzcnt) and record what
 * was bumped in `wbump[i]` (1=ncnt, 2=zcnt) so it can be reversed exactly,
 * regardless of how the values change while we sleep. */
static void sem_waitcnt_enter(struct ksemset *s, const struct sembuf *ops,
                              size_t n, char *wbump)
{
    for (size_t i = 0; i < n; i++) {
        int num = ops[i].sem_num;
        wbump[i] = 0;
        if (num < 0 || num >= s->nsems)
            continue;
        if (ops[i].sem_op < 0 && s->sems[num].val + ops[i].sem_op < 0) {
            s->sems[num].ncnt++; wbump[i] = 1;
        } else if (ops[i].sem_op == 0 && s->sems[num].val != 0) {
            s->sems[num].zcnt++; wbump[i] = 2;
        }
    }
}

static void sem_waitcnt_leave(struct ksemset *s, const struct sembuf *ops,
                              size_t n, const char *wbump)
{
    for (size_t i = 0; i < n; i++) {
        int num = ops[i].sem_num;
        if (!wbump[i] || num < 0 || num >= s->nsems)
            continue;
        if (wbump[i] == 1) s->sems[num].ncnt--;
        else               s->sems[num].zcnt--;
    }
}

int kern_semop(int semid, const struct sembuf *ksops, size_t nsops)
{
    if (nsops == 0) return -EINVAL;     /* semop requires >= 1 op (matches Linux) */
    if (nsops > SEMOPM) return -E2BIG;

    sem_lazy_init();
    mutex_lock(&sem_lock);

    int tmp[SEMMSL];
    int blocked = 0;        /* set once we've slept waiting on this set */

    for (;;) {
        int err = 0;
        struct ksemset *s = sem_lookup(semid, &err);
        if (!s) {
            mutex_unlock(&sem_lock);
            /* If we had already validated the set and then blocked, its
             * disappearance means it was removed: report EIDRM, not EINVAL. */
            return blocked ? -EIDRM : -err;
        }
        if (!sem_perm_ok(s, 2)) { mutex_unlock(&sem_lock); return -EACCES; }

        int r = sem_try(s, ksops, nsops, tmp);
        if (r < 0) { mutex_unlock(&sem_lock); return r; }
        if (r == 1) {
            /* Commit. */
            int index = (int)(s - semsets);
            int pid = current_process ? current_process->pid : 0;
            struct sem_undo *undo = NULL;
            int want_undo = 0;
            for (size_t i = 0; i < nsops; i++)
                if (ksops[i].sem_flg & SEM_UNDO) { want_undo = 1; break; }
            if (want_undo)
                undo = sem_undo_find(pid, index, s->seq, s->nsems, 1);

            for (int i = 0; i < s->nsems; i++)
                s->sems[i].val = tmp[i];
            for (size_t i = 0; i < nsops; i++) {
                int num = ksops[i].sem_num;
                if (ksops[i].sem_op != 0)
                    s->sems[num].pid = pid;
                if (undo && (ksops[i].sem_flg & SEM_UNDO)) {
                    int a = undo->adj[num] - ksops[i].sem_op;
                    if (a > SEMVMX) a = SEMVMX;
                    if (a < -SEMVMX) a = -SEMVMX;
                    undo->adj[num] = (short)a;
                }
            }
            s->otime = sem_now();
            sleepq_wake_all(&semsets[index]);
            mutex_unlock(&sem_lock);
            return 0;
        }

        /* Would block. */
        int nowait = 0;
        for (size_t i = 0; i < nsops; i++)
            if (ksops[i].sem_flg & IPC_NOWAIT) { nowait = 1; break; }
        if (nowait) { mutex_unlock(&sem_lock); return -EAGAIN; }

        if (current_thread &&
            (current_thread->sig_pending & ~current_thread->sig_mask)) {
            mutex_unlock(&sem_lock);
            return -EINTR;
        }
        int index = (int)(s - semsets);
        blocked = 1;                                 /* we validated the set, now we sleep */
        char wbump[SEMOPM];
        sem_waitcnt_enter(s, ksops, nsops, wbump);   /* count us as a waiter */
        if (current_thread)
            current_thread->flags |= THREAD_F_INTERRUPTIBLE;
        sleepq_add(&semsets[index], current_thread);
        mutex_unlock(&sem_lock);
        sched_yield();
        if (current_thread)
            current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
        mutex_lock(&sem_lock);
        /* Reverse our waiter count exactly (if the set still exists). */
        {
            int e2 = 0;
            struct ksemset *s2 = sem_lookup(semid, &e2);
            if (s2)
                sem_waitcnt_leave(s2, ksops, nsops, wbump);
        }
        if (current_thread &&
            (current_thread->sig_pending & ~current_thread->sig_mask)) {
            mutex_unlock(&sem_lock);
            return -EINTR;
        }
        /* loop: re-resolve (the set may have been removed → EIDRM) */
    }
}

/* ---- semctl helpers ---- */

int kern_sem_rmid(int semid)
{
    sem_lazy_init();
    mutex_lock(&sem_lock);
    int err = 0;
    struct ksemset *s = sem_lookup(semid, &err);
    if (!s) { mutex_unlock(&sem_lock); return -err; }
    /* Only owner/creator or root may remove. */
    process_t *p = current_process;
    if (p && p->euid != 0 && p->euid != s->perm.uid && p->euid != s->perm.cuid) {
        mutex_unlock(&sem_lock); return -EPERM;
    }
    int index = (int)(s - semsets);
    sem_free_set(s);
    sleepq_wake_all(&semsets[index]);   /* blocked semop()s wake to EIDRM */
    mutex_unlock(&sem_lock);
    return 0;
}

int kern_sem_setperm(int semid, uid_t uid, gid_t gid, mode_t mode)
{
    sem_lazy_init();
    mutex_lock(&sem_lock);
    int err = 0;
    struct ksemset *s = sem_lookup(semid, &err);
    if (!s) { mutex_unlock(&sem_lock); return -err; }
    process_t *p = current_process;
    if (p && p->euid != 0 && p->euid != s->perm.uid && p->euid != s->perm.cuid) {
        mutex_unlock(&sem_lock); return -EPERM;
    }
    s->perm.uid  = uid;
    s->perm.gid  = gid;
    s->perm.mode = (s->perm.mode & ~0777) | (mode & 0777);
    s->ctime     = sem_now();
    mutex_unlock(&sem_lock);
    return 0;
}

int kern_sem_stat(int semid, struct semid_ds *out)
{
    sem_lazy_init();
    mutex_lock(&sem_lock);
    int err = 0;
    struct ksemset *s = sem_lookup(semid, &err);
    if (!s) { mutex_unlock(&sem_lock); return -err; }
    if (!sem_perm_ok(s, 4)) { mutex_unlock(&sem_lock); return -EACCES; }
    memset(out, 0, sizeof(*out));
    out->sem_perm  = s->perm;
    out->sem_otime = s->otime;
    out->sem_ctime = s->ctime;
    out->sem_nsems = (unsigned long)s->nsems;
    mutex_unlock(&sem_lock);
    return 0;
}

int kern_sem_nsems(int semid)
{
    sem_lazy_init();
    mutex_lock(&sem_lock);
    int err = 0;
    struct ksemset *s = sem_lookup(semid, &err);
    int ret = s ? s->nsems : -err;
    mutex_unlock(&sem_lock);
    return ret;
}

/* shared body for the per-semnum getters */
static int sem_get_field(int semid, int semnum, int which)
{
    sem_lazy_init();
    mutex_lock(&sem_lock);
    int err = 0;
    struct ksemset *s = sem_lookup(semid, &err);
    if (!s) { mutex_unlock(&sem_lock); return -err; }
    if (!sem_perm_ok(s, 4)) { mutex_unlock(&sem_lock); return -EACCES; }
    if (semnum < 0 || semnum >= s->nsems) { mutex_unlock(&sem_lock); return -EINVAL; }
    int v;
    switch (which) {
    case GETVAL:  v = s->sems[semnum].val;  break;
    case GETPID:  v = s->sems[semnum].pid;  break;
    case GETNCNT: v = s->sems[semnum].ncnt; break;
    case GETZCNT: v = s->sems[semnum].zcnt; break;
    default:      v = -EINVAL;               break;
    }
    mutex_unlock(&sem_lock);
    return v;
}

int kern_sem_getval(int semid, int semnum)  { return sem_get_field(semid, semnum, GETVAL); }
int kern_sem_getpid(int semid, int semnum)  { return sem_get_field(semid, semnum, GETPID); }
int kern_sem_getncnt(int semid, int semnum) { return sem_get_field(semid, semnum, GETNCNT); }
int kern_sem_getzcnt(int semid, int semnum) { return sem_get_field(semid, semnum, GETZCNT); }

int kern_sem_setval(int semid, int semnum, int val)
{
    if (val < 0 || val > SEMVMX) return -ERANGE;
    sem_lazy_init();
    mutex_lock(&sem_lock);
    int err = 0;
    struct ksemset *s = sem_lookup(semid, &err);
    if (!s) { mutex_unlock(&sem_lock); return -err; }
    if (!sem_perm_ok(s, 2)) { mutex_unlock(&sem_lock); return -EACCES; }
    if (semnum < 0 || semnum >= s->nsems) { mutex_unlock(&sem_lock); return -EINVAL; }
    int index = (int)(s - semsets);
    s->sems[semnum].val = val;
    s->sems[semnum].pid = current_process ? current_process->pid : 0;
    s->ctime = sem_now();
    sleepq_wake_all(&semsets[index]);
    mutex_unlock(&sem_lock);
    return 0;
}

int kern_sem_getall(int semid, unsigned short *kbuf, size_t kbuf_n)
{
    sem_lazy_init();
    mutex_lock(&sem_lock);
    int err = 0;
    struct ksemset *s = sem_lookup(semid, &err);
    if (!s) { mutex_unlock(&sem_lock); return -err; }
    if (!sem_perm_ok(s, 4)) { mutex_unlock(&sem_lock); return -EACCES; }
    if (kbuf_n < (size_t)s->nsems) { mutex_unlock(&sem_lock); return -EINVAL; }
    for (int i = 0; i < s->nsems; i++)
        kbuf[i] = (unsigned short)s->sems[i].val;
    int n = s->nsems;
    mutex_unlock(&sem_lock);
    return n;
}

int kern_sem_setall(int semid, const unsigned short *kbuf, size_t n)
{
    sem_lazy_init();
    mutex_lock(&sem_lock);
    int err = 0;
    struct ksemset *s = sem_lookup(semid, &err);
    if (!s) { mutex_unlock(&sem_lock); return -err; }
    if (!sem_perm_ok(s, 2)) { mutex_unlock(&sem_lock); return -EACCES; }
    if (n != (size_t)s->nsems) { mutex_unlock(&sem_lock); return -EINVAL; }
    for (int i = 0; i < s->nsems; i++) {
        if (kbuf[i] > SEMVMX) { mutex_unlock(&sem_lock); return -ERANGE; }
        s->sems[i].val = kbuf[i];
    }
    int index = (int)(s - semsets);
    s->ctime = sem_now();
    sleepq_wake_all(&semsets[index]);
    mutex_unlock(&sem_lock);
    return 0;
}

/* ---- SEM_UNDO at process exit ---- */

void sem_proc_cleanup(int pid)
{
    if (!sem_ready) return;
    mutex_lock(&sem_lock);
    struct sem_undo **pp = &undo_list;
    while (*pp) {
        struct sem_undo *u = *pp;
        if (u->pid != pid) { pp = &u->next; continue; }
        *pp = u->next;
        struct ksemset *s = &semsets[u->index];
        if (s->in_use && s->seq == u->seq) {
            for (int i = 0; i < u->nsems && i < s->nsems; i++) {
                int v = s->sems[i].val + u->adj[i];
                if (v < 0) v = 0;
                if (v > SEMVMX) v = SEMVMX;
                s->sems[i].val = v;
            }
            sleepq_wake_all(&semsets[u->index]);
        }
        kfree(u->adj, sizeof(short) * u->nsems);
        kfree(u, sizeof(*u));
    }
    mutex_unlock(&sem_lock);
}

/* ---- native-ABI syscall wrappers ---- */

int sys_semget(key_t key, int nsems, int semflg)
{
    return kern_semget(key, nsems, semflg);
}

int sys_semop(int semid, struct sembuf *sops, size_t nsops)
{
    struct sembuf kops[SEMOPM];
    if (nsops == 0) return -EINVAL;
    if (nsops > SEMOPM) return -E2BIG;
    if (copyin(sops, kops, sizeof(struct sembuf) * nsops) != 0)
        return -EFAULT;
    return kern_semop(semid, kops, nsops);
}

int sys_semctl(int semid, int semnum, int cmd, uintptr_t arg)
{
    switch (cmd) {
    case IPC_RMID:
        return kern_sem_rmid(semid);
    case IPC_STAT: {
        struct semid_ds ds;
        int r = kern_sem_stat(semid, &ds);
        if (r < 0) return r;
        if (copyout(&ds, (void *)arg, sizeof(ds)) != 0) return -EFAULT;
        return 0;
    }
    case IPC_SET: {
        struct semid_ds ds;
        if (copyin((void *)arg, &ds, sizeof(ds)) != 0) return -EFAULT;
        return kern_sem_setperm(semid, ds.sem_perm.uid, ds.sem_perm.gid,
                                ds.sem_perm.mode);
    }
    case GETVAL:  return kern_sem_getval(semid, semnum);
    case GETPID:  return kern_sem_getpid(semid, semnum);
    case GETNCNT: return kern_sem_getncnt(semid, semnum);
    case GETZCNT: return kern_sem_getzcnt(semid, semnum);
    case SETVAL:  return kern_sem_setval(semid, semnum, (int)arg);
    case GETALL: {
        int n = kern_sem_nsems(semid);
        if (n < 0) return n;
        unsigned short kbuf[SEMMSL];
        int r = kern_sem_getall(semid, kbuf, SEMMSL);
        if (r < 0) return r;
        if (copyout(kbuf, (void *)arg, sizeof(unsigned short) * r) != 0)
            return -EFAULT;
        return 0;
    }
    case SETALL: {
        int n = kern_sem_nsems(semid);
        if (n < 0) return n;
        unsigned short kbuf[SEMMSL];
        if (copyin((void *)arg, kbuf, sizeof(unsigned short) * n) != 0)
            return -EFAULT;
        return kern_sem_setall(semid, kbuf, n);
    }
    default:
        return -EINVAL;
    }
}
