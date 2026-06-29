/*
 * perso_ipc_sem.c — System V semaphore ABI shims for the Linux / FreeBSD /
 * NetBSD personalities.
 *
 * The kernel core (kern_sem*, sys/kern/ipc_sem.c) operates on substrate's
 * native semid_ds / sembuf / semun layout.  Each foreign personality has its
 * own struct layout and its own semctl command numbers; the shims here
 * translate.
 *
 * sembuf ({unsigned short, short, short}) and semget's scalar args are
 * byte-identical across all four ABIs, so semop/semget pass straight through to
 * kern_semop / kern_semget.  Only semctl's semid_ds (IPC_STAT/IPC_SET) and the
 * GETxxx/SETxxx command numbers differ, plus the way each ABI delivers the
 * `union semun` 4th argument (Linux & NetBSD: a pointer to the union; FreeBSD:
 * the union by value).
 */

#include <sys/sem.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/copy.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include "perso_ipc_shm.h"

/* native semctl command numbers (from <sys/sem.h>): GETPID=11 GETVAL=12
 * GETALL=13 GETNCNT=14 GETZCNT=15 SETVAL=16 SETALL=17.  Linux uses the same
 * values; the BSDs use 3..9, remapped below. */

/* The operation a foreign cmd maps to, ABI-independent. */
enum sem_cmd {
    SC_BAD = 0, SC_RMID, SC_SET, SC_STAT,
    SC_GETVAL, SC_SETVAL, SC_GETALL, SC_SETALL,
    SC_GETPID, SC_GETNCNT, SC_GETZCNT
};

/* Shared dispatch for the cmds that need no semid_ds translation (scalars +
 * arrays, which are ABI-identical).  `arg` is the already-resolved union value
 * (an int for SETVAL, or a user pointer for GET/SETALL).  Returns the syscall
 * result (>=0 value, or -errno). */
static int sem_common(int semid, int semnum, enum sem_cmd c, uint32_t arg)
{
    switch (c) {
    case SC_RMID:    return kern_sem_rmid(semid);
    case SC_GETVAL:  return kern_sem_getval(semid, semnum);
    case SC_SETVAL:  return kern_sem_setval(semid, semnum, (int)arg);
    case SC_GETPID:  return kern_sem_getpid(semid, semnum);
    case SC_GETNCNT: return kern_sem_getncnt(semid, semnum);
    case SC_GETZCNT: return kern_sem_getzcnt(semid, semnum);
    case SC_GETALL: {
        int n = kern_sem_nsems(semid);
        if (n < 0) return n;
        unsigned short kbuf[SEMMSL];
        int r = kern_sem_getall(semid, kbuf, SEMMSL);
        if (r < 0) return r;
        if (copyout(kbuf, (void *)(uintptr_t)arg, sizeof(unsigned short) * r) != 0)
            return -EFAULT;
        return 0;
    }
    case SC_SETALL: {
        int n = kern_sem_nsems(semid);
        if (n < 0) return n;
        unsigned short kbuf[SEMMSL];
        if (copyin((void *)(uintptr_t)arg, kbuf, sizeof(unsigned short) * n) != 0)
            return -EFAULT;
        return kern_sem_setall(semid, kbuf, n);
    }
    default:
        return -EINVAL;
    }
}

/* ======================================================================
 * Linux  (i386 ipc(2) multiplexer, syscall 117)
 *   ipc(call, first, second, third, ptr, fifth)
 *   SEMOP=1 SEMGET=2 SEMCTL=3 SEMTIMEDOP=4
 *   glibc OR's IPC_64 (0x100) into the semctl cmd; values otherwise match
 *   substrate's native cmd numbers.
 * ====================================================================== */

#define LINUX_SEMOP       1
#define LINUX_SEMGET      2
#define LINUX_SEMCTL      3
#define LINUX_SEMTIMEDOP  4
#define LINUX_IPC_64      0x100

struct linux_ipc64_perm {
    int32_t  key;
    uint32_t uid;
    uint32_t gid;
    uint32_t cuid;
    uint32_t cgid;
    uint16_t mode;
    uint16_t __pad1;
    uint16_t seq;
    uint16_t __pad2;
    uint32_t __unused1;
    uint32_t __unused2;
};

struct linux_semid64_ds {
    struct linux_ipc64_perm sem_perm;
    uint32_t sem_otime;        /* i386 kernel ABI: 32-bit seconds ... */
    uint32_t __unused1;        /* ... + high word */
    uint32_t sem_ctime;
    uint32_t __unused2;
    uint32_t sem_nsems;
    uint32_t __unused3;
    uint32_t __unused4;
};

static enum sem_cmd linux_cmd(int cmd)
{
    switch (cmd & ~LINUX_IPC_64) {
    case IPC_RMID: return SC_RMID;
    case IPC_SET:  return SC_SET;
    case IPC_STAT: return SC_STAT;
    case GETVAL:   return SC_GETVAL;   /* 12 */
    case SETVAL:   return SC_SETVAL;   /* 16 */
    case GETALL:   return SC_GETALL;   /* 13 */
    case SETALL:   return SC_SETALL;   /* 17 */
    case GETPID:   return SC_GETPID;   /* 11 */
    case GETNCNT:  return SC_GETNCNT;  /* 14 */
    case GETZCNT:  return SC_GETZCNT;  /* 15 */
    default:       return SC_BAD;
    }
}

static int linux_semctl(int semid, int semnum, int cmd, uint32_t uptr)
{
    enum sem_cmd c = linux_cmd(cmd);
    if (c == SC_BAD) return -EINVAL;

    /* Linux ipc() passes `ptr` = address of a union semun; pull the 4-byte
     * union value out of it (val or a userspace pointer). */
    uint32_t arg = 0;
    if (uptr && copyin((void *)(uintptr_t)uptr, &arg, sizeof(arg)) != 0)
        return -EFAULT;

    if (c == SC_STAT) {
        struct semid_ds nds;
        int r = kern_sem_stat(semid, &nds);
        if (r < 0) return r;
        struct linux_semid64_ds lds;
        memset(&lds, 0, sizeof(lds));
        lds.sem_perm.key  = nds.sem_perm.__key;
        lds.sem_perm.uid  = nds.sem_perm.uid;
        lds.sem_perm.gid  = nds.sem_perm.gid;
        lds.sem_perm.cuid = nds.sem_perm.cuid;
        lds.sem_perm.cgid = nds.sem_perm.cgid;
        lds.sem_perm.mode = (uint16_t)nds.sem_perm.mode;
        lds.sem_perm.seq  = nds.sem_perm.__seq;
        lds.sem_otime     = (uint32_t)nds.sem_otime;
        lds.sem_ctime     = (uint32_t)nds.sem_ctime;
        lds.sem_nsems     = (uint32_t)nds.sem_nsems;
        if (copyout(&lds, (void *)(uintptr_t)arg, sizeof(lds)) != 0)
            return -EFAULT;
        return 0;
    }
    if (c == SC_SET) {
        struct linux_semid64_ds lds;
        if (copyin((void *)(uintptr_t)arg, &lds, sizeof(lds)) != 0)
            return -EFAULT;
        return kern_sem_setperm(semid, lds.sem_perm.uid, lds.sem_perm.gid,
                                lds.sem_perm.mode);
    }
    return sem_common(semid, semnum, c, arg);
}

static int linux_semop(int semid, uint32_t sops, unsigned nsops)
{
    if (nsops == 0 || nsops > SEMOPM) return (nsops == 0) ? -EINVAL : -E2BIG;
    struct sembuf kops[SEMOPM];
    if (copyin((void *)(uintptr_t)sops, kops, sizeof(struct sembuf) * nsops) != 0)
        return -EFAULT;
    return kern_semop(semid, kops, nsops);
}

int linux_sys_ipc(unsigned call, int first, int second, int third,
                  uint32_t ptr, int32_t fifth)
{
    (void)fifth;
    switch (call & 0xffff) {
    case LINUX_SEMGET:
        return kern_semget((key_t)first, second, third);
    case LINUX_SEMOP:
        return linux_semop(first, ptr, (unsigned)second);
    case LINUX_SEMTIMEDOP:
        /* substrate has no semop timeout; behave as SEMOP (third = timeout). */
        return linux_semop(first, ptr, (unsigned)second);
    case LINUX_SEMCTL:
        return linux_semctl(first, second, third, ptr);
    default:
        /* Not a semaphore subcall — try the shared-memory dispatcher
         * (SHMAT/SHMDT/SHMGET/SHMCTL live in perso_ipc_shm.c). */
        return linux_sys_ipc_shm(call, first, second, third, ptr, fifth);
    }
}

/* ======================================================================
 * FreeBSD  (i386: semget=221, semop=222, __semctl=510)
 *   __semctl(semid, semnum, cmd, union semun arg) — arg passed BY VALUE
 *   (one 32-bit word: val, or a userspace pointer).
 *   cmd: GETNCNT=3 GETPID=4 GETVAL=5 GETALL=6 GETZCNT=7 SETVAL=8 SETALL=9.
 * ====================================================================== */

struct bsd_ipc_perm32 {        /* FreeBSD ipc_perm32 */
    uint32_t cuid;
    uint32_t cgid;
    uint32_t uid;
    uint32_t gid;
    uint16_t mode;
    uint16_t seq;
    uint32_t key;
};

struct freebsd_semid_ds32 {
    struct bsd_ipc_perm32 sem_perm;
    uint32_t __sem_base;
    uint16_t sem_nsems;
    int32_t  sem_otime;
    int32_t  sem_ctime;
};

static enum sem_cmd bsd_cmd(int cmd)
{
    switch (cmd) {
    case IPC_RMID: return SC_RMID;     /* 0 */
    case IPC_SET:  return SC_SET;      /* 1 */
    case IPC_STAT: return SC_STAT;     /* 2 */
    case 3:        return SC_GETNCNT;
    case 4:        return SC_GETPID;
    case 5:        return SC_GETVAL;
    case 6:        return SC_GETALL;
    case 7:        return SC_GETZCNT;
    case 8:        return SC_SETVAL;
    case 9:        return SC_SETALL;
    default:       return SC_BAD;
    }
}

int freebsd_sys_semget(key_t key, int nsems, int semflg)
{
    return kern_semget(key, nsems, semflg);
}

int freebsd_sys_semop(int semid, uint32_t sops, unsigned nsops)
{
    if (nsops == 0 || nsops > SEMOPM) return (nsops == 0) ? -EINVAL : -E2BIG;
    struct sembuf kops[SEMOPM];
    if (copyin((void *)(uintptr_t)sops, kops, sizeof(struct sembuf) * nsops) != 0)
        return -EFAULT;
    return kern_semop(semid, kops, nsops);
}

int freebsd_sys___semctl(int semid, int semnum, int cmd, uint32_t arg)
{
    enum sem_cmd c = bsd_cmd(cmd);
    if (c == SC_BAD) return -EINVAL;

    if (c == SC_STAT) {
        struct semid_ds nds;
        int r = kern_sem_stat(semid, &nds);
        if (r < 0) return r;
        struct freebsd_semid_ds32 ds;
        memset(&ds, 0, sizeof(ds));
        ds.sem_perm.cuid = nds.sem_perm.cuid;
        ds.sem_perm.cgid = nds.sem_perm.cgid;
        ds.sem_perm.uid  = nds.sem_perm.uid;
        ds.sem_perm.gid  = nds.sem_perm.gid;
        ds.sem_perm.mode = (uint16_t)nds.sem_perm.mode;
        ds.sem_perm.seq  = nds.sem_perm.__seq;
        ds.sem_perm.key  = (uint32_t)nds.sem_perm.__key;
        ds.sem_nsems     = (uint16_t)nds.sem_nsems;
        ds.sem_otime     = (int32_t)nds.sem_otime;
        ds.sem_ctime     = (int32_t)nds.sem_ctime;
        /* FreeBSD __semctl's arg is `union semun` by value: arg = &semid_ds. */
        if (copyout(&ds, (void *)(uintptr_t)arg, sizeof(ds)) != 0)
            return -EFAULT;
        return 0;
    }
    if (c == SC_SET) {
        struct freebsd_semid_ds32 ds;
        if (copyin((void *)(uintptr_t)arg, &ds, sizeof(ds)) != 0)
            return -EFAULT;
        return kern_sem_setperm(semid, ds.sem_perm.uid, ds.sem_perm.gid,
                                ds.sem_perm.mode);
    }
    return sem_common(semid, semnum, c, arg);
}

/* ======================================================================
 * NetBSD  (i386: semget=221, semop=222, ____semctl50=442)
 *   ____semctl50(semid, semnum, cmd, union __semun *arg) — arg is a POINTER
 *   to the union (NetBSD libc passes &semun).
 *   cmd values match FreeBSD (3..9).  The "50" ABI uses 64-bit time_t.
 * ====================================================================== */

struct netbsd_ipc_perm32 {     /* netbsd32_ipc_perm */
    uint32_t cuid;
    uint32_t cgid;
    uint32_t uid;
    uint32_t gid;
    uint16_t mode;
    uint16_t _seq;
    uint32_t _key;
};

struct netbsd_semid_ds32 {     /* netbsd32_semid_ds (____semctl50: time_t-64) */
    struct netbsd_ipc_perm32 sem_perm;
    uint16_t sem_nsems;
    int64_t  sem_otime;
    int64_t  sem_ctime;
    uint32_t _sem_base;
};

int netbsd_sys_semget(key_t key, int nsems, int semflg)
{
    return kern_semget(key, nsems, semflg);
}

int netbsd_sys_semop(int semid, uint32_t sops, unsigned nsops)
{
    if (nsops == 0 || nsops > SEMOPM) return (nsops == 0) ? -EINVAL : -E2BIG;
    struct sembuf kops[SEMOPM];
    if (copyin((void *)(uintptr_t)sops, kops, sizeof(struct sembuf) * nsops) != 0)
        return -EFAULT;
    return kern_semop(semid, kops, nsops);
}

int netbsd_sys_semctl(int semid, int semnum, int cmd, uint32_t uptr)
{
    enum sem_cmd c = bsd_cmd(cmd);
    if (c == SC_BAD) return -EINVAL;

    /* NetBSD passes &union __semun; pull the 4-byte union value out. */
    uint32_t arg = 0;
    if (uptr && copyin((void *)(uintptr_t)uptr, &arg, sizeof(arg)) != 0)
        return -EFAULT;

    if (c == SC_STAT) {
        struct semid_ds nds;
        int r = kern_sem_stat(semid, &nds);
        if (r < 0) return r;
        struct netbsd_semid_ds32 ds;
        memset(&ds, 0, sizeof(ds));
        ds.sem_perm.cuid = nds.sem_perm.cuid;
        ds.sem_perm.cgid = nds.sem_perm.cgid;
        ds.sem_perm.uid  = nds.sem_perm.uid;
        ds.sem_perm.gid  = nds.sem_perm.gid;
        ds.sem_perm.mode = (uint16_t)nds.sem_perm.mode;
        ds.sem_perm._seq = nds.sem_perm.__seq;
        ds.sem_perm._key = (uint32_t)nds.sem_perm.__key;
        ds.sem_nsems     = (uint16_t)nds.sem_nsems;
        ds.sem_otime     = (int64_t)nds.sem_otime;
        ds.sem_ctime     = (int64_t)nds.sem_ctime;
        if (copyout(&ds, (void *)(uintptr_t)arg, sizeof(ds)) != 0)
            return -EFAULT;
        return 0;
    }
    if (c == SC_SET) {
        struct netbsd_semid_ds32 ds;
        if (copyin((void *)(uintptr_t)arg, &ds, sizeof(ds)) != 0)
            return -EFAULT;
        return kern_sem_setperm(semid, ds.sem_perm.uid, ds.sem_perm.gid,
                                ds.sem_perm.mode);
    }
    return sem_common(semid, semnum, c, arg);
}
