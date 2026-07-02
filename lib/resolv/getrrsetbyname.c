/*
 * getrrsetbyname.c — OpenBSD-historical helper that wraps res_query
 * and returns a parsed RR set + matching RRSIG signatures.
 *
 * Used by OpenSSH's `VerifyHostKeyDNS` path to fetch SSHFP records
 * and (if signed) validate them.  Substrate's resolver does not
 * validate DNSSEC; we return RRSIGs as opaque rdata so a downstream
 * verifier could in principle do it.  The RRSET_VALIDATED flag in
 * the response is never set.
 */

#include <arpa/nameser.h>
#include <errno.h>
#include <resolv.h>
#include <stdlib.h>
#include <string.h>

void freerrset(struct rrsetinfo *rrset) {
    if (!rrset) return;
    if (rrset->rri_rdatas) {
        for (unsigned int i = 0; i < rrset->rri_nrdatas; i++)
            free(rrset->rri_rdatas[i].rdi_data);
        free(rrset->rri_rdatas);
    }
    if (rrset->rri_sigs) {
        for (unsigned int i = 0; i < rrset->rri_nsigs; i++)
            free(rrset->rri_sigs[i].rdi_data);
        free(rrset->rri_sigs);
    }
    free(rrset->rri_name);
    free(rrset);
}

static int copy_rdata(struct rdatainfo *dst, const unsigned char *src, size_t len) {
    dst->rdi_data = malloc(len);
    if (!dst->rdi_data) return -1;
    memcpy(dst->rdi_data, src, len);
    dst->rdi_length = (unsigned)len;
    return 0;
}

int getrrsetbyname(const char *hostname, unsigned int rdclass,
                   unsigned int rdtype, unsigned int flags,
                   struct rrsetinfo **resp) {
    if (!hostname || !resp) return ERRSET_INVAL;
    *resp = NULL;
    (void)flags;

    unsigned char buf[NS_MAXMSG];
    int len = res_query(hostname, (int)rdclass, (int)rdtype, buf, sizeof(buf));
    if (len < 0) {
        switch (h_errno) {
        case HOST_NOT_FOUND: return ERRSET_NONAME;
        case NO_DATA:        return ERRSET_NODATA;
        case TRY_AGAIN:      return ERRSET_FAIL;
        default:             return ERRSET_FAIL;
        }
    }

    ns_msg msg;
    if (ns_initparse(buf, len, &msg) < 0) return ERRSET_FAIL;
    int ancount = ns_msg_count(msg, ns_s_an);
    if (ancount <= 0) return ERRSET_NODATA;

    /* Two passes: count matching records and signatures, then alloc
     * and copy.  Substrate prefers two clean passes over a
     * realloc-as-you-go loop. */
    unsigned int nrdatas = 0, nsigs = 0;
    char rrname[NS_MAXDNAME] = {0};
    unsigned int ttl = 0;
    for (int i = 0; i < ancount; i++) {
        ns_rr rr;
        if (ns_parserr(&msg, ns_s_an, i, &rr) < 0) return ERRSET_FAIL;
        if (rr.rr_class != rdclass) continue;
        if (rr.type == rdtype) {
            nrdatas++;
            if (rrname[0] == '\0') {
                strlcpy(rrname, rr.name, sizeof(rrname));
                ttl = rr.ttl;
            }
        } else if (rr.type == ns_t_rrsig && rr.rdlength >= 18) {
            uint16_t covered = ((uint16_t)rr.rdata[0] << 8) | rr.rdata[1];
            if (covered == rdtype) nsigs++;
        }
    }
    if (nrdatas == 0) return ERRSET_NODATA;

    struct rrsetinfo *r = calloc(1, sizeof(*r));
    if (!r) return ERRSET_NOMEMORY;
    r->rri_rdclass = rdclass;
    r->rri_rdtype  = rdtype;
    r->rri_ttl     = ttl;
    r->rri_name    = strdup(rrname);
    if (!r->rri_name) { freerrset(r); return ERRSET_NOMEMORY; }
    r->rri_rdatas = calloc(nrdatas, sizeof(struct rdatainfo));
    if (!r->rri_rdatas) { freerrset(r); return ERRSET_NOMEMORY; }
    if (nsigs > 0) {
        r->rri_sigs = calloc(nsigs, sizeof(struct rdatainfo));
        if (!r->rri_sigs) { freerrset(r); return ERRSET_NOMEMORY; }
    }

    unsigned int rd_i = 0, sig_i = 0;
    for (int i = 0; i < ancount; i++) {
        ns_rr rr;
        if (ns_parserr(&msg, ns_s_an, i, &rr) < 0) { freerrset(r); return ERRSET_FAIL; }
        if (rr.rr_class != rdclass) continue;
        if (rr.type == rdtype) {
            if (copy_rdata(&r->rri_rdatas[rd_i], rr.rdata, rr.rdlength) < 0) {
                freerrset(r); return ERRSET_NOMEMORY;
            }
            rd_i++;
        } else if (rr.type == ns_t_rrsig && rr.rdlength >= 18) {
            uint16_t covered = ((uint16_t)rr.rdata[0] << 8) | rr.rdata[1];
            if (covered == rdtype) {
                if (copy_rdata(&r->rri_sigs[sig_i], rr.rdata, rr.rdlength) < 0) {
                    freerrset(r); return ERRSET_NOMEMORY;
                }
                sig_i++;
            }
        }
    }
    r->rri_nrdatas = rd_i;
    r->rri_nsigs   = sig_i;
    *resp = r;
    return ERRSET_SUCCESS;
}
