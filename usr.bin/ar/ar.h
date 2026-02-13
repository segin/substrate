#ifndef _AR_H
#define _AR_H

#include <stdint.h>

#define ARMAG   "!<arch>\n"
#define SARMAG  8

#define ARFMAG  "`\n"

struct ar_hdr {
    char ar_name[16];
    char ar_date[12];
    char ar_uid[6];
    char ar_gid[6];
    char ar_mode[8];
    char ar_size[10];
    char ar_fmag[2];
};

#define RANLIBMAG   "__.SYMDEF"
#define RANLIBSORT  "__.SYMDEF SORTED"

struct ranlib {
    union {
        uint32_t ran_strx;
        char    *ran_name;
    } ran_un;
    uint32_t ran_off;
};

#endif /* _AR_H */
