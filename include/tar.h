/*
 * tar.h - POSIX 1003.1 tar definitions
 */

#ifndef _TAR_H
#define _TAR_H

/* General definitions */
#define TMAGIC   "ustar"        /* ustar plus null byte */
#define TMAGLEN  6              /* Length of the above */
#define TVERSION "00"           /* 00 without a null byte */
#define TVERSLEN 2              /* Length of the above */

/* Typeflag field definitions */
#define REGTYPE  '0'            /* Regular file */
#define AREGTYPE '\0'           /* Regular file */
#define LNKTYPE  '1'            /* Link */
#define SYMTYPE  '2'            /* Symbolic link */
#define CHRTYPE  '3'            /* Character special */
#define BLKTYPE  '4'            /* Block special */
#define DIRTYPE  '5'            /* Directory */
#define FIFOTYPE '6'            /* FIFO special */
#define CONTTYPE '7'            /* Reserved */

/* Mode field bit definitions (Octal) */
#define TSUID    04000          /* Set UID on execution */
#define TSGID    02000          /* Set GID on execution */
#define TSVTX    01000          /* Reserved (Sticky bit) */
#define TUREAD   00400          /* Read by owner */
#define TUWRITE  00200          /* Write by owner */
#define TUEXEC   00100          /* Execute/Search by owner */
#define TGREAD   00040          /* Read by group */
#define TGWRITE  00020          /* Write by group */
#define TGEXEC   00010          /* Execute/Search by group */
#define TOREAD   00004          /* Read by other */
#define TOWRITE  00002          /* Write by other */
#define TOEXEC   00001          /* Execute/Search by other */

#endif /* _TAR_H */
