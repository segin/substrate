#ifndef _SYS_9P_H
#define _SYS_9P_H

#include <stdint.h>

#define P9_TVERSION 100
#define P9_RVERSION 101
#define P9_TAUTH    102
#define P9_RAUTH    103
#define P9_TATTACH  104
#define P9_RATTACH  105
#define P9_TERROR   106
#define P9_RERROR   107
#define P9_TFLUSH   108
#define P9_RFLUSH   109
#define P9_TWALK    110
#define P9_RWALK    111
#define P9_TOPEN    112
#define P9_ROPEN    113
#define P9_TCREATE  114
#define P9_RCREATE  115
#define P9_TREAD    116
#define P9_RREAD    117
#define P9_TWRITE   118
#define P9_RWRITE   119
#define P9_TCLUNK   120
#define P9_RCLUNK   121
#define P9_TREMOVE  122
#define P9_RREMOVE  123
#define P9_TSTAT    124
#define P9_RSTAT    125
#define P9_TWSTAT   126
#define P9_RWSTAT   127

struct p9_header {
    uint32_t size;
    uint8_t  type;
    uint16_t tag;
} __attribute__((packed));

#endif
