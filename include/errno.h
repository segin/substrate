#ifndef _ERRNO_H
#define _ERRNO_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__linux__)
int *__errno_location(void);
#define errno (*__errno_location())
#elif defined(__FreeBSD__) || defined(__DragonFly__)
int *__error(void);
#define errno (*__error())
#elif defined(__NetBSD__)
int *__errno(void);
#define errno (*__errno())
#elif defined(__OpenBSD__)
int *__errno(void);
#define errno (*__errno())
#else
extern int errno;
#endif

#define EPERM        1  /* Operation not permitted */
#define ENOENT       2  /* No such file or directory */
#define ESRCH        3  /* No such process */
#define EINTR        4  /* Interrupted system call */
#define EIO          5  /* I/O error */
#define ENXIO        6  /* No such device or address */
#define E2BIG        7  /* Argument list too long */
#define ENOEXEC      8  /* Exec format error */
#define EBADF        9  /* Bad file number */
#define ECHILD      10  /* No child processes */
#define EAGAIN      11  /* Try again */
#define ENOMEM      12  /* Out of memory */
#define EACCES      13  /* Permission denied */
#define EFAULT      14  /* Bad address */
#define ENOTBLK     15  /* Block device required */
#define EBUSY       16  /* Device or resource busy */
#define EEXIST      17  /* File exists */
#define EXDEV       18  /* Cross-device link */
#define ENODEV      19  /* No such device */
#define ENOTDIR     20  /* Not a directory */
#define EISDIR      21  /* Is a directory */
#define EINVAL      22  /* Invalid argument */
#define ENFILE      23  /* File table overflow */
#define EMFILE      24  /* Too many open files */
#define ENOTTY      25  /* Not a typewriter */
#define ETXTBSY     26  /* Text file busy */
#define EFBIG       27  /* File too large */
#define ENOSPC      28  /* No space left on device */
#define ESPIPE      29  /* Illegal seek */
#define EROFS       30  /* Read-only file system */
#define EMLINK      31  /* Too many links */
#define EPIPE       32  /* Broken pipe */
#define EDOM        33  /* Math argument out of domain of func */
#define ERANGE      34  /* Math result not representable */
#define ENOSYS      38  /* Function not implemented */
#define ENOTEMPTY   39  /* Directory not empty */
#define EDEADLK     35  /* Resource deadlock would occur */
#define ENAMETOOLONG 63 /* File name too long */
#define ELOOP       62  /* Too many levels of symbolic links (matches kernel) */
#define EWOULDBLOCK EAGAIN  /* Operation would block */
#define EOWNERDEAD  130 /* Owner died */
#define ENOTRECOVERABLE 131 /* State not recoverable */
#define ETIMEDOUT   110 /* Connection timed out */
#define EOVERFLOW   75  /* Value too large for defined data type */
#define EUNKNOWNFS  514 /* Unknown filesystem type (sys_mount diagnostic) */

/*
 * POSIX network / IPC / streams errno values.  Numbers track the Linux
 * UAPI values so cross-personality binaries see the same constants
 * (Linux personality emulation in sys/exec/perso/ does the
 * Linux-to-substrate remap when those differ; for the network ones
 * they don't differ).  Substrate doesn't ship full BSD sockets today
 * but libstdc++'s <system_error> needs every one of these defined.
 */
#define EILSEQ           84  /* Illegal byte sequence */
#define EBADMSG          74  /* Bad message */
#define EIDRM            43  /* Identifier removed */
#define EMULTIHOP        72  /* Multihop attempted */
#define ENODATA          61  /* No data available */
#define ENOLINK          67  /* Link severed */
#define ENOMSG           42  /* No message of desired type */
/*
 * Substrate's ELOOP (62) and ENAMETOOLONG (63) historically diverge
 * from Linux UAPI (40, 36) because the kernel chose its own numbers
 * before we tracked Linux closely.  Map the late-arriving streams
 * codes around those collisions rather than renumbering the kernel.
 */
#define ENOSR            81  /* Out of streams resources */
#define ENOSTR           80  /* Device not a stream */
#define EPROTO           71  /* Protocol error */
#define ETIME            82  /* Timer expired */
#define EINPROGRESS     115  /* Operation now in progress */
#define EALREADY        114  /* Operation already in progress */
#define ENOTSOCK         88  /* Socket operation on non-socket */
#define EDESTADDRREQ     89  /* Destination address required */
#define EMSGSIZE         90  /* Message too long */
#define EPROTOTYPE       91  /* Protocol wrong type for socket */
#define ENOPROTOOPT      92  /* Protocol not available */
#define EPROTONOSUPPORT  93  /* Protocol not supported */
#define ESOCKTNOSUPPORT  94  /* Socket type not supported */
#define EOPNOTSUPP       95  /* Operation not supported on transport */
#define ENOLCK           37  /* No record locks available */
#define ENOTSUP          EOPNOTSUPP /* POSIX alias for EOPNOTSUPP */
#define EPFNOSUPPORT     96  /* Protocol family not supported */
#define EAFNOSUPPORT     97  /* Address family not supported by protocol */
#define EADDRINUSE       98  /* Address already in use */
#define EADDRNOTAVAIL    99  /* Cannot assign requested address */
#define ENETDOWN        100  /* Network is down */
#define ENETUNREACH     101  /* Network is unreachable */
#define ENETRESET       102  /* Network dropped connection because of reset */
#define ECONNABORTED    103  /* Software caused connection abort */
#define ECONNRESET      104  /* Connection reset by peer */
#define ENOBUFS         105  /* No buffer space available */
#define EISCONN         106  /* Transport endpoint is already connected */
#define ENOTCONN        107  /* Transport endpoint is not connected */
#define ESHUTDOWN       108  /* Cannot send after transport endpoint shutdown */
#define ETOOMANYREFS    109  /* Too many references: cannot splice */
#define ECONNREFUSED    111  /* Connection refused */
#define EHOSTDOWN       112  /* Host is down */
#define EHOSTUNREACH    113  /* No route to host */
#define ECANCELED       125  /* Operation canceled */
#define EDQUOT          122  /* Quota exceeded */
#define ESTALE           116 /* Stale file handle */

#ifdef __cplusplus
}
#endif
#endif
