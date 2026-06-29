/*
 * ipc_shm.c — System V shared memory (shmget/shmat/shmdt/shmctl).
 *
 * A fixed table of SHMMNI segments.  Each segment owns a run of
 * physically-contiguous, kernel-direct-mapped pages (pmm_alloc_contiguous).
 * Keys map to ids by linear scan (SHMMNI is small).  A shmid encodes a slot
 * index and a per-slot sequence number so a stale id from a
 * removed-and-recreated segment is rejected (EIDRM/EINVAL).
 *
 * shmat() maps the segment's pages into the calling process's address space as
 * a SHARED device-pager region: every attacher's PTEs point at the same
 * physical frames, write-back cacheable, so a write in one process is visible
 * in all the others (this is NOT copy-on-write).  Each attachment is recorded
 * in shm_attach_list so shmdt() and proc_exit() can reverse it.
 *
 * Backing pages live as long as the segment.  IPC_RMID marks the segment for
 * destruction; the pages are freed when the last attach is dropped (the SysV
 * "remove-but-keep-alive-until-detached" semantics).  An IPC_RMID with no
 * attachers frees immediately.
 *
 * The core (kern_shm*) works on kernel memory only and makes no assumption
 * about the userspace shmid_ds layout, so the native and the Linux/BSD
 * personalities can each marshal their own ABI on top of it.
 */

#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/copy.h>
#include <sys/kern_syscalls.h>
#include <arch/i386/pmm.h>
#include <arch/i386/pmap.h>
#include <vm/vm_kmem.h>
#include <vm/vm_object.h>
#include <vm/vm_pager.h>
#include <vm/vm_map.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>

struct kshmseg {
    int             in_use;
    int             marked_rmid;  /* IPC_RMID seen: free pages on last detach */
    key_t           key;
    unsigned short  seq;          /* bumped each (re)allocation of this slot */
    struct ipc_perm perm;
    size_t          size;         /* requested size in bytes */
    size_t          npages;       /* backing pages */
    void           *kvaddr;       /* kernel direct-mapped base of backing */
    uintptr_t       phys;         /* physical base of backing */
    int             locked;       /* SHM_LOCK state */
    time_t          atime;
    time_t          dtime;
    time_t          ctime;
    pid_t           cpid;         /* creator pid */
    pid_t           lpid;         /* last shmat/shmdt pid */
    unsigned long   nattch;       /* current attaches */
};

struct shm_attach {
    struct shm_attach *next;
    int        pid;
    int        index;             /* segment slot index */
    unsigned short seq;
    uintptr_t  vaddr;             /* user VA of this attachment */
    size_t     length;            /* mapped length (page-rounded) */
};

static struct kshmseg shmsegs[SHMMNI];
static struct shm_attach *shm_attach_list;
static mutex_t shm_lock;
static int     shm_ready;

void shm_init(void)
{
    if (shm_ready)
        return;
    mutex_init(&shm_lock, "sysv_shm");
    shm_ready = 1;
}

static void shm_lazy_init(void)
{
    if (!shm_ready)
        shm_init();
}

/* id <-> slot encoding: id = seq * SHMMNI + index. */
static int shm_make_id(int index, unsigned short seq)
{
    return (int)seq * SHMMNI + index;
}

/* Resolve a shmid to its in-use slot, or NULL with *err set. */
static struct kshmseg *shm_lookup(int shmid, int *err)
{
    if (shmid < 0) { *err = EINVAL; return NULL; }
    int index = shmid % SHMMNI;
    unsigned short seq = (unsigned short)(shmid / SHMMNI);
    struct kshmseg *s = &shmsegs[index];
    if (!s->in_use) { *err = EINVAL; return NULL; }
    if (s->seq != seq) { *err = EIDRM; return NULL; }
    return s;
}

/* Permission check.  want = read flag 4 / write flag 2 (octal-style, matching
 * the low mode bits).  Root bypasses. */
static int shm_perm_ok(struct kshmseg *s, int want)
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

static time_t shm_now(void)
{
    return kern_time(NULL);
}

/* Free a segment's backing pages and release the slot (caller holds shm_lock).
 * Only call once nattch has reached 0. */
static void shm_free_seg(struct kshmseg *s)
{
    if (s->kvaddr && s->npages)
        pmm_free_contiguous(s->kvaddr, s->npages);
    s->kvaddr = NULL;
    s->phys = 0;
    s->npages = 0;
    s->size = 0;
    s->in_use = 0;
    s->marked_rmid = 0;
    s->locked = 0;
    s->key = 0;
    s->nattch = 0;
    s->seq++;                /* invalidate outstanding ids */
}

/* ---- core ---- */

int kern_shmget(key_t key, size_t size, int shmflg)
{
    shm_lazy_init();

    /* Page-round the requested size. */
    if (size > SHMMAX)
        return -EINVAL;

    mutex_lock(&shm_lock);

    struct kshmseg *s = NULL;
    int free_index = -1;

    if (key != IPC_PRIVATE) {
        for (int i = 0; i < SHMMNI; i++) {
            if (shmsegs[i].in_use && shmsegs[i].key == key) { s = &shmsegs[i]; break; }
            if (!shmsegs[i].in_use && free_index < 0) free_index = i;
        }
        if (s) {
            int err = 0;
            if ((shmflg & IPC_CREAT) && (shmflg & IPC_EXCL))      err = EEXIST;
            else if (size != 0 && size > s->size)                err = EINVAL;
            else if (!shm_perm_ok(s, 4 | 2))                      err = EACCES;
            int ret = err ? -err : shm_make_id((int)(s - shmsegs), s->seq);
            mutex_unlock(&shm_lock);
            return ret;
        }
        if (!(shmflg & IPC_CREAT)) { mutex_unlock(&shm_lock); return -ENOENT; }
    } else {
        for (int i = 0; i < SHMMNI; i++)
            if (!shmsegs[i].in_use) { free_index = i; break; }
    }

    /* Create a new segment. */
    if (size < SHMMIN)  { mutex_unlock(&shm_lock); return -EINVAL; }
    if (free_index < 0) { mutex_unlock(&shm_lock); return -ENOSPC; }

    size_t npages = (size + 0xFFFU) >> 12;
    void *kv = pmm_alloc_contiguous(npages);
    if (!kv) { mutex_unlock(&shm_lock); return -ENOMEM; }
    memset(kv, 0, npages << 12);

    s = &shmsegs[free_index];
    process_t *p = current_process;
    s->in_use       = 1;
    s->marked_rmid  = 0;
    s->key          = key;
    s->size         = size;
    s->npages       = npages;
    s->kvaddr       = kv;
    s->phys         = (uintptr_t)V2P(kv);
    s->locked       = 0;
    s->perm.__key   = key;
    s->perm.uid     = s->perm.cuid = p ? p->euid : 0;
    s->perm.gid     = s->perm.cgid = p ? p->egid : 0;
    s->perm.mode    = shmflg & 0777;
    s->perm.__seq   = s->seq;
    s->atime        = 0;
    s->dtime        = 0;
    s->ctime        = shm_now();
    s->cpid         = p ? p->pid : 0;
    s->lpid         = 0;
    s->nattch       = 0;

    int id = shm_make_id(free_index, s->seq);
    mutex_unlock(&shm_lock);
    return id;
}

/* Record an attachment (caller holds shm_lock). */
static int shm_attach_add(int pid, int index, unsigned short seq,
                          uintptr_t vaddr, size_t length)
{
    struct shm_attach *a = (struct shm_attach *)kmalloc(sizeof(*a));
    if (!a)
        return -1;
    a->pid = pid;
    a->index = index;
    a->seq = seq;
    a->vaddr = vaddr;
    a->length = length;
    a->next = shm_attach_list;
    shm_attach_list = a;
    return 0;
}

void *kern_shmat(int shmid, const void *shmaddr, int shmflg, int *err)
{
    shm_lazy_init();

    process_t *p = current_process;
    if (!p || !p->vm_map) { *err = EINVAL; return (void *)-1; }
    vm_map_t *map = p->vm_map;

    mutex_lock(&shm_lock);
    int e = 0;
    struct kshmseg *s = shm_lookup(shmid, &e);
    if (!s) { mutex_unlock(&shm_lock); *err = e; return (void *)-1; }

    int want = (shmflg & SHM_RDONLY) ? 4 : (4 | 2);
    if (!shm_perm_ok(s, want)) { mutex_unlock(&shm_lock); *err = EACCES; return (void *)-1; }

    size_t length = s->npages << 12;
    uintptr_t virt = (uintptr_t)shmaddr;

    /* Snapshot what kern_shmat needs from the segment before dropping the
     * lock for the (potentially blocking) vm_map operations.  The segment
     * can't be freed underneath us because nattch keeps it alive only after
     * we succeed; to keep this simple we hold shm_lock across the map work,
     * which never blocks on shm_lock recursively. */
    uintptr_t phys = s->phys;
    int index = (int)(s - shmsegs);
    unsigned short seq = s->seq;

    uint32_t vm_prot = VM_PROT_READ | VM_PROT_USER;
    if (!(shmflg & SHM_RDONLY))
        vm_prot |= VM_PROT_WRITE;
    if (shmflg & SHM_EXEC)
        vm_prot |= VM_PROT_EXEC;

    if (virt == 0) {
        if (vm_map_find_space(map, &virt, length) != 0) {
            mutex_unlock(&shm_lock); *err = ENOMEM; return (void *)-1;
        }
    } else {
        if (shmflg & SHM_RND)
            virt &= ~((uintptr_t)SHMLBA - 1);
        if (virt & 0xFFF) { mutex_unlock(&shm_lock); *err = EINVAL; return (void *)-1; }
        /* Clear anything already mapped there (SHM_REMAP-ish behaviour). */
        if (vm_map_remove(map, virt, virt + length) != 0) {
            mutex_unlock(&shm_lock); *err = EINVAL; return (void *)-1;
        }
    }

    vm_object_t *obj = vm_object_allocate(VM_OBJ_TYPE_DEVICE, length);
    if (!obj) { mutex_unlock(&shm_lock); *err = ENOMEM; return (void *)-1; }
    obj->pager = vm_pager_allocate(VM_OBJ_TYPE_DEVICE, (void *)phys, length,
                                   (uint8_t)vm_prot, 0);
    if (!obj->pager) {
        vm_object_deallocate(obj);
        mutex_unlock(&shm_lock); *err = ENOMEM; return (void *)-1;
    }
    /* Plain RAM, not MMIO: map write-back cacheable so the shared mapping
     * runs at full memory speed (MIT-SHM image transfers). */
    vm_pager_set_cache_mode(obj->pager, VM_PAGER_CACHE_WB);

    if (vm_map_insert(map, obj, 0, virt, virt + length,
                      (uint8_t)vm_prot, (uint8_t)vm_prot, VM_INHERIT_NONE) != 0) {
        vm_object_deallocate(obj);
        mutex_unlock(&shm_lock); *err = ENOMEM; return (void *)-1;
    }

    if (shm_attach_add(p->pid, index, seq, virt, length) != 0) {
        vm_map_remove(map, virt, virt + length);
        mutex_unlock(&shm_lock); *err = ENOMEM; return (void *)-1;
    }

    s->nattch++;
    s->atime = shm_now();
    s->lpid = p->pid;
    mutex_unlock(&shm_lock);
    *err = 0;
    return (void *)virt;
}

/* Drop one attachment (caller holds shm_lock).  Frees the segment if this was
 * the last attach and the segment is marked for removal.  Returns 0/-errno. */
static int shm_detach_one(struct shm_attach **pp, vm_map_t *map)
{
    struct shm_attach *a = *pp;
    int index = a->index;
    unsigned short seq = a->seq;
    uintptr_t vaddr = a->vaddr;
    size_t length = a->length;

    /* Unlink the attach record first. */
    *pp = a->next;
    kfree(a, sizeof(*a));

    if (map)
        vm_map_remove(map, vaddr, vaddr + length);

    struct kshmseg *s = &shmsegs[index];
    if (s->in_use && s->seq == seq) {
        if (s->nattch > 0)
            s->nattch--;
        s->dtime = shm_now();
        s->lpid = current_process ? current_process->pid : 0;
        if (s->nattch == 0 && s->marked_rmid)
            shm_free_seg(s);
    }
    return 0;
}

int kern_shmdt(const void *shmaddr)
{
    shm_lazy_init();
    process_t *p = current_process;
    if (!p) return -EINVAL;
    int pid = p->pid;
    uintptr_t vaddr = (uintptr_t)shmaddr;

    mutex_lock(&shm_lock);
    struct shm_attach **pp = &shm_attach_list;
    while (*pp) {
        if ((*pp)->pid == pid && (*pp)->vaddr == vaddr) {
            int r = shm_detach_one(pp, p->vm_map);
            mutex_unlock(&shm_lock);
            return r;
        }
        pp = &(*pp)->next;
    }
    mutex_unlock(&shm_lock);
    return -EINVAL;     /* no attachment at that address */
}

int kern_shm_rmid(int shmid)
{
    shm_lazy_init();
    mutex_lock(&shm_lock);
    int err = 0;
    struct kshmseg *s = shm_lookup(shmid, &err);
    if (!s) { mutex_unlock(&shm_lock); return -err; }
    /* Only owner/creator or root may remove. */
    process_t *p = current_process;
    if (p && p->euid != 0 && p->euid != s->perm.uid && p->euid != s->perm.cuid) {
        mutex_unlock(&shm_lock); return -EPERM;
    }
    if (s->nattch == 0) {
        shm_free_seg(s);            /* no attachers: free now */
    } else {
        s->marked_rmid = 1;         /* free on last detach */
        s->ctime = shm_now();
    }
    mutex_unlock(&shm_lock);
    return 0;
}

int kern_shm_setperm(int shmid, uid_t uid, gid_t gid, mode_t mode)
{
    shm_lazy_init();
    mutex_lock(&shm_lock);
    int err = 0;
    struct kshmseg *s = shm_lookup(shmid, &err);
    if (!s) { mutex_unlock(&shm_lock); return -err; }
    process_t *p = current_process;
    if (p && p->euid != 0 && p->euid != s->perm.uid && p->euid != s->perm.cuid) {
        mutex_unlock(&shm_lock); return -EPERM;
    }
    s->perm.uid  = uid;
    s->perm.gid  = gid;
    s->perm.mode = (s->perm.mode & ~0777) | (mode & 0777);
    s->ctime     = shm_now();
    mutex_unlock(&shm_lock);
    return 0;
}

int kern_shm_stat(int shmid, struct shmid_ds *out)
{
    shm_lazy_init();
    mutex_lock(&shm_lock);
    int err = 0;
    struct kshmseg *s = shm_lookup(shmid, &err);
    if (!s) { mutex_unlock(&shm_lock); return -err; }
    if (!shm_perm_ok(s, 4)) { mutex_unlock(&shm_lock); return -EACCES; }
    memset(out, 0, sizeof(*out));
    out->shm_perm   = s->perm;
    if (s->marked_rmid)
        out->shm_perm.mode |= SHM_DEST;
    if (s->locked)
        out->shm_perm.mode |= SHM_LOCKED;
    out->shm_segsz  = s->size;
    out->shm_atime  = s->atime;
    out->shm_dtime  = s->dtime;
    out->shm_ctime  = s->ctime;
    out->shm_cpid   = s->cpid;
    out->shm_lpid   = s->lpid;
    out->shm_nattch = s->nattch;
    mutex_unlock(&shm_lock);
    return 0;
}

int kern_shm_lock(int shmid, int lock)
{
    shm_lazy_init();
    mutex_lock(&shm_lock);
    int err = 0;
    struct kshmseg *s = shm_lookup(shmid, &err);
    if (!s) { mutex_unlock(&shm_lock); return -err; }
    /* Substrate never pages out shm backing (it is wired by construction),
     * so SHM_LOCK / SHM_UNLOCK only toggle the advisory flag. */
    process_t *p = current_process;
    if (p && p->euid != 0 && p->euid != s->perm.uid && p->euid != s->perm.cuid) {
        mutex_unlock(&shm_lock); return -EPERM;
    }
    s->locked = lock ? 1 : 0;
    mutex_unlock(&shm_lock);
    return 0;
}

/* ---- attachment reversal at process exit ---- */

void shm_proc_cleanup(int pid)
{
    if (!shm_ready) return;
    process_t *p = current_process;
    vm_map_t *map = (p && p->pid == pid) ? p->vm_map : NULL;

    mutex_lock(&shm_lock);
    struct shm_attach **pp = &shm_attach_list;
    while (*pp) {
        if ((*pp)->pid == pid) {
            /* shm_detach_one advances *pp past the freed node. */
            shm_detach_one(pp, map);
        } else {
            pp = &(*pp)->next;
        }
    }
    mutex_unlock(&shm_lock);
}

/* ---- native-ABI syscall wrappers ---- */

int sys_shmget(key_t key, size_t size, int shmflg)
{
    return kern_shmget(key, size, shmflg);
}

void *sys_shmat(int shmid, const void *shmaddr, int shmflg)
{
    int err = 0;
    void *r = kern_shmat(shmid, shmaddr, shmflg, &err);
    if (r == (void *)-1)
        return (void *)(intptr_t)(-err);   /* libc maps negative-errno -> errno */
    return r;
}

int sys_shmdt(const void *shmaddr)
{
    return kern_shmdt(shmaddr);
}

int sys_shmctl(int shmid, int cmd, struct shmid_ds *buf)
{
    switch (cmd) {
    case IPC_RMID:
        return kern_shm_rmid(shmid);
    case IPC_STAT:
    case SHM_STAT: {
        struct shmid_ds ds;
        int r = kern_shm_stat(shmid, &ds);
        if (r < 0) return r;
        if (copyout(&ds, buf, sizeof(ds)) != 0) return -EFAULT;
        return 0;
    }
    case IPC_SET: {
        struct shmid_ds ds;
        if (copyin(buf, &ds, sizeof(ds)) != 0) return -EFAULT;
        return kern_shm_setperm(shmid, ds.shm_perm.uid, ds.shm_perm.gid,
                                ds.shm_perm.mode);
    }
    case SHM_LOCK:
        return kern_shm_lock(shmid, 1);
    case SHM_UNLOCK:
        return kern_shm_lock(shmid, 0);
    default:
        return -EINVAL;
    }
}
