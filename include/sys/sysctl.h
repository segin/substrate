/*
 * include/sys/sysctl.h - System control interface (sysctl) for userland
 *
 * Provides canonical declarations for sysctl(), sysctlbyname(), and sysctlnametomib()
 * functions, stable userspace data types, and standard sysctl identifiers.
 */

#ifndef _SYS_SYSCTL_H_
#define _SYS_SYSCTL_H_

#include <sys/types.h>
#include <stddef.h>

__BEGIN_DECLS

/*
 * System Control (sysctl) API
 *
 * These functions provide a standardized interface for querying and modifying
 * kernel parameters from userland. All functions are thread-safe and reentrant,
 * with no static mutable buffers and proper errno preservation.
 */

/**
 * sysctl - Query or modify kernel parameters using MIB (Management Information Base) path
 *
 * @param name      MIB path array (sequence of integers representing the sysctl hierarchy)
 * @param namelen   Length of the MIB path array
 * @param oldp      Pointer to buffer for storing old value (NULL to skip reading)
 * @param oldlenp   Pointer to variable containing size of oldp buffer (updated with actual size)
 * @param newp      Pointer to buffer containing new value (NULL to skip writing)
 * @param newlen    Size of the new value buffer
 *
 * @return 0 on success, -1 on failure with errno set appropriately
 *
 * Thread-safety: Safe. No static mutable buffers. Errno preserved per thread.
 * Reentrancy: Reentrant. Can be called recursively from signal handlers.
 */
int sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen);

/**
 * sysctlbyname - Query or modify kernel parameters using ASCII name
 *
 * @param name      Null-terminated ASCII string representing the sysctl name (e.g., "kern.hostname")
 * @param oldp      Pointer to buffer for storing old value (NULL to skip reading)
 * @param oldlenp   Pointer to variable containing size of oldp buffer (updated with actual size)
 * @param newp      Pointer to buffer containing new value (NULL to skip writing)
 * @param newlen    Size of the new value buffer
 *
 * @return 0 on success, -1 on failure with errno set appropriately
 *
 * Thread-safety: Safe. No static mutable buffers. Errno preserved per thread.
 * Reentrancy: Reentrant. Can be called recursively from signal handlers.
 */
int sysctlbyname(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen);

/**
 * sysctlnametomib - Convert ASCII sysctl name to MIB (Management Information Base) path
 *
 * @param name      Null-terminated ASCII string representing the sysctl name
 * @param mibp      Pointer to buffer for storing the MIB path (NULL to only get size)
 * @param sizep     Pointer to variable containing size of mibp buffer (updated with actual size)
 *
 * @return 0 on success, -1 on failure with errno set appropriately
 *
 * Thread-safety: Safe. No static mutable buffers. Errno preserved per thread.
 * Reentrancy: Reentrant. Can be called recursively from signal handlers.
 */
int sysctlnametomib(const char *name, int *mibp, size_t *sizep);

/*
 * Top-level sysctl identifiers (CTL_*)
 *
 * These constants define the root of the sysctl hierarchy.
 */

#define CTL_SYSCTL     0   /* "magic" sysctl names (for sysctl internal operations) */
#define CTL_KERN       1   /* General kernel info and control */
#define CTL_VM         2   /* VM management */
#define CTL_HW         3   /* Hardware configuration */
#define CTL_MACHDEP    4   /* Machine dependent */
#define CTL_USER       5   /* User-level configuration */
#define CTL_DEBUG      6   /* Debugging */

#define CTL_MAXID      7   /* Maximum number of top-level identifiers */
#define CTL_MAXNAME    12  /* Maximum length of an ASCII sysctl name */

/*
 * CTL_SYSCTL sub-identifiers (CTL_SYSCTL_*)
 *
 * These are used for sysctl internal operations.
 */

#define CTL_SYSCTL_DEBUG    0   /* Debugging control */
#define CTL_SYSCTL_NAME     1   /* Name lookup operations */
#define CTL_SYSCTL_NEXT     2   /* Next OID traversal */
#define CTL_SYSCTL_NAME2OID 3   /* Name to OID conversion */
#define CTL_SYSCTL_OIDFMT   4   /* OID format information */
#define CTL_SYSCTL_OIDDESCR 5   /* OID description */
#define CTL_SYSCTL_OIDLABEL 6   /* OID label */

/*
 * CTL_KERN sub-identifiers (KERN_*)
 *
 * General kernel information and control parameters.
 */

#define KERN_OSTYPE         1   /* Operating system type (e.g., "Substrate") */
#define KERN_OSRELEASE      2   /* Operating system release (e.g., "1.0.0") */
#define KERN_OSREV          3   /* Operating system revision */
#define KERN_VERSION        4   /* Kernel version string */
#define KERN_MAXVNODES      5   /* Maximum number of vnodes */
#define KERN_MAXPROC        6   /* Maximum number of processes */
#define KERN_MAXFILES       7   /* Maximum number of open files */
#define KERN_ARGMAX         8   /* Maximum argument size for exec */
#define KERN_SECURELVL      9   /* Secure level */
#define KERN_HOSTNAME       10  /* Hostname */
#define KERN_HOSTID         11  /* Host ID */
#define KERN_CLOCKRATE      12  /* Clock rate information */
#define KERN_VNODE          13  /* Vnode management */
#define KERN_PROC           14  /* Process information */
#define KERN_FILE           15  /* File information */
#define KERN_PROF           16  /* Profiling control */
#define KERN_POSIX1         17  /* POSIX.1 compliance */
#define KERN_NGROUPS        18  /* Maximum number of groups per user */
#define KERN_JOB_CONTROL    19  /* Job control support */
#define KERN_SAVED_IDS      20  /* Saved IDs support */
#define KERN_BOOTTIME       21  /* Boot time */
#define KERN_DOMAINNAME     22  /* Domain name */
#define KERN_MAXPARTITIONS  23  /* Maximum number of disk partitions */

/*
 * CTL_HW sub-identifiers (HW_*)
 *
 * Hardware configuration parameters.
 */

#define HW_MACHINE          1   /* Machine type (e.g., "i386") */
#define HW_MODEL            2   /* Machine model */
#define HW_NCPU             3   /* Number of CPUs */
#define HW_BYTEORDER        4   /* Byte order (0 = little-endian, 1 = big-endian) */
#define HW_PHYSMEM          5   /* Physical memory size in bytes */
#define HW_USERMEM          6   /* Usable memory size in bytes */
#define HW_PAGESIZE         7   /* Page size in bytes */

/*
 * CTL_USER sub-identifiers (USER_*)
 *
 * User-level configuration parameters (POSIX.2 limits).
 */

#define USER_CS_PATH            1   /* Character set path */
#define USER_BC_BASE_MAX        2   /* Maximum bc base */
#define USER_BC_DIM_MAX         3   /* Maximum bc array dimension */
#define USER_BC_SCALE_MAX       4   /* Maximum bc scale */
#define USER_BC_STRING_MAX      5   /* Maximum bc string length */
#define USER_COLL_WEIGHTS_MAX   6   /* Maximum collation weights */
#define USER_EXPR_NEST_MAX      7   /* Maximum expression nesting */
#define USER_LINE_MAX           8   /* Maximum line length */
#define USER_RE_DUP_MAX         9   /* Maximum regex duplicate count */
#define USER_POSIX2_VERSION     10  /* POSIX.2 version */
#define USER_POSIX2_C_BIND      11  /* POSIX.2 C bindings */
#define USER_POSIX2_CXX_BIND    12  /* POSIX.2 C++ bindings */
#define USER_POSIX2_FORT_BIND   13  /* POSIX.2 Fortran bindings */
#define USER_POSIX2_INT64_VERSION 14 /* POSIX.2 int64 version */
#define USER_POSIX2_LOCT_BIND   15  /* POSIX.2 localization bindings */
#define USER_POSIX2_LOCALEDEF   16  /* POSIX.2 locale definition */
#define USER_POSIX2_SW_DEV      17  /* POSIX.2 software development */
#define USER_POSIX2_UPE         18  /* POSIX.2 user portability */
#define USER_STREAM_MAX         19  /* Maximum stream size */
#define USER_TZNAME_MAX         20  /* Maximum timezone name length */

/*
 * CTL_VM sub-identifiers (VM_*)
 *
 * Virtual memory management parameters.
 * (Currently minimal; will be expanded as VM system matures)
 */

/*
 * CTL_MACHDEP sub-identifiers (MACHDEP_*)
 *
 * Machine dependent parameters.
 */

/*
 * CTL_DEBUG sub-identifiers (DEBUG_*)
 *
 * Debugging parameters.
 */

/*
 * Compile-time ABI stability checks
 *
 * These static assertions ensure that the sizes and alignments of key
 * structures and types do not change unexpectedly, preserving ABI stability.
 */

/* Verify that u_int is 32-bit */
_Static_assert(sizeof(u_int) == 4, "u_int must be 32-bit");

/* Verify that int is 32-bit */
_Static_assert(sizeof(int) == 4, "int must be 32-bit");

__END_DECLS

#endif /* _SYS_SYSCTL_H_ */