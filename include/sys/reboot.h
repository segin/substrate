/*
 * <sys/reboot.h> — privileged shutdown / reboot interface.
 *
 * reboot(2) is the kernel's terminate-the-system entry point.  Only a
 * process with euid==0 may call it; the kernel returns -EPERM
 * otherwise.  The argument selects one of three actions.
 *
 * Substrate's kernel sees a single sys_reboot() that today performs a
 * keyboard-controller-driven reset regardless of action; the three
 * magic values are reserved so that when proper power management
 * (ACPI, APM) lands the userspace contract doesn't change.
 *
 * Typical use:
 *
 *     #include <sys/reboot.h>
 *     reboot(RB_POWER_OFF);
 *
 * /sbin/init terminates its shutdown_sequence with a reboot() call;
 * /sbin/{halt,reboot,poweroff} are thin wrappers that go straight
 * to reboot() for the emergency / scripted case.
 */

#ifndef _SYS_REBOOT_H
#define _SYS_REBOOT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Action codes — magic numbers matching sys/include/sys/reboot.h in
 * the kernel tree.  Do not renumber: the kernel switches on these
 * exact values. */
#define RB_AUTOBOOT     0x01234567  /* Reboot the machine. */
#define RB_HALT_SYSTEM  0xCEDEADBE  /* Halt: stop the CPU, leave power on. */
#define RB_POWER_OFF    0x4321FEDC  /* Power off (ATX soft-off when supported). */

int reboot(int cmd);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_REBOOT_H */
