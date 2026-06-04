/*
 * <arpa/nameser.h> — DNS message format (RFC 1035 + extensions).
 *
 * This is the historical BIND name-server API surface that libresolv
 * and downstream consumers (OpenSSH's getrrsetbyname, glibc dn_expand
 * users, …) reference.  Substrate's libresolv (lib/resolv/) is the
 * runtime that implements the functions declared in <resolv.h>; this
 * header provides only the on-the-wire format constants and the
 * opaque message-walking handle types.
 */

#ifndef _ARPA_NAMESER_H
#define _ARPA_NAMESER_H

#include <sys/types.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Size limits (RFC 1035 §2.3.4 / §4.1.4). */
#define NS_PACKETSZ      512   /* maximum UDP message size */
#define NS_MAXDNAME      1025  /* maximum domain name (ASCII) */
#define NS_MAXMSG        65535 /* maximum TCP message size */
#define NS_MAXCDNAME     255   /* maximum compressed wire-form name */
#define NS_MAXLABEL      63    /* maximum label length */
#define NS_HFIXEDSZ      12    /* header */
#define NS_QFIXEDSZ      4     /* per-question fixed bytes */
#define NS_RRFIXEDSZ     10    /* per-RR fixed bytes (excluding rdata) */
#define NS_INT32SZ       4
#define NS_INT16SZ       2
#define NS_INT8SZ        1
#define NS_INADDRSZ      4
#define NS_IN6ADDRSZ     16
#define NS_CMPRSFLGS     0xc0  /* compression-pointer top-bits mask */
#define NS_DEFAULTPORT   53    /* DNS server UDP port */

/* Historical BIND-4 names — many downstream callers (OpenSSH's
 * getrrsetbyname among them) use these instead of the NS_-prefixed
 * macros above.  Keep both in lockstep. */
#define PACKETSZ         NS_PACKETSZ
#define MAXDNAME         NS_MAXDNAME
#define HFIXEDSZ         NS_HFIXEDSZ
#define QFIXEDSZ         NS_QFIXEDSZ
#define RRFIXEDSZ        NS_RRFIXEDSZ
#define INT32SZ          NS_INT32SZ
#define INT16SZ          NS_INT16SZ
#define INADDRSZ         NS_INADDRSZ
#define IN6ADDRSZ        NS_IN6ADDRSZ
#define INDIR_MASK       NS_CMPRSFLGS

/* Resource Record type codes (subset that matters for substrate).
 * Full list at IANA "Domain Name System (DNS) Parameters". */
typedef enum __ns_type {
    ns_t_invalid = 0,
    ns_t_a       = 1,
    ns_t_ns      = 2,
    ns_t_md      = 3,
    ns_t_mf      = 4,
    ns_t_cname   = 5,
    ns_t_soa     = 6,
    ns_t_mb      = 7,
    ns_t_mg      = 8,
    ns_t_mr      = 9,
    ns_t_null    = 10,
    ns_t_wks     = 11,
    ns_t_ptr     = 12,
    ns_t_hinfo   = 13,
    ns_t_minfo   = 14,
    ns_t_mx      = 15,
    ns_t_txt     = 16,
    ns_t_rp      = 17,
    ns_t_afsdb   = 18,
    ns_t_aaaa    = 28,
    ns_t_loc     = 29,
    ns_t_srv     = 33,
    ns_t_naptr   = 35,
    ns_t_kx      = 36,
    ns_t_cert    = 37,
    ns_t_a6      = 38,
    ns_t_dname   = 39,
    ns_t_opt     = 41,
    ns_t_ds      = 43,
    ns_t_sshfp   = 44,
    ns_t_ipseckey= 45,
    ns_t_rrsig   = 46,
    ns_t_nsec    = 47,
    ns_t_dnskey  = 48,
    ns_t_dhcid   = 49,
    ns_t_nsec3   = 50,
    ns_t_axfr    = 252,
    ns_t_mailb   = 253,
    ns_t_maila   = 254,
    ns_t_any     = 255,
} ns_type;

/* BIND-4 aliases. */
#define T_A          ns_t_a
#define T_NS         ns_t_ns
#define T_CNAME      ns_t_cname
#define T_SOA        ns_t_soa
#define T_PTR        ns_t_ptr
#define T_HINFO      ns_t_hinfo
#define T_MX         ns_t_mx
#define T_TXT        ns_t_txt
#define T_AAAA       ns_t_aaaa
#define T_SRV        ns_t_srv
#define T_NAPTR      ns_t_naptr
#define T_OPT        ns_t_opt
#define T_DS         ns_t_ds
#define T_SSHFP      ns_t_sshfp
#define T_RRSIG      ns_t_rrsig
#define T_NSEC       ns_t_nsec
#define T_DNSKEY     ns_t_dnskey
#define T_AXFR       ns_t_axfr
#define T_ANY        ns_t_any

/* Class codes. */
typedef enum __ns_class {
    ns_c_invalid = 0,
    ns_c_in      = 1,
    ns_c_chaos   = 3,
    ns_c_hs      = 4,
    ns_c_none    = 254,
    ns_c_any     = 255,
} ns_class;

#define C_IN         ns_c_in
#define C_CHAOS      ns_c_chaos
#define C_HS         ns_c_hs
#define C_ANY        ns_c_any

/* Section identifiers used by ns_initparse / ns_parserr. */
typedef enum __ns_sect {
    ns_s_qd = 0,   /* question */
    ns_s_an = 1,   /* answer */
    ns_s_ns = 2,   /* authority */
    ns_s_ar = 3,   /* additional */
    ns_s_max = 4,
} ns_sect;

/* Opcode + response codes (in the HEADER flag bytes). */
typedef enum __ns_opcode {
    ns_o_query  = 0,
    ns_o_iquery = 1,
    ns_o_status = 2,
    ns_o_notify = 4,
    ns_o_update = 5,
} ns_opcode;

typedef enum __ns_rcode {
    ns_r_noerror = 0,
    ns_r_formerr = 1,
    ns_r_servfail = 2,
    ns_r_nxdomain = 3,
    ns_r_notimpl = 4,
    ns_r_refused = 5,
} ns_rcode;

/* HEADER flag positions — used by ns_msg_getflag(). */
typedef enum __ns_flag {
    ns_f_qr,
    ns_f_opcode,
    ns_f_aa,
    ns_f_tc,
    ns_f_rd,
    ns_f_ra,
    ns_f_z,
    ns_f_ad,
    ns_f_cd,
    ns_f_rcode,
    ns_f_max,
} ns_flag;

/*
 * HEADER — the on-the-wire DNS message header.  Bit-field layout is
 * the BIND-historical one; portable consumers should use the
 * ns_msg_* accessors rather than poking the bits directly.
 */
typedef struct {
    unsigned    id      : 16;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    unsigned    qr      : 1;
    unsigned    opcode  : 4;
    unsigned    aa      : 1;
    unsigned    tc      : 1;
    unsigned    rd      : 1;
    unsigned    ra      : 1;
    unsigned    unused  : 1;
    unsigned    ad      : 1;
    unsigned    cd      : 1;
    unsigned    rcode   : 4;
#else
    unsigned    rd      : 1;
    unsigned    tc      : 1;
    unsigned    aa      : 1;
    unsigned    opcode  : 4;
    unsigned    qr      : 1;
    unsigned    rcode   : 4;
    unsigned    cd      : 1;
    unsigned    ad      : 1;
    unsigned    unused  : 1;
    unsigned    ra      : 1;
#endif
    unsigned    qdcount : 16;
    unsigned    ancount : 16;
    unsigned    nscount : 16;
    unsigned    arcount : 16;
} HEADER;

/*
 * ns_msg — opaque handle to a parsed DNS message.  Filled by
 * ns_initparse(); subsequent calls walk it via ns_parserr() etc.
 */
typedef struct __ns_msg {
    const unsigned char *_msg;
    const unsigned char *_eom;
    uint16_t             _id;
    uint16_t             _flags;
    uint16_t             _counts[ns_s_max];
    const unsigned char *_sections[ns_s_max];
    ns_sect              _sect;
    int                  _rrnum;
    const unsigned char *_msg_ptr;
} ns_msg;

typedef struct __ns_rr {
    char                  name[NS_MAXDNAME];
    uint16_t              type;
    uint16_t              rr_class;
    uint32_t              ttl;
    uint16_t              rdlength;
    const unsigned char  *rdata;
} ns_rr;

/* Accessor macros.  These mirror BIND's published names so that
 * downstream code (libbind callers, OpenSSH's openbsd-compat) just
 * works without #ifdef churn. */
#define ns_msg_id(handle)             ((handle)._id + 0)
#define ns_msg_size(handle)           ((unsigned)((handle)._eom - (handle)._msg))
#define ns_msg_count(handle, section) ((handle)._counts[section] + 0)
#define ns_msg_base(handle)           ((handle)._msg + 0)
#define ns_msg_end(handle)            ((handle)._eom + 0)
#define ns_rr_name(rr)                ((rr).name)
#define ns_rr_type(rr)                ((ns_type)(rr).type)
#define ns_rr_class(rr)               ((ns_class)(rr).rr_class)
#define ns_rr_ttl(rr)                 ((rr).ttl)
#define ns_rr_rdlen(rr)               ((rr).rdlength)
#define ns_rr_rdata(rr)               ((rr).rdata)

/* Wire-form integer accessors (network → host byte order, no
 * alignment assumption). */
#define NS_GET16(s, cp)  do { \
    const unsigned char *t_cp = (const unsigned char *)(cp); \
    (s) = ((uint16_t)t_cp[0] << 8) | (uint16_t)t_cp[1]; \
    (cp) += 2; \
} while (0)
#define NS_GET32(l, cp)  do { \
    const unsigned char *t_cp = (const unsigned char *)(cp); \
    (l) = ((uint32_t)t_cp[0] << 24) | ((uint32_t)t_cp[1] << 16) \
        | ((uint32_t)t_cp[2] << 8)  | (uint32_t)t_cp[3]; \
    (cp) += 4; \
} while (0)
#define NS_PUT16(s, cp)  do { \
    uint16_t t_s = (uint16_t)(s); \
    unsigned char *t_cp = (unsigned char *)(cp); \
    t_cp[0] = (unsigned char)(t_s >> 8); \
    t_cp[1] = (unsigned char)t_s; \
    (cp) += 2; \
} while (0)
#define NS_PUT32(l, cp)  do { \
    uint32_t t_l = (uint32_t)(l); \
    unsigned char *t_cp = (unsigned char *)(cp); \
    t_cp[0] = (unsigned char)(t_l >> 24); \
    t_cp[1] = (unsigned char)(t_l >> 16); \
    t_cp[2] = (unsigned char)(t_l >> 8); \
    t_cp[3] = (unsigned char)t_l; \
    (cp) += 4; \
} while (0)

/* Legacy BIND names for the same accessors (GLib's resolver, libbind code). */
#define GETSHORT(s, cp)  NS_GET16(s, cp)
#define GETLONG(l, cp)   NS_GET32(l, cp)
#define PUTSHORT(s, cp)  NS_PUT16(s, cp)
#define PUTLONG(l, cp)   NS_PUT32(l, cp)

/* Message-walking API.  All implemented in lib/resolv/ns_parse.c. */
int  ns_initparse(const unsigned char *msg, int msglen, ns_msg *handle);
int  ns_parserr(ns_msg *handle, ns_sect section, int rrnum, ns_rr *rr);
int  ns_skiprr(const unsigned char *ptr, const unsigned char *eom,
               ns_sect section, int count);
int  ns_name_uncompress(const unsigned char *msg, const unsigned char *eom,
                        const unsigned char *src, char *dst, size_t dstsiz);
int  ns_name_unpack(const unsigned char *msg, const unsigned char *eom,
                    const unsigned char *src, unsigned char *dst, size_t dstsiz);

#ifdef __cplusplus
}
#endif

#endif /* _ARPA_NAMESER_H */
