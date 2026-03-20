#ifndef _SYS_FILE_H
#define _SYS_FILE_H

#include <sys/types.h>

struct vnode;
struct uio;
struct ucred;
struct thread;
struct file;
struct stat;

/*
 * File descriptor types
 */
#define DTYPE_VNODE     1       /* vnode */
#define DTYPE_SOCKET    2       /* socket */
#define DTYPE_PIPE      3       /* pipe */
#define DTYPE_KQUEUE    4       /* kqueue */

/*
 * File flags (f_flag)
 */
#define FREAD           0x0001
#define FWRITE          0x0002
#define FNONBLOCK       0x0004
#define FAPPEND         0x0008
#define FASYNC          0x0010
#define FSIGIO          0x0020
#define FMARK           0x0040
#define FDEFER          0x0080
#define FHASLOCK        0x0100
#define FDIRTY          0x0200
#define O_DIRECT        0x0400
/* O_CLOEXEC defined in <sys/fcntl.h> */

struct fileops {
    int (*fo_read)(struct file *fp, struct uio *uio, struct ucred *cred, int flags, struct thread *td);
    int (*fo_write)(struct file *fp, struct uio *uio, struct ucred *cred, int flags, struct thread *td);
    int (*fo_ioctl)(struct file *fp, unsigned long command, void *data, struct thread *td);
    int (*fo_poll)(struct file *fp, int events, struct thread *td);
    int (*fo_close)(struct file *fp, struct thread *td);
    int (*fo_stat)(struct file *fp, struct stat *sb, struct thread *td);
};

struct file {
    short           f_type;         /* descriptor type */
    short           f_flag;         /* file flags */
    uint32_t        f_count;        /* reference count */
    struct ucred    *f_cred;        /* credentials at open time */
    struct fileops  *f_ops;         /* file operations vector */
    off_t           f_offset;       /* current offset */
    void            *f_data;        /* vnode/socket/pipe/etc. */
    struct file     *f_next;        /* next file in global list */
};

typedef struct file file_t;

#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0

#endif /* _SYS_FILE_H */
