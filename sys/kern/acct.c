#include <sys/acct.h>
#include <sys/proc.h>
#include <sys/session.h>
#include <vfs/vfs.h>
#include <drivers/video/vga.h>
#include <kern/sched.h>

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

int sys_acct(const char *path) {
    if (path == 0) {
        if (acct_node) {
            close_fs(acct_node);
            acct_node = 0;
        }
        return 0;
    }

    // In a real kernel, we would check permissions here.
    fs_node_t *node = finddir_fs(fs_root, (char*)path); // Simple path lookup
    if (!node) return -1; // ENOENT

    // Should verify it's a regular file
    if ((node->flags & 0x7) == FS_DIRECTORY) return -1; // EISDIR

    acct_node = node;
    open_fs(acct_node, 1, 1); // Open for writing
    return 0;
}

extern uint32_t get_time(void); // Defined in time.c (to be implemented)

void acct_process(int exitcode) {
    (void)exitcode;
    if (!acct_node || !current_process) return;

    struct acct ac;
    process_t *p = current_process;

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
    if (!current_process || !current_process->p_pgrp) return -1;
    return current_process->p_pgrp->pg_id;
}

/* sys_setpgid is now implemented in pm/pgrp.c */
extern int sys_setpgid(int pid, int pgid);
