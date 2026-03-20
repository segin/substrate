#include <vfs/vnode.h>
#include <kern/console.h>
#include <string.h>

void test_vhold_vdrop(void) {
    kprint("TEST: vhold/vdrop... ");
    struct vnode vp;
    memset(&vp, 0, sizeof(vp));
    spinlock_init(&vp.v_interlock, "vnode_interlock");

    if (vp.v_holdcount != 0) {
        kprint("FAIL (init)\n");
        return;
    }

    vhold(&vp);
    if (vp.v_holdcount != 1) {
        kprint("FAIL (vhold 1)\n");
        return;
    }

    vhold(&vp);
    if (vp.v_holdcount != 2) {
        kprint("FAIL (vhold 2)\n");
        return;
    }

    vdrop(&vp);
    if (vp.v_holdcount != 1) {
        kprint("FAIL (vdrop 1)\n");
        return;
    }

    vdrop(&vp);
    if (vp.v_holdcount != 0) {
        kprint("FAIL (vdrop 2)\n");
        return;
    }

    kprint("PASS\n");
}
