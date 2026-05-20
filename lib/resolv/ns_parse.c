/*
 * ns_parse.c — message-walking API.
 *
 * ns_initparse takes a raw DNS response and populates a handle with
 * section offsets; ns_parserr returns the i-th RR of a section in
 * decoded form.  Both follow BIND-historical semantics.
 */

#include <arpa/nameser.h>
#include <resolv.h>
#include <string.h>

static const unsigned char *skip_question(const unsigned char *p,
                                          const unsigned char *eom) {
    int n = dn_skipname(p, eom);
    if (n < 0) return NULL;
    p += n;
    if (p + 4 > eom) return NULL;
    p += 4;   /* qtype + qclass */
    return p;
}

static const unsigned char *skip_rr(const unsigned char *p,
                                    const unsigned char *eom) {
    int n = dn_skipname(p, eom);
    if (n < 0) return NULL;
    p += n;
    if (p + 10 > eom) return NULL;
    unsigned rdlen = ((unsigned)p[8] << 8) | p[9];
    p += 10 + rdlen;
    if (p > eom) return NULL;
    return p;
}

int ns_initparse(const unsigned char *msg, int msglen, ns_msg *handle) {
    if (!msg || !handle || msglen < (int)NS_HFIXEDSZ) return -1;
    memset(handle, 0, sizeof(*handle));

    handle->_msg = msg;
    handle->_eom = msg + msglen;
    handle->_id    = ((uint16_t)msg[0] << 8) | msg[1];
    handle->_flags = ((uint16_t)msg[2] << 8) | msg[3];
    handle->_counts[ns_s_qd] = ((uint16_t)msg[4] << 8) | msg[5];
    handle->_counts[ns_s_an] = ((uint16_t)msg[6] << 8) | msg[7];
    handle->_counts[ns_s_ns] = ((uint16_t)msg[8] << 8) | msg[9];
    handle->_counts[ns_s_ar] = ((uint16_t)msg[10] << 8) | msg[11];

    const unsigned char *p = msg + NS_HFIXEDSZ;
    handle->_sections[ns_s_qd] = p;
    for (int i = 0; i < handle->_counts[ns_s_qd]; i++) {
        p = skip_question(p, handle->_eom);
        if (!p) return -1;
    }

    handle->_sections[ns_s_an] = p;
    for (int i = 0; i < handle->_counts[ns_s_an]; i++) {
        p = skip_rr(p, handle->_eom);
        if (!p) return -1;
    }

    handle->_sections[ns_s_ns] = p;
    for (int i = 0; i < handle->_counts[ns_s_ns]; i++) {
        p = skip_rr(p, handle->_eom);
        if (!p) return -1;
    }

    handle->_sections[ns_s_ar] = p;
    for (int i = 0; i < handle->_counts[ns_s_ar]; i++) {
        p = skip_rr(p, handle->_eom);
        if (!p) return -1;
    }

    handle->_sect = ns_s_max;
    handle->_rrnum = -1;
    handle->_msg_ptr = NULL;
    return 0;
}

int ns_parserr(ns_msg *handle, ns_sect section, int rrnum, ns_rr *rr) {
    if (!handle || !rr || section >= ns_s_max) return -1;
    if (rrnum < 0 || rrnum >= handle->_counts[section]) return -1;

    const unsigned char *p = handle->_sections[section];
    const unsigned char *eom = handle->_eom;

    /* For questions, walk rrnum question records. */
    if (section == ns_s_qd) {
        for (int i = 0; i < rrnum; i++) {
            p = skip_question(p, eom);
            if (!p) return -1;
        }
        int n = dn_expand(handle->_msg, eom, p, rr->name, sizeof(rr->name));
        if (n < 0) return -1;
        p += n;
        if (p + 4 > eom) return -1;
        rr->type     = ((uint16_t)p[0] << 8) | p[1]; p += 2;
        rr->rr_class = ((uint16_t)p[0] << 8) | p[1]; p += 2;
        rr->ttl      = 0;
        rr->rdlength = 0;
        rr->rdata    = NULL;
        return 0;
    }

    /* Answer / authority / additional: walk rrnum full RRs. */
    for (int i = 0; i < rrnum; i++) {
        p = skip_rr(p, eom);
        if (!p) return -1;
    }
    int n = dn_expand(handle->_msg, eom, p, rr->name, sizeof(rr->name));
    if (n < 0) return -1;
    p += n;
    if (p + 10 > eom) return -1;
    rr->type     = ((uint16_t)p[0] << 8) | p[1]; p += 2;
    rr->rr_class = ((uint16_t)p[0] << 8) | p[1]; p += 2;
    rr->ttl      = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                   ((uint32_t)p[2] << 8) | p[3];               p += 4;
    rr->rdlength = ((uint16_t)p[0] << 8) | p[1];               p += 2;
    if (p + rr->rdlength > eom) return -1;
    rr->rdata = p;
    return 0;
}

int ns_skiprr(const unsigned char *ptr, const unsigned char *eom,
              ns_sect section, int count) {
    const unsigned char *p = ptr;
    for (int i = 0; i < count; i++) {
        if (section == ns_s_qd) p = skip_question(p, eom);
        else                    p = skip_rr(p, eom);
        if (!p) return -1;
    }
    return (int)(p - ptr);
}
