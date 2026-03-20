/*
 * kern/efi_runtime.h - EFI Runtime Services kernel interface
 *
 * Provides access to UEFI runtime services (time, variables, reset)
 * after ExitBootServices.  Only available when the kernel was booted
 * via the EFI stub; all functions return -1 / false when runtime
 * services are not present.
 */

#ifndef _KERN_EFI_RUNTIME_H
#define _KERN_EFI_RUNTIME_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Kernel-facing EFI time structure (mirrors EFI_TIME layout). */
struct efi_time {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  pad1;
    uint32_t nanosecond;
    int16_t  timezone;
    uint8_t  daylight;
    uint8_t  pad2;
};

/* Reset types */
#define EFI_RESET_COLD     0
#define EFI_RESET_WARM     1
#define EFI_RESET_SHUTDOWN 2

/*
 * efi_runtime_init - Wire up saved runtime services pointer.
 *
 * Called once during kernel boot after paging and ioremap are available.
 * Maps the EFI runtime services table into kernel virtual address space
 * and optionally calls SetVirtualAddressMap.
 */
void efi_runtime_init(void);

/* Returns true if EFI runtime services are available. */
bool efi_runtime_available(void);

/*
 * efi_get_time - Read the hardware clock via EFI GetTime.
 * Returns 0 on success, -1 on failure.
 */
int efi_get_time(struct efi_time *time);

/*
 * efi_set_time - Set the hardware clock via EFI SetTime.
 * Returns 0 on success, -1 on failure.
 */
int efi_set_time(const struct efi_time *time);

/*
 * efi_reset_system - Reset or shut down the machine via EFI ResetSystem.
 * type: EFI_RESET_COLD, EFI_RESET_WARM, or EFI_RESET_SHUTDOWN.
 * Does not return on success.
 */
void efi_reset_system(int type);

/*
 * efi_get_variable - Read an EFI variable.
 * name:      NUL-terminated ASCII variable name (converted internally).
 * vendor:    16-byte GUID of the variable vendor.
 * data:      Output buffer.
 * data_size: In/out size of data buffer.
 * Returns 0 on success, -1 on failure.
 */
int efi_get_variable(const char *name, const uint8_t vendor[16],
    void *data, unsigned long *data_size);

/*
 * efi_set_variable - Write an EFI variable.
 * name:      NUL-terminated ASCII variable name.
 * vendor:    16-byte GUID of the variable vendor.
 * attrs:     EFI variable attributes.
 * data:      Data buffer.
 * data_size: Size of data.
 * Returns 0 on success, -1 on failure.
 */
int efi_set_variable(const char *name, const uint8_t vendor[16],
    uint32_t attrs, const void *data, unsigned long data_size);

#endif /* _KERN_EFI_RUNTIME_H */
