/*
 * perso_ipc_shm.c — System V shared-memory ABI shims for the Linux / FreeBSD
 * personalities.
 *
 * The kernel core (kern_shm*, sys/kern/ipc_shm.c) operates on substrate's
 * native shmid_ds layout.  Each foreign personality has its own struct layout
 * and its own ipc-call/syscall conventions; the shims here translate.
 *
 * shmget's scalar args and shmdt are ABI-identical everywhere, so they pass
 * straight through.  shmat returns an address (the personalities deliver it
 * differently — Linux writes it through a result pointer and returns 0; FreeBSD
 * returns it directly).  Only shmctl's shmid_ds (IPC_STAT/IPC_SET) differs.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <exec/perso/perso_ipc_shm.h>
#include <sys/copy.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>

/* ======================================================================
 * Linux  (i386 ipc(2) multiplexer, syscall 117)
 *   ipc(call, first, second, third, ptr, fifth)
 *   SHMAT=21 SHMDT=22 SHMGET=23 SHMCTL=24
 *   glibc OR's IPC_64 (0x100) into the shmctl cmd.
 *
 *   SHMAT:  first=shmid, second=shmflg, third=&result_ulong, ptr=shmaddr.
 *           Returns 0 and writes the attach address to *third.
 *   SHMDT:  ptr=shmaddr.
 *   SHMGET: first=key, second=size, third=shmflg.
 *   SHMCTL: first=shmid, second=cmd, ptr=buf.
 * ====================================================================== */

#define LINUX_SHMAT   21
#define LINUX_SHMDT   22
#define LINUX_SHMGET  23
#define LINUX_SHMCTL  24
#define LINUX_IPC_64  0x100

struct linux_ipc64_perm_shm {
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

struct linux_shmid64_ds {
    struct linux_ipc64_perm_shm shm_perm;
    uint32_t shm_segsz;
    uint32_t shm_atime;        /* i386 kernel ABI: 32-bit seconds ... */
    uint32_t __unused1;
    uint32_t shm_dtime;
    uint32_t __unused2;
    uint32_t shm_ctime;
    uint32_t __unused3;
    uint32_t shm_cpid;
    uint32_t shm_lpid;
    uint32_t shm_nattch;
    uint32_t __unused4;
    uint32_t __unused5;
};

static int linux_shmctl(int shmid, int cmd, uint32_t buf)
{
    switch (cmd & ~LINUX_IPC_64) {
    case IPC_RMID:
        return kern_shm_rmid(shmid);
    case SHM_LOCK:
        return kern_shm_lock(shmid, 1);
    case SHM_UNLOCK:
        return kern_shm_lock(shmid, 0);
    case IPC_STAT:
    case SHM_STAT: {
        struct shmid_ds nds;
        int r = kern_shm_stat(shmid, &nds);
        if (r < 0) return r;
        struct linux_shmid64_ds lds;
        memset(&lds, 0, sizeof(lds));
        lds.shm_perm.key  = nds.shm_perm.__key;
        lds.shm_perm.uid  = nds.shm_perm.uid;
        lds.shm_perm.gid  = nds.shm_perm.gid;
        lds.shm_perm.cuid = nds.shm_perm.cuid;
        lds.shm_perm.cgid = nds.shm_perm.cgid;
        lds.shm_perm.mode = (uint16_t)nds.shm_perm.mode;
        lds.shm_perm.seq  = nds.shm_perm.__seq;
        lds.shm_segsz     = (uint32_t)nds.shm_segsz;
        lds.shm_atime     = (uint32_t)nds.shm_atime;
        lds.shm_dtime     = (uint32_t)nds.shm_dtime;
        lds.shm_ctime     = (uint32_t)nds.shm_ctime;
        lds.shm_cpid      = (uint32_t)nds.shm_cpid;
        lds.shm_lpid      = (uint32_t)nds.shm_lpid;
        lds.shm_nattch    = (uint32_t)nds.shm_nattch;
        if (copyout(&lds, (void *)(uintptr_t)buf, sizeof(lds)) != 0)
            return -EFAULT;
        return 0;
    }
    case IPC_SET: {
        struct linux_shmid64_ds lds;
        if (copyin((void *)(uintptr_t)buf, &lds, sizeof(lds)) != 0)
            return -EFAULT;
        return kern_shm_setperm(shmid, lds.shm_perm.uid, lds.shm_perm.gid,
                                lds.shm_perm.mode);
    }
    default:
        return -EINVAL;
    }
}

int linux_sys_ipc_shm(unsigned call, int first, int second, int third,
                      uint32_t ptr, int32_t fifth)
{
    (void)fifth;
    switch (call & 0xffff) {
    case LINUX_SHMGET:
        return kern_shmget((key_t)first, (size_t)(uint32_t)second, third);
    case LINUX_SHMAT: {
        int err = 0;
        void *r = kern_shmat(first, (const void *)(uintptr_t)ptr, second, &err);
        if (r == (void *)-1)
            return -err;
        /* Linux writes the attach address through `third` and returns 0. */
        uint32_t uaddr = (uint32_t)(uintptr_t)r;
        if (copyout(&uaddr, (void *)(uintptr_t)third, sizeof(uaddr)) != 0)
            return -EFAULT;
        return 0;
    }
    case LINUX_SHMDT:
        return kern_shmdt((const void *)(uintptr_t)ptr);
    case LINUX_SHMCTL:
        return linux_shmctl(first, second, ptr);
    default:
        return -ENOSYS;
    }
}

/* ======================================================================
 * FreeBSD  (i386: shmat=228, shmdt=230, shmget=231, shmctl=512)
 *   shmat(shmid, addr, flag) returns the address directly.
 *   shmctl(shmid, cmd, struct shmid_ds *buf).
 * ====================================================================== */

struct bsd_ipc_perm32_shm {     /* FreeBSD ipc_perm32 */
    uint32_t cuid;
    uint32_t cgid;
    uint32_t uid;
    uint32_t gid;
    uint16_t mode;
    uint16_t seq;
    uint32_t key;
};

struct freebsd_shmid_ds32 {
    struct bsd_ipc_perm32_shm shm_perm;
    int32_t  shm_segsz;
    int32_t  shm_lpid;
    int32_t  shm_cpid;
    int32_t  shm_nattch;        /* shmatt_t (int on i386) */
    int32_t  shm_atime;
    int32_t  shm_dtime;
    int32_t  shm_ctime;
    uint32_t shm_internal;
};

int freebsd_sys_shmget(key_t key, size_t size, int shmflg)
{
    return kern_shmget(key, size, shmflg);
}

void *freebsd_sys_shmat(int shmid, uint32_t shmaddr, int shmflg)
{
    int err = 0;
    void *r = kern_shmat(shmid, (const void *)(uintptr_t)shmaddr, shmflg, &err);
    if (r == (void *)-1)
        return (void *)(intptr_t)(-err);   /* libc errno bridge */
    return r;
}

int freebsd_sys_shmdt(uint32_t shmaddr)
{
    return kern_shmdt((const void *)(uintptr_t)shmaddr);
}

int freebsd_sys_shmctl(int shmid, int cmd, uint32_t buf)
{
    switch (cmd) {
    case IPC_RMID:
        return kern_shm_rmid(shmid);
    case SHM_LOCK:
        return kern_shm_lock(shmid, 1);
    case SHM_UNLOCK:
        return kern_shm_lock(shmid, 0);
    case IPC_STAT: {
        struct shmid_ds nds;
        int r = kern_shm_stat(shmid, &nds);
        if (r < 0) return r;
        struct freebsd_shmid_ds32 ds;
        memset(&ds, 0, sizeof(ds));
        ds.shm_perm.cuid = nds.shm_perm.cuid;
        ds.shm_perm.cgid = nds.shm_perm.cgid;
        ds.shm_perm.uid  = nds.shm_perm.uid;
        ds.shm_perm.gid  = nds.shm_perm.gid;
        ds.shm_perm.mode = (uint16_t)nds.shm_perm.mode;
        ds.shm_perm.seq  = nds.shm_perm.__seq;
        ds.shm_perm.key  = (uint32_t)nds.shm_perm.__key;
        ds.shm_segsz     = (int32_t)nds.shm_segsz;
        ds.shm_lpid      = (int32_t)nds.shm_lpid;
        ds.shm_cpid      = (int32_t)nds.shm_cpid;
        ds.shm_nattch    = (int32_t)nds.shm_nattch;
        ds.shm_atime     = (int32_t)nds.shm_atime;
        ds.shm_dtime     = (int32_t)nds.shm_dtime;
        ds.shm_ctime     = (int32_t)nds.shm_ctime;
        if (copyout(&ds, (void *)(uintptr_t)buf, sizeof(ds)) != 0)
            return -EFAULT;
        return 0;
    }
    case IPC_SET: {
        struct freebsd_shmid_ds32 ds;
        if (copyin((void *)(uintptr_t)buf, &ds, sizeof(ds)) != 0)
            return -EFAULT;
        return kern_shm_setperm(shmid, ds.shm_perm.uid, ds.shm_perm.gid,
                                ds.shm_perm.mode);
    }
    default:
        return -EINVAL;
    }
}

/* ======================================================================
 * NetBSD  (i386: shmat=228, shmdt=230, shmget=231, ____shmctl50=512)
 *   shmat(shmid, addr, flag) returns the address directly.
 *   ____shmctl50(shmid, cmd, struct shmid_ds *buf) — the time_t-64 ("50")
 *   ABI; cmd values match FreeBSD/native (IPC_RMID=0, IPC_SET=1, IPC_STAT=2,
 *   SHM_LOCK=3, SHM_UNLOCK=4).
 * ====================================================================== */

struct netbsd_ipc_perm32_shm {  /* netbsd32_ipc_perm */
    uint32_t cuid;
    uint32_t cgid;
    uint32_t uid;
    uint32_t gid;
    uint16_t mode;
    uint16_t _seq;
    uint32_t _key;
};

struct netbsd_shmid_ds32 {      /* netbsd32_shmid_ds (____shmctl50: time_t-64) */
    struct netbsd_ipc_perm32_shm shm_perm;
    uint32_t shm_segsz;         /* size_t on i386 */
    int32_t  shm_lpid;
    int32_t  shm_cpid;
    uint32_t shm_nattch;        /* shmatt_t (unsigned int on NetBSD) */
    int64_t  shm_atime;
    int64_t  shm_dtime;
    int64_t  shm_ctime;
    uint32_t _shm_internal;     /* netbsd32_voidp */
};

int netbsd_sys_shmget(key_t key, size_t size, int shmflg)
{
    return kern_shmget(key, size, shmflg);
}

void *netbsd_sys_shmat(int shmid, uint32_t shmaddr, int shmflg)
{
    int err = 0;
    void *r = kern_shmat(shmid, (const void *)(uintptr_t)shmaddr, shmflg, &err);
    if (r == (void *)-1)
        return (void *)(intptr_t)(-err);   /* libc errno bridge */
    return r;
}

int netbsd_sys_shmdt(uint32_t shmaddr)
{
    return kern_shmdt((const void *)(uintptr_t)shmaddr);
}

int netbsd_sys_shmctl(int shmid, int cmd, uint32_t buf)
{
    switch (cmd) {
    case IPC_RMID:
        return kern_shm_rmid(shmid);
    case SHM_LOCK:
        return kern_shm_lock(shmid, 1);
    case SHM_UNLOCK:
        return kern_shm_lock(shmid, 0);
    case IPC_STAT: {
        struct shmid_ds nds;
        int r = kern_shm_stat(shmid, &nds);
        if (r < 0) return r;
        struct netbsd_shmid_ds32 ds;
        memset(&ds, 0, sizeof(ds));
        ds.shm_perm.cuid = nds.shm_perm.cuid;
        ds.shm_perm.cgid = nds.shm_perm.cgid;
        ds.shm_perm.uid  = nds.shm_perm.uid;
        ds.shm_perm.gid  = nds.shm_perm.gid;
        ds.shm_perm.mode = (uint16_t)nds.shm_perm.mode;
        ds.shm_perm._seq = nds.shm_perm.__seq;
        ds.shm_perm._key = (uint32_t)nds.shm_perm.__key;
        ds.shm_segsz     = (uint32_t)nds.shm_segsz;
        ds.shm_lpid      = (int32_t)nds.shm_lpid;
        ds.shm_cpid      = (int32_t)nds.shm_cpid;
        ds.shm_nattch    = (uint32_t)nds.shm_nattch;
        ds.shm_atime     = (int64_t)nds.shm_atime;
        ds.shm_dtime     = (int64_t)nds.shm_dtime;
        ds.shm_ctime     = (int64_t)nds.shm_ctime;
        if (copyout(&ds, (void *)(uintptr_t)buf, sizeof(ds)) != 0)
            return -EFAULT;
        return 0;
    }
    case IPC_SET: {
        struct netbsd_shmid_ds32 ds;
        if (copyin((void *)(uintptr_t)buf, &ds, sizeof(ds)) != 0)
            return -EFAULT;
        return kern_shm_setperm(shmid, ds.shm_perm.uid, ds.shm_perm.gid,
                                ds.shm_perm.mode);
    }
    default:
        return -EINVAL;
    }
}
