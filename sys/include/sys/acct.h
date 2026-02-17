#ifndef _SUBSTRATE_SYS_ACCT_H
#define _SUBSTRATE_SYS_ACCT_H

#include <stdint.h>
#include <sys/types.h>

#define AC_COMM_LEN 16

/*
 * Accounting flags
 */
#define AFORK   0x01    /* has executed fork, but no exec */
#define ASU     0x02    /* used super-user privileges */
#define ACOMP   0x04    /* used compatibility mode */
#define AXSIG   0x08    /* killed by a signal */

/*
 * Legacy comp_t: 3 bits base-8 exponent, 13 bits fraction.
 * Represents "ticks".
 */
typedef uint16_t comp_t;

struct acct {
    char      ac_comm[AC_COMM_LEN]; /* Command name */
    comp_t    ac_utime;             /* User time */
    comp_t    ac_stime;             /* System time */
    comp_t    ac_etime;             /* Elapsed time */
    uint32_t  ac_btime;             /* Beginning time */
    uid_t     ac_uid;               /* User ID */
    gid_t     ac_gid;               /* Group ID */
    uint16_t  ac_mem;               /* Memory usage (average) */
    comp_t    ac_io;                /* Chars transferred */
    uint8_t   ac_tty;               /* Control Typewriter */
    uint8_t   ac_flag;              /* Accounting Flags */
};

int sys_acct(const char *path);
void acct_process(int exitcode);

#endif
