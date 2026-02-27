/*
 * sys/sys/sysctl.h
 *
 * System control interface (sysctl).
 */

#ifndef _SYS_SYSCTL_H
#define _SYS_SYSCTL_H

#include <sys/types.h>

/* Top-level identifiers */
#define CTL_KERN    1   /* General kernel info and control */
#define CTL_VM      2   /* VM management */
#define CTL_HW      3   /* Hardware config */
#define CTL_MACHDEP 4   /* Machine dependent */
#define CTL_USER    5   /* User-level config */
#define CTL_DEBUG   6   /* Debugging */
#define CTL_SYSCTL  0   /* "magic" sysctl names */
#define CTL_MAXID   7

#define CTL_MAXNAME 12

/* CTL_SYSCTL identifiers */
#define CTL_SYSCTL_DEBUG    0
#define CTL_SYSCTL_NAME     1
#define CTL_SYSCTL_NEXT     2
#define CTL_SYSCTL_NAME2OID 3
#define CTL_SYSCTL_OIDFMT   4
#define CTL_SYSCTL_OIDDESCR 5
#define CTL_SYSCTL_OIDLABEL 6


/* CTL_KERN identifiers */
#define KERN_OSTYPE      1
#define KERN_OSRELEASE   2
#define KERN_OSREV       3
#define KERN_VERSION     4
#define KERN_MAXVNODES   5
#define KERN_MAXPROC     6
#define KERN_MAXFILES    7
#define KERN_ARGMAX      8
#define KERN_SECURELVL   9
#define KERN_HOSTNAME    10
#define KERN_HOSTID      11
#define KERN_CLOCKRATE   12
#define KERN_VNODE       13
#define KERN_PROC        14
#define KERN_FILE        15
#define KERN_PROF        16
#define KERN_POSIX1      17
#define KERN_NGROUPS     18
#define KERN_JOB_CONTROL 19
#define KERN_SAVED_IDS   20
#define KERN_BOOTTIME    21
#define KERN_DOMAINNAME  22
#define KERN_MAXPARTITIONS 23

/* CTL_HW identifiers */
#define HW_MACHINE       1
#define HW_MODEL         2
#define HW_NCPU          3
#define HW_BYTEORDER     4
#define HW_PHYSMEM       5
#define HW_USERMEM       6
#define HW_PAGESIZE      7

/* CTL_USER identifiers */
#define USER_CS_PATH            1
#define USER_BC_BASE_MAX        2
#define USER_BC_DIM_MAX         3
#define USER_BC_SCALE_MAX       4
#define USER_BC_STRING_MAX      5
#define USER_COLL_WEIGHTS_MAX   6
#define USER_EXPR_NEST_MAX      7
#define USER_LINE_MAX           8
#define USER_RE_DUP_MAX         9
#define USER_POSIX2_VERSION     10
#define USER_POSIX2_C_BIND      11
#define USER_POSIX2_CXX_BIND    12
#define USER_POSIX2_FORT_BIND   13
#define USER_POSIX2_INT64_VERSION 14
#define USER_POSIX2_LOCT_BIND   15
#define USER_POSIX2_LOCALEDEF   16
#define USER_POSIX2_SW_DEV      17
#define USER_POSIX2_UPE         18
#define USER_STREAM_MAX         19
#define USER_TZNAME_MAX         20

/*
 * Sysctl handling
 */
struct sysctl_oid;
struct sysctl_req;

/*
 * Definition of the oid_handler function type.
 * Returns 0 on success, errno on failure.
 */
typedef int (*ctl_handler_t)(struct sysctl_oid *oidp, void *arg1, int arg2, struct sysctl_req *req);

/* List of OIDs */
struct sysctl_oid_list {
    struct sysctl_oid *slh_first;
};

/* OID Structure */
struct sysctl_oid {
    struct sysctl_oid_list *oid_parent;
    struct sysctl_oid      *oid_link;   /* Next in list */
    const char             *oid_name;
    int                     oid_number;
    int                     oid_kind;
    void                   *oid_arg1;
    int                     oid_arg2;
    const char             *oid_fmt;
    ctl_handler_t           oid_handler;
    const char             *oid_descr;
    int                     oid_refcnt; /* Reference count */
};

#define CTLTYPE_NODE    1
#define CTLTYPE_INT     2
#define CTLTYPE_STRING  3
#define CTLTYPE_QUAD    4
#define CTLTYPE_OPAQUE  5
#define CTLTYPE_STRUCT  CTLTYPE_OPAQUE
#define CTLTYPE_UINT    6
#define CTLTYPE_LONG    7
#define CTLTYPE_ULONG   8

#define CTLFLAG_RD      0x80000000
#define CTLFLAG_WR      0x40000000
#define CTLFLAG_RW      (CTLFLAG_RD|CTLFLAG_WR)
#define CTLFLAG_DYN     0x20000000  /* Dynamically allocated */
#define CTLFLAG_ANYBODY 0x10000000  /* Accessible by any user */
#define CTLFLAG_SECURE  0x08000000  /* Require securelevel check */
#define CTLFLAG_MASK    0xf8000000
#define CTLTYPE_MASK    0x0000000f

/*
 * Request structure passed to handler
 */
struct sysctl_req {
    void    *oldptr;
    size_t   oldlen;
    size_t   oldidx; /* Current read index */
    void    *newptr;
    size_t   newlen;
    size_t   newidx; /* Current write index */
    pid_t    p_pid;  /* Process ID of caller */
    uid_t    p_uid;  /* User ID of caller */
    int      validlen; /* Amount of valid data available (for string formatting etc) */
    int      lock;   /* 1 if we hold lock */
};

/*
 * Helper functions
 */
int sysctl_handle_int(struct sysctl_oid *oidp, void *arg1, int arg2, struct sysctl_req *req);
int sysctl_handle_long(struct sysctl_oid *oidp, void *arg1, int arg2, struct sysctl_req *req);
int sysctl_handle_string(struct sysctl_oid *oidp, void *arg1, int arg2, struct sysctl_req *req);
int sysctl_handle_opaque(struct sysctl_oid *oidp, void *arg1, int arg2, struct sysctl_req *req);

/*
 * Registration functions
 */
void sysctl_init(void);
void sysctl_register_oid(struct sysctl_oid *oidp);
void sysctl_unregister_oid(struct sysctl_oid *oidp);
struct sysctl_oid *sysctl_find_oid(int *name, unsigned int namelen, struct sysctl_oid *root);

/*
 * Macros for defining sysctl entries
 */
#define SYSCTL_DECL(name) \
    extern struct sysctl_oid_list sysctl_##name##_children

#define SYSCTL_NODE(parent, nbr, name, access, handler, descr) \
    struct sysctl_oid_list sysctl_##parent##_##name##_children; \
    struct sysctl_oid sysctl_##parent##_##name = { \
        &sysctl_##parent##_children, NULL, #name, nbr, \
        CTLTYPE_NODE|(access), \
        (void*)&sysctl_##parent##_##name##_children, 0, NULL, handler, descr, 0 \
    };

/* Standard Types */
#define SYSCTL_INT(parent, nbr, name, access, ptr, val, descr) \
    struct sysctl_oid sysctl_##parent##_##name = { \
        &sysctl_##parent##_children, NULL, #name, nbr, \
        CTLTYPE_INT|(access), \
        ptr, val, "I", sysctl_handle_int, descr, 0 \
    }

#define SYSCTL_STRING(parent, nbr, name, access, ptr, len, descr) \
    struct sysctl_oid sysctl_##parent##_##name = { \
        &sysctl_##parent##_children, NULL, #name, nbr, \
        CTLTYPE_STRING|(access), \
        ptr, len, "A", sysctl_handle_string, descr, 0 \
    }
    
#define SYSCTL_OPAQUE(parent, nbr, name, access, ptr, len, fmt, descr) \
    struct sysctl_oid sysctl_##parent##_##name = { \
        &sysctl_##parent##_children, NULL, #name, nbr, \
        CTLTYPE_OPAQUE|(access), \
        ptr, len, fmt, sysctl_handle_opaque, descr, 0 \
    }

/* Root nodes declaration */
SYSCTL_DECL(kern);
SYSCTL_DECL(vm);
SYSCTL_DECL(hw);
SYSCTL_DECL(debug);

#endif /* _SYS_SYSCTL_H */
