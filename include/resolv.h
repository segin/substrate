/*
 * <resolv.h> — DNS stub-resolver API (BIND-historical).
 *
 * Implementation in lib/resolv/ which builds libresolv.{a,so.0}.
 * Substrate's resolver supports IPv4 + IPv6 nameservers configured
 * in /etc/resolv.conf, UDP-only, and parses A / AAAA / CNAME / MX /
 * SRV / TXT / SSHFP / RRSIG / DNSKEY / DS responses.  TCP fallback
 * (for truncated responses) and DNSSEC validation are NOT done.
 */

#ifndef _RESOLV_H
#define _RESOLV_H

#include <sys/types.h>
#include <netinet/in.h>
#include <stdint.h>
#include <arpa/nameser.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximums.  Match BIND-9 historical caps. */
#define MAXNS              3        /* maximum nameservers in resolv.conf */
#define MAXDFLSRCH         3        /* maximum search domains */
#define MAXDNSRCH          6
#define LOCALDOMAINPARTS   2
#define RES_TIMEOUT        5        /* default per-query timeout (seconds) */
#define RES_DFLRETRY       2        /* default attempts per server */
#define RES_MAXTIME        45       /* total wall clock cap */

/* Option bits for res_state.options.  RES_INIT is set by res_init()
 * once parsing succeeds; callers test it to avoid re-parsing. */
#define RES_INIT       0x00000001
#define RES_DEBUG      0x00000002
#define RES_USEVC      0x00000008   /* force TCP (unimplemented) */
#define RES_DEFNAMES   0x00000080   /* append local domain to single labels */
#define RES_DNSRCH     0x00000200   /* search path of domains */
#define RES_RECURSE    0x00000001   /* recursion desired bit in queries */

typedef struct __res_state {
    int               retrans;            /* retransmission timeout */
    int               retry;              /* attempts per server */
    unsigned long     options;            /* RES_* flag bits */
    int               nscount;            /* nameservers configured */
    struct sockaddr_in  nsaddr_list[MAXNS];
    unsigned short    id;                 /* current query ID */
    char             *dnsrch[MAXDNSRCH + 1];
    char              defdname[256];      /* default domain */
    unsigned int      ndots;              /* # dots before search */
    int               nsort;              /* number of sortlist entries */
    int               _u[37];             /* opaque pad — keep ABI stable */
} *res_state;

/* Global resolver state.  The historical BIND API is to expose a
 * single `_res` struct.  Substrate provides it as a pointer-returning
 * accessor so callers don't have to relink when the struct grows;
 * the legacy `_res` symbol is also exported pointing at the same
 * thread-shared instance. */
struct __res_state *__res_state(void);
#define _res          (*__res_state())

/* Initialization: parse /etc/resolv.conf into `_res`.  Returns 0 on
 * success, -1 on error.  Subsequent calls are no-ops if RES_INIT is
 * already set in `_res.options`. */
int res_init(void);

/*
 * res_query — send a DNS query for (dname, class, type), copy the
 * response into `answer` (up to anslen bytes), return the response
 * length on success or -1 on error.  Sets h_errno on failure.
 */
int res_query(const char *dname, int rrclass, int type,
              unsigned char *answer, int anslen);

/* Same as res_query but appends the search-list domains. */
int res_search(const char *dname, int rrclass, int type,
               unsigned char *answer, int anslen);

/* Compose a query packet without sending it.  Returns the packet
 * length or -1 on error. */
int res_mkquery(int op, const char *dname, int rrclass, int type,
                const unsigned char *data, int datalen,
                const unsigned char *newrr, unsigned char *buf, int buflen);

/* Send an already-composed query packet.  Returns the response
 * length or -1.  This is the path res_query() uses internally. */
int res_send(const unsigned char *query, int querylen,
             unsigned char *answer, int anslen);

/* Decompress a domain name in `src` (within message [msg, eomorig))
 * into `dst` of size `dstsiz`.  Returns the number of bytes consumed
 * from `src` or -1.  Matches BIND-historical signature. */
int dn_expand(const unsigned char *msg, const unsigned char *eomorig,
              const unsigned char *src, char *dst, int dstsiz);

/* Walk past one compressed name in a packet without expanding it.
 * Returns the byte count or -1. */
int dn_skipname(const unsigned char *src, const unsigned char *eom);

/* h_errno values (the "host_errno" reported by gethostbyname /
 * res_query).  Substrate stores it in a thread-local slot reached
 * by __h_errno(). */
extern int *__h_errno(void);
#define h_errno   (*__h_errno())

#define NETDB_INTERNAL  (-1)
#define HOST_NOT_FOUND  1
#define TRY_AGAIN       2
#define NO_RECOVERY     3
#define NO_DATA         4
#define NO_ADDRESS      NO_DATA

/*
 * getrrsetbyname — the API OpenSSH's DNSSEC fingerprint code uses.
 * Returns ERRSET_SUCCESS and populates *rrset on success; caller
 * must free via freerrset().
 */
struct rdatainfo {
    unsigned int  rdi_length;
    unsigned char *rdi_data;
};
struct rrsetinfo {
    unsigned int  rri_flags;
    unsigned int  rri_rdclass;
    unsigned int  rri_rdtype;
    unsigned int  rri_ttl;
    unsigned int  rri_nrdatas;
    unsigned int  rri_nsigs;
    char         *rri_name;
    struct rdatainfo *rri_rdatas;
    struct rdatainfo *rri_sigs;
};
#define RRSET_VALIDATED  0x0001
#define RRSET_FORCE_EDNS0 0x0001

#define ERRSET_SUCCESS   0
#define ERRSET_NOMEMORY  1
#define ERRSET_FAIL      2
#define ERRSET_INVAL     3
#define ERRSET_NONAME    4
#define ERRSET_NODATA    5

int  getrrsetbyname(const char *hostname, unsigned int rdclass,
                    unsigned int rdtype, unsigned int flags,
                    struct rrsetinfo **res);
void freerrset(struct rrsetinfo *rrset);

#ifdef __cplusplus
}
#endif

#endif /* _RESOLV_H */
