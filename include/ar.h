/*
 * <ar.h> — Unix `ar` archive file format (4.4BSD / POSIX.1-2017).
 *
 * Static libraries (libfoo.a), GCC's intermediate archives, and the
 * .deb format all wrap their members in an ar(5) container.  An ar
 * archive begins with the eight-byte magic ARMAG, followed by one
 * struct ar_hdr per member.  Each header is exactly 60 bytes of
 * ASCII fields — name, timestamp, owner uid, owner gid, file mode,
 * size, magic — none NUL-terminated.  The terminator characters are
 * spaces (0x20); the final two-byte magic `ARFMAG` ("`\n") closes
 * the header and prevents accidental scanning past it.
 */
#ifndef _AR_H
#define _AR_H

#ifdef __cplusplus
extern "C" {
#endif

#define ARMAG    "!<arch>\n"   /* 8-byte archive magic at file start */
#define SARMAG   8             /* size of ARMAG */

#define ARFMAG   "`\n"         /* 2-byte trailer of each ar_hdr */

struct ar_hdr {
    char ar_name[16];          /* member name, NUL-padded with spaces */
    char ar_date[12];          /* mtime, decimal ASCII */
    char ar_uid[6];            /* owner uid, decimal ASCII */
    char ar_gid[6];            /* owner gid, decimal ASCII */
    char ar_mode[8];           /* mode, octal ASCII */
    char ar_size[10];          /* member size, decimal ASCII */
    char ar_fmag[2];           /* ARFMAG */
};

#ifdef __cplusplus
}
#endif

#endif /* _AR_H */
