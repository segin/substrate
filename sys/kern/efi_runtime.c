/*
 * kern/efi_runtime.c - EFI Runtime Services integration
 *
 * After the EFI boot stub saves the runtime services pointer and
 * exits boot services, this module maps the RT table into kernel
 * virtual space and exposes time, variable, and reset operations
 * to the rest of the kernel.
 *
 * If the kernel was not booted via EFI, all entry points are no-ops.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include <kern/console.h>
#include "efi_runtime.h"

/* Pull in EFI types used by the saved pointer (arch-specific). */
#include <arch/i386/efi.h>

/* Symbols exported by efi_boot.c (NULL when not EFI-booted). */
extern EFI_RUNTIME_SERVICES *efi_saved_runtime_services __attribute__((weak));

/* Mapped (virtual) pointer to the runtime services table. */
static EFI_RUNTIME_SERVICES *rt = NULL;

/* ==================== Initialisation ==================== */

void efi_runtime_init(void) {
    /*
     * efi_saved_runtime_services is a weak symbol — if no definition
     * exists (non-EFI build) the linker sets it to zero.  Check both
     * the symbol address and its value.
     */
    volatile void *sym = (volatile void *)&efi_saved_runtime_services;
    if (sym == NULL || efi_saved_runtime_services == NULL) {
        return;
    }

    /*
     * On IA-32 with identity-mapped low memory (0-16 MB) and the
     * direct-mapped kernel region, the runtime services table
     * typically resides in EfiRuntimeServicesData memory that is
     * already accessible.  For a fully correct implementation we
     * should call SetVirtualAddressMap, but many firmware
     * implementations on IA-32 place RT below 4 GB in regions
     * that our identity / direct mapping covers already.
     *
     * We therefore try the simplest path: if the pointer is in the
     * kernel direct-map range (physical < kernel_direct_map_size
     * i.e. first 16 MB identity-mapped in boot page tables), use
     * it directly.  Otherwise, convert using the standard
     * phys + 0xC0000000 direct-map convention where applicable.
     */
    uint32_t phys = (uint32_t)(uintptr_t)efi_saved_runtime_services;

    if (phys < 0x40000000) {
        /* Low physical: accessible through identity map or direct map */
        rt = (EFI_RUNTIME_SERVICES *)(uintptr_t)(phys + 0xC0000000);
    } else {
        /*
         * High physical address.  On i386 we cannot trivially ioremap
         * the RT table (it may span multiple pages and we don't know
         * its exact size).  Just use the raw pointer and hope the
         * initial page tables still cover it.
         */
        rt = efi_saved_runtime_services;
    }

    kprint("EFI: Runtime services available.\n");
}

bool efi_runtime_available(void) {
    return rt != NULL;
}

/* ==================== Time ==================== */

int efi_get_time(struct efi_time *time) {
    EFI_TIME efi_t;
    EFI_STATUS s;

    if (rt == NULL || rt->GetTime == NULL || time == NULL) {
        return -1;
    }

    s = rt->GetTime(&efi_t, NULL);
    if (s != EFI_SUCCESS) {
        return -1;
    }

    time->year       = efi_t.Year;
    time->month      = efi_t.Month;
    time->day        = efi_t.Day;
    time->hour       = efi_t.Hour;
    time->minute     = efi_t.Minute;
    time->second     = efi_t.Second;
    time->nanosecond = efi_t.Nanosecond;
    time->timezone   = efi_t.TimeZone;
    time->daylight   = efi_t.Daylight;

    return 0;
}

int efi_set_time(const struct efi_time *time) {
    EFI_TIME efi_t;
    EFI_STATUS s;

    if (rt == NULL || rt->SetTime == NULL || time == NULL) {
        return -1;
    }

    memset(&efi_t, 0, sizeof(efi_t));
    efi_t.Year       = time->year;
    efi_t.Month      = time->month;
    efi_t.Day        = time->day;
    efi_t.Hour       = time->hour;
    efi_t.Minute     = time->minute;
    efi_t.Second     = time->second;
    efi_t.Nanosecond = time->nanosecond;
    efi_t.TimeZone   = time->timezone;
    efi_t.Daylight   = time->daylight;

    s = rt->SetTime(&efi_t);
    return (s == EFI_SUCCESS) ? 0 : -1;
}

/* ==================== Reset ==================== */

void efi_reset_system(int type) {
    EFI_RESET_TYPE rt_type;

    if (rt == NULL || rt->ResetSystem == NULL) {
        return;
    }

    switch (type) {
    case EFI_RESET_WARM:
        rt_type = EfiResetWarm;
        break;
    case EFI_RESET_SHUTDOWN:
        rt_type = EfiResetShutdown;
        break;
    default:
        rt_type = EfiResetCold;
        break;
    }

    rt->ResetSystem(rt_type, EFI_SUCCESS, 0, NULL);
    /* Should not return */
}

/* ==================== Variables ==================== */

/*
 * Helper: convert ASCII name to UCS-2 on the stack.
 * Returns length in characters (excluding NUL).
 */
static int ascii_to_ucs2(const char *ascii, int16_t *ucs2, int max) {
    int i = 0;

    while (ascii[i] != '\0' && i < max - 1) {
        ucs2[i] = (int16_t)(unsigned char)ascii[i];
        i++;
    }
    ucs2[i] = 0;
    return i;
}

int efi_get_variable(const char *name, const uint8_t vendor[16],
    void *data, unsigned long *data_size) {
    int16_t ucs2_name[128];
    EFI_GUID guid;
    EFI_STATUS s;

    if (rt == NULL || rt->GetVariable == NULL || name == NULL ||
        vendor == NULL || data == NULL || data_size == NULL) {
        return -1;
    }

    ascii_to_ucs2(name, ucs2_name, 128);
    memcpy(&guid, vendor, sizeof(guid));

    s = rt->GetVariable(ucs2_name, &guid, NULL, data_size, data);
    return (s == EFI_SUCCESS) ? 0 : -1;
}

int efi_set_variable(const char *name, const uint8_t vendor[16],
    uint32_t attrs, const void *data, unsigned long data_size) {
    int16_t ucs2_name[128];
    EFI_GUID guid;
    EFI_STATUS s;

    if (rt == NULL || rt->SetVariable == NULL || name == NULL ||
        vendor == NULL) {
        return -1;
    }

    ascii_to_ucs2(name, ucs2_name, 128);
    memcpy(&guid, vendor, sizeof(guid));

    s = rt->SetVariable(ucs2_name, &guid, attrs, data_size, (void *)data);
    return (s == EFI_SUCCESS) ? 0 : -1;
}
