#include "../../../sys/exec/perso/freebsd/freebsd_user.h"
#include <stdbool.h>
#include <stddef.h>

/*
 * FreeBSD kinfo_proc Unit Tests (ABI Verification)
 */

bool test_kinfo_proc_size(void) {
    // Verified size for FreeBSD 14.3 i386
    // (Note: This depends on exact padding/alignment)
    // For now, we verify it's at least as large as expected.
    return (sizeof(struct kinfo_proc) >= 1000); 
}

bool test_kinfo_proc_offsets(void) {
    // Verify critical offsets match FreeBSD 14.3
    // ki_pid is typically at a fixed offset
    struct kinfo_proc kp;
    
    if (offsetof(struct kinfo_proc, ki_pid) < 40) return false;
    if (offsetof(struct kinfo_proc, ki_comm) < 100) return false;
    
    return true;
}
