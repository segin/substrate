/*
 * sys/compat32.h - user-side layouts of structures that carry pointers
 *
 * A process today is a 32-bit (i386 ABI) one on either kernel
 * (<sys/abi32.h>).  Structures it exchanges with the kernel by value are
 * declared in the i386 layout directly; the ones below cannot be, because
 * the kernel keeps real pointers in them -- a signal handler, a thread's
 * start function, the base of an I/O vector -- which are 64 bits wide on
 * the x86_64 kernel.  So the kernel keeps its native structure and these
 * are the user's view of it, converted where it crosses the boundary by
 * the helpers in kern/compat32.c.
 *
 * Every field is fixed-width and each structure is pinned to its i386 size,
 * so the layout is the same on both kernels and the conversion is exact on
 * the i386 one (where it amounts to a copy).  A user pointer is a uptr32_t:
 * the 32-bit address as the process stores it.
 *
 * The device and socket ioctl structures further down are here for a second
 * reason: their declarations (<sys/fb.h>, <sys/input.h>, <net/if.h>,
 * <sys/usbdevfs.h>) are shared with userland and spell their fields in
 * native C types -- long, pointers -- that cannot change without changing
 * the userland headers.  The kernel uses these twins wherever it exchanges
 * such a structure with the process instead.
 */
#ifndef _SYS_COMPAT32_H
#define _SYS_COMPAT32_H

#include <stdint.h>
#include <stddef.h>
#include <sys/abi32.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/thr.h>
#include <sys/uio.h>
#include <sys/usbdevfs.h>

struct sigaction32 {
    uptr32_t sa_handler;
    uint32_t sa_mask;
    int32_t  sa_flags;
};
ABI32_ASSERT_SIZE(struct sigaction32, 12);

typedef struct {
    uptr32_t ss_sp;
    int32_t  ss_flags;
    uint32_t ss_size;
} stack32_t;
ABI32_ASSERT_SIZE(stack32_t, 12);

union sigval32 {
    int32_t  sival_int;
    uptr32_t sival_ptr;
};

typedef struct {
    int32_t  si_signo;
    int32_t  si_errno;
    int32_t  si_code;
    int32_t  si_pid;
    uint32_t si_uid;
    uptr32_t si_addr;
    int32_t  si_status;
    union sigval32 si_value;
    int32_t  _pad[25];
} siginfo32_t;
ABI32_ASSERT_SIZE(siginfo32_t, 132);

struct sigevent32 {
    int32_t  sigev_notify;
    int32_t  sigev_signo;
    union sigval32 sigev_value;
    uptr32_t sigev_notify_function;
    uptr32_t sigev_notify_attributes;
};
ABI32_ASSERT_SIZE(struct sigevent32, 20);

struct thr_param32 {
    uptr32_t start_func;
    uptr32_t arg;
    uptr32_t stack_base;
    uint32_t stack_size;
    uptr32_t tls_base;
    uint32_t tls_size;
    uptr32_t child_tid;     /* int32_t * in the process */
    uptr32_t parent_tid;
    int32_t  flags;
};
ABI32_ASSERT_SIZE(struct thr_param32, 36);

struct iovec32 {
    uptr32_t iov_base;
    uint32_t iov_len;
};
ABI32_ASSERT_SIZE(struct iovec32, 8);

struct msghdr32 {
    uptr32_t msg_name;
    int32_t  msg_namelen;
    uptr32_t msg_iov;
    int32_t  msg_iovlen;
    uptr32_t msg_control;
    int32_t  msg_controllen;
    int32_t  msg_flags;
};
ABI32_ASSERT_SIZE(struct msghdr32, 28);

/* struct robust_list_head (<sys/futex.h>) as the process lays it out; the
 * list it heads is a chain of 32-bit next pointers. */
struct robust_list_head32 {
    uptr32_t list_next;
    int32_t  futex_offset;
    uptr32_t list_op_pending;
};
ABI32_ASSERT_SIZE(struct robust_list_head32, 12);

/* FBIOGET_FSCREENINFO: struct fb_fix_screeninfo (<sys/fb.h>), whose two
 * unsigned longs are 32 bits in the process. */
struct fb_fix_screeninfo32 {
    char     id[16];
    uint32_t smem_start;
    uint32_t smem_len;
    uint32_t type;
    uint32_t type_aux;
    uint32_t visual;
    uint16_t xpanstep;
    uint16_t ypanstep;
    uint16_t ywrapstep;
    uint32_t line_length;
    uint32_t mmio_start;
    uint32_t mmio_len;
    uint32_t accel;
    uint16_t reserved[3];
};
ABI32_ASSERT_SIZE(struct fb_fix_screeninfo32, 68);

/* FBIOGET_VIDEO_MODES: struct video_mode_query (<sys/fb.h>).  `modes`
 * points at the process's array of struct video_mode_info, which has no
 * pointers and the same layout on both kernels. */
struct video_mode_query32 {
    uint32_t count;
    uptr32_t modes;
};
ABI32_ASSERT_SIZE(struct video_mode_query32, 8);

/* A record read from /dev/input/event0: struct input_event (<sys/input.h>),
 * the i386 Linux evdev layout with a 32-bit long seconds/microseconds. */
struct input_event32 {
    int32_t  time_sec;
    int32_t  time_usec;
    uint16_t type;
    uint16_t code;
    int32_t  value;
};
ABI32_ASSERT_SIZE(struct input_event32, 16);

/*
 * The interface ioctls: struct ifreq and struct ifconf (<net/if.h>).  The
 * union members keep their <net/if.h> names so its ifr_addr, ifr_flags,
 * ifc_req, ... accessor macros apply to these as well.  ifru_map stands in
 * for struct ifmap, whose two longs make it 16 bytes in the process.
 */
struct ifreq32 {
    char ifr_name[16];                  /* IFNAMSIZ */
    union {
        struct sockaddr ifru_addr;
        struct sockaddr ifru_dstaddr;
        struct sockaddr ifru_broadaddr;
        struct sockaddr ifru_netmask;
        struct sockaddr ifru_hwaddr;
        int16_t         ifru_flags;
        int32_t         ifru_ivalue;
        int32_t         ifru_mtu;
        uint8_t         ifru_map[16];
        char            ifru_slave[16];
        char            ifru_newname[16];
        uptr32_t        ifru_data;
    } ifr_ifru;
};
ABI32_ASSERT_SIZE(struct ifreq32, 32);

struct ifconf32 {
    int32_t ifc_len;
    union {
        uptr32_t ifcu_buf;
        uptr32_t ifcu_req;              /* struct ifreq32 * in the process */
    } ifc_ifcu;
};
ABI32_ASSERT_SIZE(struct ifconf32, 8);

/* USBDEVFS_CONTROL: struct usbdevfs_ctrltransfer (<sys/usbdevfs.h>).  The
 * request number encodes the argument's size, so the process's number is
 * the one built from this layout. */
struct usbdevfs_ctrltransfer32 {
    uint8_t  bRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
    uint32_t timeout;
    uptr32_t data;
};
ABI32_ASSERT_SIZE(struct usbdevfs_ctrltransfer32, 16);

#define USBDEVFS_CONTROL32 _IOWR('U', 0, struct usbdevfs_ctrltransfer32)

/*
 * Copy a structure in from, or out to, the process at `uaddr`, converting
 * between its layout and the kernel's.  Each returns 0 or EFAULT, as
 * copyin()/copyout() do.
 */
int sigaction_copyin(const void *uaddr, struct sigaction *k);
int sigaction_copyout(const struct sigaction *k, void *uaddr);
int stack_copyin(const void *uaddr, stack_t *k);
int stack_copyout(const stack_t *k, void *uaddr);
int sigevent_copyin(const void *uaddr, struct sigevent *k);
int thr_param_copyin(const void *uaddr, struct thr_param *k);
int siginfo_copyout(const siginfo_t *k, void *uaddr);
int msghdr_copyin(const void *uaddr, struct msghdr *k);
int msghdr_copyout(const struct msghdr *k, void *uaddr);

/* `count` user I/O vectors at `uaddr` into k[0..count). */
int iovec_copyin(const void *uaddr, struct iovec *k, int count);

/*
 * recvmsg() writes three fields back into the process's msghdr; these are
 * their addresses in the layout the process uses.
 */
struct msghdr_out {
    void *msg_namelen;      /* socklen_t */
    void *msg_controllen;   /* socklen_t */
    void *msg_flags;        /* int */
};
void msghdr_out_fields(void *uaddr, struct msghdr_out *out);

/*
 * The scalars and small records a handler reads or writes through a user
 * pointer in the middle of its work, whose width follows the process: a
 * size_t or a pointer (32 bits, or 64 for a native 64-bit process), and
 * struct timespec and struct sched_param (the kernel's i386 layout, or the
 * LP64 one of <sys/amd64_abi.h>).
 */
struct sched_param;
struct timespec;
int usize_copyin(const void *uaddr, size_t *k);
int usize_copyout(size_t v, void *uaddr);
int uptr_copyout(uintptr_t v, void *uaddr);
int timespec_copyin(const void *uaddr, struct timespec *k);
int timespec_copyout(const struct timespec *k, void *uaddr);
int sched_param_copyin(const void *uaddr, struct sched_param *k);
int sched_param_copyout(const struct sched_param *k, void *uaddr);

/* Element `index` of the process's sys_map_t / sys_swapinfo_t array at
 * `uarray` (<sys/sysinfo.h>). */
struct sys_map;
struct sys_swapinfo;
int sys_map_copyout(const struct sys_map *k, void *uarray, size_t index);
int sys_swapinfo_copyout(const struct sys_swapinfo *k, void *uarray,
                         size_t index);

/* The process's layout of a siginfo, for building a signal frame. */
void siginfo_to32(const siginfo_t *k, siginfo32_t *u);
void stack_to32(const stack_t *k, stack32_t *u);

#endif /* _SYS_COMPAT32_H */
