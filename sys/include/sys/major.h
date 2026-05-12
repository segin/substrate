/*
 * sys/major.h — canonical major numbers for substrate character / block
 * device nodes.  Layout follows Linux conventions closely so that
 * stat(2) on a tty returns numbers that match what userland tools
 * expect to compare against.  Add a new entry here rather than picking
 * a free number ad-hoc in a driver.
 *
 * The full dev_t encoding is in <sys/sysmacros.h>:
 *   rdev = (major << 8) | (minor & 0xff)
 */
#ifndef _SUBSTRATE_SYS_MAJOR_H
#define _SUBSTRATE_SYS_MAJOR_H

/*
 * Kernel-side dev_t encode / decode.  Userspace gets the same macros
 * from <sys/sysmacros.h>; we duplicate them here so kernel sources
 * don't have to pull in a userspace header.  Keep both in sync.
 */
#define makedev(maj, min) ((unsigned)((((maj) & 0xFF) << 8) | ((min) & 0xFF)))
#define major(dev)        ((unsigned)(((dev) >> 8) & 0xFF))
#define minor(dev)        ((unsigned)((dev) & 0xFF))

#define MEM_MAJOR          1   /* /dev/mem, null, zero, full, ... */
#define TTY_MAJOR          4   /* /dev/tty1..ttyN (minor 1..63),
                                  /dev/ttyS0..ttySn (minor 64..) */
#define TTYAUX_MAJOR       5   /* /dev/tty (minor 0),
                                  /dev/console (minor 1),
                                  /dev/ptmx (minor 2) */
#define LP_MAJOR           6   /* /dev/lp0..lpN — parallel/line printer */
#define SCSI_DISK_MAJOR    8   /* /dev/sd* */
#define SOUND_MAJOR        14  /* /dev/audio, /dev/audioctl, /dev/mixer */
#define BLOCK_LOOP_MAJOR   7   /* /dev/loop0..loopN */
#define UNIX98_PTS_MAJOR   136 /* /dev/pts/N */

/* Minor sub-allocations within TTY_MAJOR (4). */
#define TTY_MINOR_VC_FIRST  1   /* /dev/tty1 */
#define TTY_MINOR_VC_LAST   63
#define TTY_MINOR_SERIAL    64  /* /dev/ttyS0..S(255-64) */

/* Minor allocations within TTYAUX_MAJOR (5). */
#define TTYAUX_MINOR_TTY      0  /* /dev/tty (controlling-tty alias) */
#define TTYAUX_MINOR_CONSOLE  1  /* /dev/console */
#define TTYAUX_MINOR_PTMX     2  /* /dev/ptmx */

/* Minor sub-allocations within MEM_MAJOR (1). */
#define MEM_MINOR_MEM        1
#define MEM_MINOR_KMEM       2
#define MEM_MINOR_NULL       3
#define MEM_MINOR_PORT       4
#define MEM_MINOR_ZERO       5
#define MEM_MINOR_FULL       7
#define MEM_MINOR_RANDOM     8
#define MEM_MINOR_URANDOM    9

#endif /* _SUBSTRATE_SYS_MAJOR_H */
