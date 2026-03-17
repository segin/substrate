#include <vfs/vnode.h>
#include <kern/console.h>
#include <string.h>

void test_vdrop(void) {
    kprint("TEST: vdrop... ");
    struct vnode vp;
    memset(&vp, 0, sizeof(vp));
    spinlock_init(&vp.v_interlock, "vnode_interlock");

    vp.v_holdcount = 1;
    vdrop(&vp);
    if (vp.v_holdcount != 0) {
        kprint("FAIL (vdrop)\n");
        return;
    }

    kprint("PASS\n");
}
