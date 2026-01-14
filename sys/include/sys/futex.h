#ifndef _SYS_FUTEX_H
#define _SYS_FUTEX_H

/* Operation codes */
#define FUTEX_WAIT          0
#define FUTEX_WAKE          1
#define FUTEX_FD            2
#define FUTEX_REQUEUE       3
#define FUTEX_CMP_REQUEUE   4

/* Flags (Bit 7 and higher) */
#define FUTEX_PRIVATE_FLAG  128
#define FUTEX_CLOCK_REALTIME 256
#define FUTEX_CMD_MASK      ~(FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME)

#define FUTEX_WAIT_PRIVATE  (FUTEX_WAIT | FUTEX_PRIVATE_FLAG)
#define FUTEX_WAKE_PRIVATE  (FUTEX_WAKE | FUTEX_PRIVATE_FLAG)

#endif
