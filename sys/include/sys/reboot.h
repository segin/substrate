#ifndef _SYS_REBOOT_H
#define _SYS_REBOOT_H

/*
 * Commands for the reboot system call.
 */

#define RB_AUTOBOOT     0x01234567  /* Reboot immediately */
#define RB_HALT_SYSTEM  0xCEDEADBE  /* Halt the system */
#define RB_POWER_OFF    0x4321FEDC  /* Power off the system */

#endif /* _SYS_REBOOT_H */
