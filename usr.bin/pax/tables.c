/*
 * Copyright (c) 2024 Substrate OS
 * All rights reserved.
 */

#include "pax.h"

extern int ustar_id(char *, int);
extern int ustar_rd(ARCH_HDR *, char *);
extern int ustar_wr(ARCH_HDR *);

extern int cpio_id(char *, int);
extern int cpio_rd(ARCH_HDR *, char *);
extern int cpio_wr(ARCH_HDR *);

FSUB fsub[] = {
    { "ustar", ustar_id, NULL, ustar_rd, NULL, ustar_wr, NULL, 0 },
    { "cpio", cpio_id, NULL, cpio_rd, NULL, cpio_wr, NULL, 0 },
    { NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0 }
};
