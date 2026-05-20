/*
 * arpa/ftp.h — FTP protocol constants (RFC 959 + RFC 2228).
 *
 * Mirrors the BSD historical header that inetutils' ftp/ftpd
 * source expects.  No code, just the FTP reply-class macros, the
 * representation-type constants, and a couple of mode/structure
 * letters that the parser tests against.
 */

#ifndef _ARPA_FTP_H
#define _ARPA_FTP_H

/* Reply categories — first digit of every FTP response. */
#define PRELIM          1   /* positive preliminary */
#define COMPLETE        2   /* positive completion */
#define CONTINUE        3   /* positive intermediate */
#define TRANSIENT       4   /* transient negative */
#define ERROR           5   /* permanent negative */

/* Type codes for the TYPE command. */
#define TYPE_A          1   /* ASCII */
#define TYPE_E          2   /* EBCDIC */
#define TYPE_I          3   /* image (binary) */
#define TYPE_L          4   /* local-byte (size set by argument) */

/* Format codes for ASCII / EBCDIC types. */
#define FORM_N          1   /* non-print */
#define FORM_T          2   /* telnet format effectors */
#define FORM_C          3   /* carriage control (ASA) */

/* Structure codes for the STRU command. */
#define STRU_F          1   /* file (no record structure) */
#define STRU_R          2   /* record structure */
#define STRU_P          3   /* page structure */

/* Mode codes for the MODE command. */
#define MODE_S          1   /* stream */
#define MODE_B          2   /* block */
#define MODE_C          3   /* compressed */

/* Authentication / data protection levels (RFC 2228). */
#define PROT_C          1   /* clear */
#define PROT_S          2   /* safe */
#define PROT_P          3   /* private */
#define PROT_E          4   /* confidential */

/* Default ports (legacy compile-time fallbacks; getservbyname is
 * the runtime source of truth). */
#define FTP_PORT        21
#define FTP_DATA_PORT   20

/* Pretty names used by ftp(1) parser diagnostics. */
extern const char *typenames[];   /* "A", "E", "I", "L" */
extern const char *formnames[];   /* "N", "T", "C"      */
extern const char *strunames[];   /* "F", "R", "P"      */
extern const char *modenames[];   /* "S", "B", "C"      */

#endif /* _ARPA_FTP_H */
