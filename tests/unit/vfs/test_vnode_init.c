#include <sys/types.h>
#include <sys/errno.h>
#include <vfs/vnode.h>
#include <stdbool.h>

bool test_vnode_init_basic(void) {
    /* Initialize vnode system to create vnode_zone */
    vnode_init();

    /* If it didn't panic, the basic path works. */
    return true;
}
