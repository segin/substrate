#include <sys/acct.h>
#include <sys/copy.h>
#include <sys/errno.h>
#include <sys/kern_syscalls.h>
#include <sys/proc.h>
#include <sys/session.h>
#include <sys/signal.h>
#include <string.h>
#include <vfs/vfs.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <drivers/video/vga.h>

static fs_node_t *acct_node = 0;

// Pseudo-floating point compression (V7 style)
// 3 bit base-8 exponent, 13 bit fraction
comp_t compress(uint32_t t) {
    int exp = 0;
    int round = 0;

    while (t >= 8192) {
        exp++;
        round = t & 0x04;
        t >>= 3;
    }
    if (round) {
        t++;
        if (t >= 8192) {
            t >>= 3;
            exp++;
        }
    }
    return (exp << 13) + t;
}

int kern_acct(const char *path) {
    /* Only root may enable/disable process accounting */
    if (current_process && current_process->euid != 0) return -EPERM;

    if (path == 0) {
        if (acct_node) {
            close_fs(acct_node);
            acct_node = 0;
        }
        return 0;
    }

    /* A whole path, from the caller's root or working directory. */
    fs_node_t *root = fs_root, *start;
    if (current_process && current_process->root_node)
        root = current_process->root_node;
    start = root;
    if (path[0] != '/' && current_process && current_process->cwd_node)
        start = current_process->cwd_node;
    fs_node_t *node = vfs_lookup(start, path);
    if (!node) return -ENOENT;

    /* Records go only to a regular file. */
    if ((node->flags & 0x7) != FS_FILE) {
        vfs_rele(node);
        return -EACCES;
    }

    /* Kept open (open_fs) while accounting is on; the lookup's reference
     * goes now, and so does the file accounting wrote to before. */
    open_fs(node, 1, 1); // Open for writing
    vfs_rele(node);
    if (acct_node)
        close_fs(acct_node);
    acct_node = node;
    return 0;
}

int sys_acct(const char *path) {
    if (path == NULL) return kern_acct(NULL);
    
    char kpath[256];
    COPYIN_STR(path, kpath);
    return kern_acct(kpath);
}


void acct_process(int exitcode) {
    (void)exitcode;
    if (!acct_node || !current_process) return;

    struct acct ac;
    process_t *p = current_process;

    /* The padding goes to the file too. */
    memset(&ac, 0, sizeof(ac));

    // Copy comm
    for (int i = 0; i < AC_COMM_LEN; i++) ac.ac_comm[i] = p->comm[i];
    
    // Calculate times
    uint32_t now = get_time();
    
    ac.ac_utime = compress(p->utime);
    ac.ac_stime = compress(p->stime);
    ac.ac_etime = compress(now - p->start_time);
    ac.ac_btime = p->start_time;
    ac.ac_uid = p->uid;
    ac.ac_gid = p->gid;
    ac.ac_mem = 0; // Stub
    ac.ac_io = 0; // Stub
    ac.ac_tty = 0; // Stub
    ac.ac_flag = p->ac_flag | AFORK; // Assuming we are forked for now

    // Write record to end of file
    // Note: VFS write takes offset. We need append.
    // Assuming simple linear write for now, or we track size.
    // fs_node usually has 'length'.
    write_fs(acct_node, acct_node->length, sizeof(struct acct), (uint8_t*)&ac);
}

int sys_getpgrp(void) {
    if (!current_process || !current_process->p_pgrp) return -ESRCH;
    return current_process->p_pgrp->pg_id;
}


