/*
 * sys/drivers/devices/kmem.c
 *
 * /dev/kmem - Kernel Virtual Memory Access Device
 *
 * This driver exposes the kernel virtual address space to privileged users.
 * It enforces strict security policies including privilege checks and
 * kernel lockdown/securelevel enforcement.
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <vfs/vfs.h>
#include <kern/console.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/fcntl.h>
#include <sys/poll.h>
#include <stdio.h>

/* Global securelevel (defined in sys/kern/sysctl.c) */
extern int securelevel;

/* External Copy Helpers (sys/arch/i386/signal.c) */
extern int copyin(const void *src, void *dst, size_t size);
extern int copyout(const void *src, void *dst, size_t size);

/* Policy configuration */
static int kmem_allow_read = 1;   /* Allow reading by default (if privileged) */
static int kmem_allow_write = 0;  /* Deny writing by default */

/* Sysctl Registration for Policy */
#define KERN_KMEM_READ 24
#define KERN_KMEM_WRITE 25
SYSCTL_INT(kern, KERN_KMEM_READ, kmem_allow_read, CTLFLAG_RW|CTLFLAG_SECURE, &kmem_allow_read, 0, "Allow reading /dev/kmem");
SYSCTL_INT(kern, KERN_KMEM_WRITE, kmem_allow_write, CTLFLAG_RW|CTLFLAG_SECURE, &kmem_allow_write, 0, "Allow writing /dev/kmem");

/*
 * Privileged Check Helper
 */
static int check_privileges(void) {
    if (!current_process) return EPERM;
    if (current_process->euid != 0) return EPERM;

    /* Check securelevel/lockdown */
    if (securelevel > 0) {
        /* In secure mode, /dev/kmem is generally restricted */
        /* For now, we deny access if securelevel > 0 */
        return EPERM;
    }

    return 0;
}

/*
 * kmem_open
 */
static void kmem_open(fs_node_t *node) {
    (void)node;
    /*
     * We don't fail here because open() in VFS usually returns void or doesn't propagation error
     * well in this specific VFS implementation (it returns void).
     * However, the VFS open_fs calls this.
     * The actual permission check should happen in VFS before calling open,
     * or we rely on read/write to fail.
     *
     * But wait, standard open() syscall checks permissions.
     * If VFS doesn't support returning error from open op, we are limited.
     *
     * Looking at sys/vfs/vfs.c: open_fs returns void.
     * But sys_open (syscall) calls vfs_lookup then check_permissions then open_fs.
     * check_permissions checks file mode (r/w/x) vs uid/gid.
     *
     * For a character device, usually the driver's open routine handles specific checks.
     * If the driver open fails, the syscall should fail.
     * But the signature is `void (*open_type_t)(struct fs_node*);`.
     * This is a limitation of the current VFS.
     *
     * We will enforce permissions in read/write/ioctl.
     */
}

/*
 * kmem_read
 * Reads from Kernel Virtual Address Space to User Buffer.
 */
static size_t kmem_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;

    /* 1. Privilege Check */
    if (check_privileges() != 0) {
        /* We can't return error code directly in size_t read (usually returns -1 cast to size_t) */
        /* Common convention in this OS seems to be (size_t)-Error */
        return (size_t)-EPERM;
    }

    /* 2. Policy Check */
    if (!kmem_allow_read) {
        return (size_t)-EPERM;
    }

    /* 3. Address Validation (Sanity) */
    /* We don't strictly validate KVA ranges here, relying on hardware faults (caught by copyout) */

    /* 4. Perform Copy */
    /*
     * copyout(src, dst, size)
     * src = Kernel Address (offset)
     * dst = User Buffer (buffer)
     */
    void *kva = (void *)(uintptr_t)offset;

    /* copyout returns 0 on success, -1 on fault */
    if (copyout(kva, buffer, size) != 0) {
        return (size_t)-EFAULT;
    }

    return size;
}

/*
 * kmem_write
 * Writes from User Buffer to Kernel Virtual Address Space.
 */
static size_t kmem_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node;

    /* 1. Privilege Check */
    if (check_privileges() != 0) {
        return (size_t)-EPERM;
    }

    /* 2. Policy Check */
    if (!kmem_allow_write) {
        return (size_t)-EPERM;
    }

    /* 3. Perform Copy */
    /*
     * copyin(src, dst, size)
     * src = User Buffer (buffer)
     * dst = Kernel Address (offset)
     */
    void *kva = (void *)(uintptr_t)offset;

    /* copyin returns 0 on success, -1 on fault */
    /* Note: casting const away is necessary for copyin prototype but we are reading from it */
    if (copyin(buffer, kva, size) != 0) {
        return (size_t)-EFAULT;
    }

    return size;
}

/*
 * kmem_ioctl
 */
static int kmem_ioctl(fs_node_t *node, uint32_t request, void *arg) {
    (void)node; (void)request; (void)arg;
    /* Only privileged users can issue ioctls, but we don't support any yet */
    if (check_privileges() != 0) return -EPERM;
    return -ENOTTY;
}

/*
 * kmem_poll
 */
static int kmem_poll(fs_node_t *node, void *waiter) {
    (void)node; (void)waiter;
    /* Always ready for I/O (it's memory) */
    return POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM;
}

/*
 * kmem_mmap - Explicitly Disallowed
 */
static void *kmem_mmap(fs_node_t *node, void *addr, size_t length, int prot, int flags, off_t offset) {
    (void)node; (void)addr; (void)length; (void)prot; (void)flags; (void)offset;
    /* Mapping kernel memory to user space is dangerous and disabled */
    return (void *)-EPERM; /* or EINVAL */
}

/*
 * Device Node
 */
static fs_node_t kmem_node;

/*
 * kmem_dev_init
 * Registers /dev/kmem
 */
void kmem_dev_init(void) {
    memset(&kmem_node, 0, sizeof(fs_node_t));
    strcpy(kmem_node.name, "kmem");
    kmem_node.flags = FS_CHARDEVICE;
    kmem_node.uid = 0;
    kmem_node.gid = 0;
    kmem_node.mask = 0600; /* Only root can open */
    kmem_node.rdev = (1 << 8) | 2; /* Major 1, Minor 2 */

    kmem_node.open = &kmem_open;
    kmem_node.read = &kmem_read;
    kmem_node.write = &kmem_write;
    kmem_node.ioctl = &kmem_ioctl;
    kmem_node.poll = &kmem_poll;
    kmem_node.mmap = &kmem_mmap;

    devfs_register_device(&kmem_node);

    char buf[128];
    sprintf(buf, "/dev/kmem initialized (Policy: R=%d W=%d)\n", kmem_allow_read, kmem_allow_write);
    kprint(buf);

    /* Initialize Kernel Test Helper (if linked) */
    extern void kmem_test_init(void);
    /* Weak symbol check would be nice, but for now we assume it's linked if configured */
    /* Since we control the build, we know it is there. */
    kmem_test_init();
}
