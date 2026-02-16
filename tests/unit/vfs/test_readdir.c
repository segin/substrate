#include <sys/types.h>
#include <sys/errno.h>
#include <vfs/vnode.h>
#include <sys/uio.h>
#include <sys/ucred.h>
#include <sys/dirent.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

/* Mock vnode operations for testing */
static int mock_readdir(struct vnode *vp, struct uio *uio, struct ucred *cred, int *eofflag, int *ncookies, uint64_t **cookies);

static struct vnodeops mock_vops = {
    .vop_readdir = mock_readdir
};

static struct dirent mock_entries[] = {
    { .d_ino = 1, .d_type = DT_DIR, .d_namlen = 1, .d_name = "." },
    { .d_ino = 2, .d_type = DT_DIR, .d_namlen = 2, .d_name = ".." },
    { .d_ino = 3, .d_type = DT_REG, .d_namlen = 4, .d_name = "file" }
};

static int mock_readdir(struct vnode *vp, struct uio *uio, struct ucred *cred, int *eofflag, int *ncookies, uint64_t **cookies) {
    (void)vp;
    (void)cred;
    (void)ncookies;
    (void)cookies;

    size_t total_entries = sizeof(mock_entries) / sizeof(mock_entries[0]);
    size_t current_idx = uio->uio_offset / sizeof(struct dirent);
    
    if (current_idx >= total_entries) {
        if (eofflag) *eofflag = 1;
        return 0;
    }

    size_t remaining = uio->uio_resid;
    size_t bytes_copied = 0;

    while (current_idx < total_entries && remaining >= sizeof(struct dirent)) {
        struct dirent *ent = &mock_entries[current_idx];
        /* Simplified copy for test - essentially uiomove */
        memcpy(uio->uio_iov->iov_base + bytes_copied, ent, sizeof(struct dirent));
        
        bytes_copied += sizeof(struct dirent);
        remaining -= sizeof(struct dirent);
        current_idx++;
        uio->uio_offset += sizeof(struct dirent);
    }
    
    uio->uio_resid = remaining;
    
    if (current_idx >= total_entries && eofflag) {
        *eofflag = 1;
    } else if (eofflag) {
        *eofflag = 0;
    }

    return 0;
}

bool test_vop_readdir_basic(void) {
    struct vnode vp;
    memset(&vp, 0, sizeof(vp));
    vp.v_type = VDIR;
    vp.v_op = &mock_vops;

    struct ucred cred = {0};
    int eofflag = 0;
    
    /* Buffer for reading */
    struct dirent buf[10];
    struct iovec iov = { .iov_base = buf, .iov_len = sizeof(buf) };
    struct uio uio = {
        .uio_iov = &iov,
        .uio_iovcnt = 1,
        .uio_offset = 0,
        .uio_resid = sizeof(buf),
        .uio_segflg = UIO_SYSSPACE,
        .uio_rw = UIO_READ,
        .uio_td = NULL
    };

    int error = vop_readdir(&vp, &uio, &cred, &eofflag, NULL, NULL);
    
    if (error != 0) return false;
    if (eofflag != 1) return false;
    
    /* Verify entries */
    if (memcmp(&buf[0], &mock_entries[0], sizeof(struct dirent)) != 0) return false;
    if (memcmp(&buf[1], &mock_entries[1], sizeof(struct dirent)) != 0) return false;
    if (memcmp(&buf[2], &mock_entries[2], sizeof(struct dirent)) != 0) return false;

    return true;
}

bool test_vop_readdir_notdir(void) {
    struct vnode vp;
    memset(&vp, 0, sizeof(vp));
    vp.v_type = VREG; /* Not a directory */
    vp.v_op = &mock_vops;

    struct ucred cred = {0};
    int eofflag = 0;
    struct uio uio = {0};

    int error = vop_readdir(&vp, &uio, &cred, &eofflag, NULL, NULL);
    
    if (error != ENOTDIR) return false;

    return true;
}
