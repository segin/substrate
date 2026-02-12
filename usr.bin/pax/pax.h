/*
 * Copyright (c) 2024 Substrate OS
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _PAX_H_
#define _PAX_H_

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

/*
 * PAX formats
 */
#define PAX_USTAR   1
#define PAX_CPIO    2
#define PAX_PAX     3

/*
 * General Definitions
 */
#ifndef MAXPATHLEN
#define MAXPATHLEN  1024
#endif

#define BLKMULT     512     /* block size multiple */
#define DFLT_BLKSIZE 10240  /* default block size */
#define MAXBLK      32768   /* max block size */

/*
 * Archive format types
 */
typedef enum {
    ARCH_UNKNOWN = 0,
    ARCH_USTAR,
    ARCH_PAX,
    ARCH_CPIO,
    ARCH_CPIO_SVR4, /* newc */
    ARCH_TAR
} ARCHD;

/*
 * Operation modes
 */
typedef enum {
    OP_LIST,
    OP_READ,
    OP_WRITE,
    OP_COPY,
    OP_APPEND
} OPMD;

/*
 * Global variables
 */
extern int      act;        /* operation mode */
extern int      vflag;      /* verbose */
extern int      dflag;      /* directory list mode */
extern int      cflag;      /* complement match */
extern int      iflag;      /* interactive rename */
extern int      kflag;      /* prevent overwrite */
extern int      lflag;      /* link files */
extern int      nflag;      /* select first match */
extern int      oflag;      /* open tty */
extern int      pflag;      /* preserve attributes */
extern int      sflag;      /* substitution */
extern int      tflag;      /* reset access time */
extern int      uflag;      /* ignore older */
extern int      xflag;      /* read/write format */
extern int      zflag;      /* gzip */

extern char     *dirptr;    /* destination directory */
extern char     *ltemp;     /* temp name */
extern char     *artype;    /* archive type string */
extern char     *arfile;    /* archive file name */

/*
 * Structure for generic archive header
 */
typedef struct arch_header {
    char    *name;
    char    *ln_name;   /* link name */
    char    *org_name;  /* original name */
    mode_t  mode;
    uid_t   uid;
    gid_t   gid;
    off_t   size;
    time_t  mtime;
    time_t  atime;
    time_t  ctime;
    dev_t   dev;
    nlink_t nlink;
    int     type;       /* file type */
    long    pad;
} ARCH_HDR;

/*
 * Header structures
 */
typedef struct hd_ustar {
    char    name[100];
    char    mode[8];
    char    uid[8];
    char    gid[8];
    char    size[12];
    char    mtime[12];
    char    chksum[8];
    char    typeflag;
    char    linkname[100];
    char    magic[6];
    char    version[2];
    char    uname[32];
    char    gname[32];
    char    devmajor[8];
    char    devminor[8];
    char    prefix[155];
    char    pad[12];
} HD_USTAR;

typedef struct hd_cpio {
    char    c_magic[6];
    char    c_dev[6];
    char    c_ino[6];
    char    c_mode[6];
    char    c_uid[6];
    char    c_gid[6];
    char    c_nlink[6];
    char    c_rdev[6];
    char    c_mtime[11];
    char    c_namesize[6];
    char    c_filesize[11];
} HD_CPIO;

/*
 * Format Handler Structure
 */
typedef struct {
    char    *name;
    int     (*id)(char *, int);
    int     (*st_rd)(void);
    int     (*rd)(ARCH_HDR *, char *);
    int     (*st_wr)(void);
    int     (*wr)(ARCH_HDR *);
    int     (*trail)(char *, int, int *);
    int     is_stream;
} FSUB;

/*
 * Getopt definitions
 */
extern char *optarg;
extern int optind;
extern int optopt;
extern int opterr;
extern int optreset;
int getopt(int nargc, char * const nargv[], const char *ostr);

/*
 * Function Prototypes
 */
void    ar_read(void);
void    ar_write(int argc, char **argv);
void    ar_append(int argc, char **argv);
void    copy_file(int argc, char **argv);
void    list_archive(void);

int     pax_read(void);
int     pax_write(void);

void    options(int argc, char **argv);
void    usage(void);

/* buffer handling */
int     wr_skip(off_t skip);
int     rd_skip(off_t skip);
void    cp_start(void);
int     ar_next(void);
int     ar_open(const char *name);
void    ar_close(void);
void    ar_drain(void);
int     ar_fow(off_t sksz, off_t *skipped);

/* ftree.c */
int     ftree_start(int argc, char **argv);
int     ftree_next(ARCH_HDR *arcn);

/* file_subs.c */
int     file_creat(ARCH_HDR *arcn);
void    file_close(ARCH_HDR *arcn, int fd);

/* pat_rep.c */
int     rep_add(char *str);
int     rep_name(char *name, int *nlen, int *prnt);
int     pat_add(char *str, char *chd);
int     pat_match(ARCH_HDR *arcn);

#endif /* _PAX_H_ */
