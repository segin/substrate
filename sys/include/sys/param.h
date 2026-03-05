#ifndef _SYS_PARAM_H
#define _SYS_PARAM_H

#ifndef HZ
#define HZ 128
#endif

#define USEC_PER_TICK (1000000 / HZ)

/* Memory layout (Temporary defaults, should be arch-specific) */
#define KERN_BASE       0xC0000000
#define USER_STACK_MIN  0x00001000  /* Minimum valid user stack address */

#endif
