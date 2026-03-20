#include <kern/console.h>
#include <vfs/vnode.h>

void test_vnode_init(void) {
    kprint("\n=== TEST: vnode_init ===\n");
    kprint("Calling vnode_init() for testing...\n");
    vnode_init();
    kprint("PASS: vnode_init() executed without panicking\n");
    kprint("=== TEST COMPLETE ===\n");
}
