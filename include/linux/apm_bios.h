/*
 * <linux/apm_bios.h> — APM power-management compat stub.
 *
 * Substrate doesn't speak APM; kdrive's linux.c opens /dev/apm_bios
 * to listen for suspend/resume events.  Provide just enough that
 * the header compiles — the open() call in kdrive will fail and the
 * code path falls through to "no APM events."
 */
#ifndef _LINUX_APM_BIOS_H
#define _LINUX_APM_BIOS_H

struct apm_event_info {
    unsigned short event;
    unsigned short info;
};

#define APM_SYS_STANDBY     1
#define APM_SYS_SUSPEND     2
#define APM_NORMAL_RESUME   3
#define APM_CRITICAL_RESUME 4
#define APM_LOW_BATTERY     5
#define APM_POWER_STATUS_CHANGE 6
#define APM_UPDATE_TIME     7
#define APM_CRITICAL_SUSPEND 8
#define APM_USER_STANDBY    9
#define APM_USER_SUSPEND   10
#define APM_STANDBY_RESUME 11

/* _IO('A', n) on Linux expands to a #define-only macro; substrate
 * doesn't have it.  Hardcode the resulting ioctl numbers — the
 * device doesn't exist on substrate so the ioctl call fails with
 * ENOTTY and kdrive moves on. */
#define APM_IOC_STANDBY     0x4101u
#define APM_IOC_SUSPEND     0x4102u

typedef unsigned short apm_event_t;
#endif /* _LINUX_APM_BIOS_H */
