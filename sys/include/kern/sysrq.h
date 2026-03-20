/*
 * kern/sysrq.h - Magic SysRq key handling
 *
 * Provides emergency keyboard and serial console commands for debugging
 * and recovery. Alt+SysRq+key on keyboard, or serial break+key on UART.
 */
#ifndef _KERN_SYSRQ_H
#define _KERN_SYSRQ_H

/* Initialize the SysRq subsystem */
void sysrq_init(void);

/*
 * Handle a SysRq key press.
 * Called from keyboard driver (after Alt+SysRq detected)
 * or from serial driver (after break condition + key).
 *
 * key: The ASCII character of the SysRq command (e.g., 'b', 's', 'h').
 */
void sysrq_handle(int key);

#endif /* _KERN_SYSRQ_H */
