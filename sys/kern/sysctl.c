/*
 * sys/kern/sysctl.c
 *
 * Kernel Sysctl Subsystem Implementation
 */

#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

extern process_t *current_process;

/* Global lock for sysctl tree operations */
static mutex_t sysctl_mutex;
static int sysctl_initialized = 0;

/* Root list */
struct sysctl_oid_list sysctl__children;
struct sysctl_oid_list sysctl_kern_children;
struct sysctl_oid_list sysctl_hw_children;
struct sysctl_oid_list sysctl_vm_children;
struct sysctl_oid_list sysctl_debug_children;

/* Root Nodes */
struct sysctl_oid sysctl_kern = { &sysctl__children, NULL, "kern", CTL_KERN, CTLTYPE_NODE|CTLFLAG_RD, (void*)&sysctl_kern_children, 0, NULL, NULL, "Kernel", 0 };
struct sysctl_oid sysctl_hw   = { &sysctl__children, NULL, "hw",   CTL_HW,   CTLTYPE_NODE|CTLFLAG_RD, (void*)&sysctl_hw_children,   0, NULL, NULL, "Hardware", 0 };
struct sysctl_oid sysctl_vm   = { &sysctl__children, NULL, "vm",   CTL_VM,   CTLTYPE_NODE|CTLFLAG_RD, (void*)&sysctl_vm_children,   0, NULL, NULL, "Virtual Memory", 0 };
struct sysctl_oid sysctl_debug= { &sysctl__children, NULL, "debug",CTL_DEBUG,CTLTYPE_NODE|CTLFLAG_RD, (void*)&sysctl_debug_children,0, NULL, NULL, "Debugging", 0 };

/* Basic Variables */
static char kernel_ostype[] = "Substrate";
static char kernel_osrelease[] = "0.1-ALPHA";
static char kernel_version[] = "Substrate 0.1-ALPHA (GENERIC) #0: Tue Jan 27 00:00:00 UTC 2026";
static int kernel_maxproc = 1000; // placeholder
static char kernel_hostname[256] = "localhost";
static char kernel_domainname[256] = "localdomain";

SYSCTL_STRING(kern, KERN_OSTYPE, ostype, CTLFLAG_RD, kernel_ostype, 0, "Operating system type");
SYSCTL_STRING(kern, KERN_OSRELEASE, osrelease, CTLFLAG_RD, kernel_osrelease, 0, "Operating system release");
SYSCTL_INT(kern, KERN_OSREV, osrevision, CTLFLAG_RD, NULL, 202601, "Operating system revision");
SYSCTL_STRING(kern, KERN_VERSION, version, CTLFLAG_RD, kernel_version, 0, "Kernel version");
SYSCTL_INT(kern, KERN_MAXPROC, maxproc, CTLFLAG_RD, &kernel_maxproc, 0, "Maximum number of processes");
SYSCTL_STRING(kern, KERN_HOSTNAME, hostname, CTLFLAG_RW, kernel_hostname, sizeof(kernel_hostname), "Hostname");
SYSCTL_STRING(kern, KERN_DOMAINNAME, domainname, CTLFLAG_RW, kernel_domainname, sizeof(kernel_domainname), "Domain name");

/* HW Variables */
static char hw_machine[] = "i386";
static char hw_model[] = "Generic x86 PC";
static int hw_ncpu = 1;
static int hw_pagesize = 4096;

SYSCTL_STRING(hw, HW_MACHINE, machine, CTLFLAG_RD, hw_machine, 0, "Machine class");
SYSCTL_STRING(hw, HW_MODEL, model, CTLFLAG_RD, hw_model, 0, "Machine model");
SYSCTL_INT(hw, HW_NCPU, ncpu, CTLFLAG_RD, &hw_ncpu, 0, "Number of CPUs");
SYSCTL_INT(hw, HW_PAGESIZE, pagesize, CTLFLAG_RD, &hw_pagesize, 0, "System page size");

/*
 * sysctl_init()
 */
extern void early_uart_print(const char *s);

void sysctl_init(void) {
    early_uart_print("sysctl_init: start\n");
    if (sysctl_initialized) return;
    sysctl_initialized = 1;
    early_uart_print("sysctl_init: mutex_init\n");
    mutex_init(&sysctl_mutex, "sysctl");
    // Initialize root list
    sysctl__children.slh_first = NULL;
    
    // Register Roots
    sysctl_register_oid(&sysctl_kern);
    sysctl_register_oid(&sysctl_hw);
    sysctl_register_oid(&sysctl_vm);
    sysctl_register_oid(&sysctl_debug);

    // Register Kern variables
    sysctl_register_oid(&sysctl_kern_ostype);
    sysctl_register_oid(&sysctl_kern_osrelease);
    sysctl_register_oid(&sysctl_kern_osrevision);
    sysctl_register_oid(&sysctl_kern_version);
    sysctl_register_oid(&sysctl_kern_maxproc);
    sysctl_register_oid(&sysctl_kern_hostname);
    sysctl_register_oid(&sysctl_kern_domainname);

    // Register HW variables
    sysctl_register_oid(&sysctl_hw_machine);
    sysctl_register_oid(&sysctl_hw_model);
    sysctl_register_oid(&sysctl_hw_ncpu);
    sysctl_register_oid(&sysctl_hw_pagesize);

    sysctl_initialized = 1;
}

/*
 * Safe copy helpers (Temporary)
 * TODO: Move centralized copyin/copyout to sys/kern/subr_copy.c
 */
static int sysctl_check_user_addr(const void *addr, size_t size) {
    uintptr_t start = (uintptr_t)addr;
    uintptr_t end = start + size;
    
    if (end < start) return EFAULT;
    if (start >= 0xC0000000) return EFAULT; // User space < 3GB
    return 0;
}

static int sysctl_copyin(const void *uaddr, void *kaddr, size_t len) {
    if (sysctl_check_user_addr(uaddr, len) != 0) return EFAULT;
    memcpy(kaddr, uaddr, len);
    return 0;
}

static int sysctl_copyout(const void *kaddr, void *uaddr, size_t len) {
    if (sysctl_check_user_addr(uaddr, len) != 0) return EFAULT;
    memcpy(uaddr, kaddr, len);
    return 0;
}

/*
 * Implementation of sys_sysctl System Call
 */
int sys_sysctl(int *name, unsigned int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
    int error = 0;
    struct sysctl_oid *oid;
    struct sysctl_req req;
    int name_buf[CTL_MAXNAME];

    /* 1. Copy in the name */
    if (namelen > CTL_MAXNAME || namelen < 2) {
        return EINVAL;
    }
    error = sysctl_copyin(name, name_buf, namelen * sizeof(int));
    if (error) return error;

    /* 2. Setup request */
    memset(&req, 0, sizeof(req));
    req.p_pid = (current_process) ? current_process->pid : 0;
    req.p_uid = (current_process) ? current_process->uid : 0; // TODO: UID support
    req.oldptr = oldp;
    req.newptr = newp;
    req.newlen = newlen;

    /* 3. Copy in old length if provided */
    if (oldlenp) {
        size_t oldlen;
        error = sysctl_copyin(oldlenp, &oldlen, sizeof(oldlen));
        if (error) return error;
        req.oldlen = oldlen;
    } else {
        req.oldlen = 0;
    }

    mutex_lock(&sysctl_mutex);

    /* 4. Find the OID */
    oid = sysctl_find_oid(name_buf, namelen, NULL);
    if (!oid) {
        mutex_unlock(&sysctl_mutex);
        return ENOENT;
    }

    /* 5. Permission checks (Basic) */
    if ((oid->oid_kind & CTLFLAG_WR) == 0 && newp != NULL) {
        mutex_unlock(&sysctl_mutex);
        return EPERM;
    }

    /* 6. Call parameters setup */
    // For standard handlers, we might need to handle buffer copyout results
    // The handler does the work.

    /* 7. Invoke Handler */
    if (oid->oid_handler) {
        error = oid->oid_handler(oid, oid->oid_arg1, oid->oid_arg2, &req);
    } else {
        error = EINVAL;
    }

    mutex_unlock(&sysctl_mutex);

    /* 8. Copy out new length if requested and no error */
    if (error == 0 && oldlenp) {
        // req.oldidx is updated by handler to indicate how much was written
        // or how much would have been written
        size_t used = req.oldidx;
        sysctl_copyout(&used, oldlenp, sizeof( used ));
    }

    return error;
}


/*
 * Registration
 */
void sysctl_register_oid(struct sysctl_oid *oidp) {
    struct sysctl_oid_list *parent = oidp->oid_parent;
    struct sysctl_oid *p;

    if (!sysctl_initialized) sysctl_init();

    early_uart_print("sysctl_register_oid: lock\n");
    mutex_lock(&sysctl_mutex);

    // Insert into list (simple prepend or sort? BSD sorts)
    // We'll prepend for O(1) now, can sort later.
    // Check for duplicate
    for (p = parent->slh_first; p; p = p->oid_link) {
        if (p->oid_number == oidp->oid_number) {
           // Collision!
           mutex_unlock(&sysctl_mutex);
           return; // Fail?
        }
    }
    oidp->oid_link = parent->slh_first;
    parent->slh_first = oidp;

    mutex_unlock(&sysctl_mutex);
}

void sysctl_unregister_oid(struct sysctl_oid *oidp) {
    struct sysctl_oid_list *parent = oidp->oid_parent;
    struct sysctl_oid *p, *prev = NULL;

    mutex_lock(&sysctl_mutex);
    for (p = parent->slh_first; p; p = p->oid_link) {
        if (p == oidp) {
            if (prev) prev->oid_link = p->oid_link;
            else parent->slh_first = p->oid_link;
            break;
        }
        prev = p;
    }
    mutex_unlock(&sysctl_mutex);
}

/*
 * Lookup
 */
struct sysctl_oid *sysctl_find_oid(int *name, unsigned int namelen, struct sysctl_oid *root) {
    struct sysctl_oid_list *list;
    struct sysctl_oid *oid;
    unsigned int i;

    // Start from root if not specified
    if (!root) {
        list = &sysctl__children;
    } else {
        // If root provided, it must be a NODE
        if ((root->oid_kind & CTLTYPE_MASK) != CTLTYPE_NODE) return NULL;
        list = (struct sysctl_oid_list *)root->oid_arg1;
    }

    // Traverse
    for (i = 0; i < namelen; i++) {
        int id = name[i];
        for (oid = list->slh_first; oid; oid = oid->oid_link) {
            if (oid->oid_number == id) break;
        }
        if (!oid) return NULL;

        // If this is the last component, return it
        if (i == namelen - 1) return oid;

        // Otherwise, must be a node
        if ((oid->oid_kind & CTLTYPE_MASK) != CTLTYPE_NODE) return NULL;
        list = (struct sysctl_oid_list *)oid->oid_arg1;
    }
    return NULL;
}

/*
 * Standard Handlers
 */

/* helper to copy out data */
int sysctl_handle_int(struct sysctl_oid *oidp, void *arg1, int arg2, struct sysctl_req *req) {
    (void)oidp;
    int error = 0;
    int val;

    if (arg1) val = *(int *)arg1;
    else val = arg2;

    if (req->oldptr) {
        size_t len = sizeof(int);
        if (req->oldlen < len) return ENOMEM;
        error = sysctl_copyout(&val, req->oldptr, len);
        if (error) return error;
        req->oldidx = len;
    }

    if (req->newptr) {
        int newval;
        size_t len = sizeof(int);
        if (req->newlen < len) return EINVAL;
        error = sysctl_copyin(req->newptr, &newval, len);
        if (error) return error;
        
        if (arg1) *(int *)arg1 = newval;
        // else read-only effectively if no pointer
    }
    return 0;
}

int sysctl_handle_string(struct sysctl_oid *oidp, void *arg1, int arg2, struct sysctl_req *req) {
    (void)oidp;
    int error = 0;
    char *str = (char *)arg1;
    size_t len = 0;

    if (str) len = strlen(str) + 1; // Include NUL

    if (req->oldptr) {
        if (req->oldlen < len) return ENOMEM;
        error = sysctl_copyout(str, req->oldptr, len);
        if (error) return error;
        req->oldidx = len;
    }

    if (req->newptr) {
        size_t newlen = req->newlen;
        if (newlen > (size_t)arg2) return ENAMETOOLONG; // arg2 is max len for string
        // Copy in new string
        error = sysctl_copyin(req->newptr, str, newlen);
        if (error) return error;
        str[newlen-1] = '\0'; // Ensure termination
    }
    return 0;
}

int sysctl_handle_opaque(struct sysctl_oid *oidp, void *arg1, int arg2, struct sysctl_req *req) {
    (void)oidp;
    int error = 0;
    void *data = arg1;
    size_t len = arg2; // Fixed size passed in arg2

    if (req->oldptr) {
        if (req->oldlen < len) return ENOMEM;
        error = sysctl_copyout(data, req->oldptr, len);
        if (error) return error;
        req->oldidx = len;
    }
    
    // Opaque usually assumes read-only structure or specific handling for write
    if (req->newptr) {
        return EPERM;
    }
    
    return 0;
}

