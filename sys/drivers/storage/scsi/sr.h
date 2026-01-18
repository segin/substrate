/*
 * sr.h - SCSI CD-ROM Driver Header
 */

#ifndef _SR_H
#define _SR_H

#include "scsi.h"

/* Initialize sr driver */
void sr_init(void);

/* Attach/detach SCSI CD-ROM */
int sr_attach(scsi_device_t *scsi_dev);
int sr_detach(scsi_device_t *scsi_dev);

/* Forward declaration */
typedef struct sr_device sr_device_t;

/* CD-ROM specific commands */
int sr_read_toc(sr_device_t *sr, void *buffer, uint16_t buflen);
int sr_start_stop(sr_device_t *sr, int load, int eject);
int sr_lock_door(sr_device_t *sr, int lock);

/* Lookup */
sr_device_t *sr_lookup(const char *name);

#endif /* _SR_H */
