/*
 * <linux/vt.h> — VT switching ioctl compat layer.
 *
 * substrate's VT abstraction (sys/vt.h) doesn't have signal-driven
 * VT switching (no VT_ACQUIRE_SIG / VT_RELEASE_SIG to deliver on a
 * switch).  ioctls here are accepted by the kernel and turn into
 * no-ops at runtime — calling code (kdrive's linux.c) gets enough
 * back to limp along: VT_GETMODE returns "auto", VT_SETMODE / VT_
 * ACTIVATE / VT_WAITACTIVE return 0 immediately.  Active-VT
 * switching is handled by substrate's KDSETMODE flow, not the
 * Linux signal protocol.
 */
#ifndef _LINUX_VT_H
#define _LINUX_VT_H

#define MIN_NR_CONSOLES   1
#define MAX_NR_CONSOLES   63
#define MAX_NR_USER_CONSOLES 63

#define VT_OPENQRY        0x5600
#define VT_GETMODE        0x5601
#define VT_SETMODE        0x5602
#define VT_GETSTATE       0x5603
#define VT_SENDSIG        0x5604
#define VT_RELDISP        0x5605
#define VT_ACTIVATE       0x5606
#define VT_WAITACTIVE     0x5607
#define VT_DISALLOCATE    0x5608
#define VT_RESIZE         0x5609
#define VT_RESIZEX        0x560A
#define VT_LOCKSWITCH     0x560B
#define VT_UNLOCKSWITCH   0x560C

#define VT_AUTO           0x00
#define VT_PROCESS        0x01
#define VT_ACKACQ         0x02

#define VT_ACQUIRE        2  /* signal value */
#define VT_RELEASE        1  /* signal value */

struct vt_mode {
    char  mode;     /* VT_AUTO / VT_PROCESS */
    char  waitv;    /* unused */
    short relsig;   /* signal to send when releasing */
    short acqsig;   /* signal to send when acquiring */
    short frsig;    /* unused */
};

struct vt_stat {
    unsigned short v_active;   /* active VT number */
    unsigned short v_signal;   /* unused */
    unsigned short v_state;    /* unused */
};

struct vt_sizes {
    unsigned short v_rows;
    unsigned short v_cols;
    unsigned short v_scrollsize;
};

#endif /* _LINUX_VT_H */
