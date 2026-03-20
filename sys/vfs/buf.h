#ifndef _VFS_BUF_H
#define _VFS_BUF_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/queue.h>

struct vnode;
struct ucred;

/* Buffer flags */
#define B_BUSY      0x00000001
#define B_DONE      0x00000002
#define B_ERROR     0x00000004
#define B_DELWRI    0x00000008
#define B_PHYS      0x00000010
#define B_READ      0x00000020
#define B_WRITE     0x00000040
#define B_ASYNC     0x00000080
#define B_INVAL     0x00000100
#define B_NOCACHE   0x00000200
#define B_CACHE     0x00000400

/* Buffer queue indexes */
#define BQ_LOCKED   0
#define BQ_CLEAN    1
#define BQ_DIRTY    2
#define BQ_EMPTY    3
#define BQ_COUNT    4

struct buf;
LIST_HEAD(bufhashhead, buf);
TAILQ_HEAD(bufqueue, buf);

typedef void (*biodone_t)(struct buf *bp);

struct buf {
    uint32_t            b_flags;
    void                *b_data;
    size_t              b_bcount;
    int64_t             b_blkno;
    int64_t             b_lblkno;
    struct vnode        *b_vp;
    struct ucred        *b_rcred;
    struct ucred        *b_wcred;
    size_t              b_resid;
    biodone_t           b_iodone;
    int                 b_error;

    int                 b_qindex;

    LIST_ENTRY(buf)     b_hash;
    TAILQ_ENTRY(buf)    b_freelist;
};

void bio_init(void);

struct buf *getblk(struct vnode *vp, int64_t blkno, size_t size, int slpflag, int slptimeo);
int bread(struct vnode *vp, int64_t blkno, size_t size, struct ucred *cred, struct buf **bpp);
int breada(struct vnode *vp, int64_t blkno, size_t size,
           int64_t rablkno, size_t rabsize, struct ucred *cred, struct buf **bpp);
int bwrite(struct buf *bp);
int bawrite(struct buf *bp);
void bdwrite(struct buf *bp);
void brelse(struct buf *bp);
struct buf *incore(struct vnode *vp, int64_t blkno);

int biowait(struct buf *bp);
void biodone(struct buf *bp);

int bufsync(int freq);
void syncer_daemon(void *arg);

int binval_vnode(struct vnode *vp, int save);

#endif /* _VFS_BUF_H */