/*
 * res_query.c + res_search.c — the high-level resolver entry points.
 */

#include <arpa/nameser.h>
#include <resolv.h>
#include <string.h>

int res_query(const char *dname, int rrclass, int type,
              unsigned char *answer, int anslen) {
    unsigned char query[NS_PACKETSZ];
    if ((_res.options & RES_INIT) == 0 && res_init() < 0) return -1;
    int qlen = res_mkquery(ns_o_query, dname, rrclass, type,
                           NULL, 0, NULL, query, sizeof(query));
    if (qlen < 0) { h_errno = NETDB_INTERNAL; return -1; }
    int rlen = res_send(query, qlen, answer, anslen);
    if (rlen < (int)NS_HFIXEDSZ) return -1;

    /* Inspect rcode. */
    int rcode = answer[3] & 0x0F;
    if (rcode == ns_r_nxdomain) { h_errno = HOST_NOT_FOUND; return -1; }
    if (rcode == ns_r_servfail || rcode == ns_r_notimpl ||
        rcode == ns_r_refused)  { h_errno = TRY_AGAIN; return -1; }
    if (rcode == ns_r_formerr)  { h_errno = NO_RECOVERY; return -1; }

    /* ancount == 0 with NOERROR == NODATA. */
    int ancount = ((int)answer[6] << 8) | answer[7];
    if (ancount == 0) { h_errno = NO_DATA; return -1; }
    return rlen;
}

int res_search(const char *dname, int rrclass, int type,
               unsigned char *answer, int anslen) {
    if ((_res.options & RES_INIT) == 0 && res_init() < 0) return -1;

    /* If the name contains a dot, try it as-is first. */
    int dots = 0;
    for (const char *p = dname; *p; p++) if (*p == '.') dots++;
    int try_first = (dots >= (int)_res.ndots);
    int rc;

    if (try_first) {
        rc = res_query(dname, rrclass, type, answer, anslen);
        if (rc > 0) return rc;
        if (h_errno != HOST_NOT_FOUND) return -1;
    }

    /* Try each search-list entry. */
    char buf[NS_MAXDNAME];
    for (int i = 0; _res.dnsrch[i] && i < MAXDNSRCH; i++) {
        size_t nl = strlen(dname), sl = strlen(_res.dnsrch[i]);
        if (nl + 1 + sl + 1 > sizeof(buf)) continue;
        memcpy(buf, dname, nl);
        buf[nl] = '.';
        memcpy(buf + nl + 1, _res.dnsrch[i], sl);
        buf[nl + 1 + sl] = '\0';
        rc = res_query(buf, rrclass, type, answer, anslen);
        if (rc > 0) return rc;
        if (h_errno != HOST_NOT_FOUND && h_errno != NO_DATA) return -1;
    }

    /* Finally, try as-is if we didn't above. */
    if (!try_first) {
        rc = res_query(dname, rrclass, type, answer, anslen);
        if (rc > 0) return rc;
    }
    return -1;
}
