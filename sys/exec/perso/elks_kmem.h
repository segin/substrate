#ifndef _EXEC_PERSO_ELKS_KMEM_H
#define _EXEC_PERSO_ELKS_KMEM_H

/*
 * ELKS /dev/kmem ioctl numbers used by upstream ELKS userland.
 * These values are part of the ELKS userspace ABI contract.
 */
#define ELKS_MEM_GETTEXTSIZ   2
#define ELKS_MEM_GETUSAGE     3
#define ELKS_MEM_GETTASK      4
#define ELKS_MEM_GETDS        5
#define ELKS_MEM_GETCS        6
#define ELKS_MEM_GETHEAP      7
#define ELKS_MEM_GETUPTIME    8
#define ELKS_MEM_GETFARTEXT   9
#define ELKS_MEM_GETMAXTASKS  10
#define ELKS_MEM_GETJIFFADDR  11
#define ELKS_MEM_GETSEGALL    12

#endif
