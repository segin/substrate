/*
 * <arpa/tftp.h> — Trivial File Transfer Protocol packet definitions.
 *
 * From RFC 1350 / BSD <arpa/tftp.h>.  Substrate doesn't run tftp
 * itself; this header is here so packages that build a tftp helper
 * subroutine in their common library (inetutils' libinetutils, etc.)
 * compile against substrate even when tftp itself is disabled.
 */
#ifndef _ARPA_TFTP_H
#define _ARPA_TFTP_H

#define SEGSIZE         512     /* data segment size */

/* Packet types.  */
#define RRQ             01      /* read request */
#define WRQ             02      /* write request */
#define DATA            03      /* data packet */
#define ACK             04      /* acknowledgement */
#define ERROR           05      /* error code */
#define OACK            06      /* RFC 2347 option acknowledgement */

struct tftphdr {
    short    th_opcode;         /* packet type */
    union {
        unsigned short tu_block;        /* block # */
        short          tu_code;         /* error code */
        char           tu_stuff[1];     /* request packet stuff */
    } th_u;
    char     th_data[1];        /* data or error string */
};

#define th_block        th_u.tu_block
#define th_code         th_u.tu_code
#define th_stuff        th_u.tu_stuff
#define th_msg          th_data

/* Error codes (RFC 1350 §5).  */
#define EUNDEF          0       /* not defined */
#define ENOTFOUND       1       /* file not found */
#define EACCESS         2       /* access violation */
#define ENOSPACE        3       /* disk full or allocation exceeded */
#define EBADOP          4       /* illegal TFTP operation */
#define EBADID          5       /* unknown transfer ID */
#define EEXISTS         6       /* file already exists */
#define ENOUSER         7       /* no such user */
#define EOPTNEG         8       /* RFC 2347 option negotiation failed */

#endif /* _ARPA_TFTP_H */
