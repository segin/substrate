/*
 * sd.h - SCSI Disk Driver Header
 */

#ifndef _SD_H
#define _SD_H

#include "scsi.h"

/* Initialize sd driver */
void sd_init(void);

/* Attach/detach SCSI disk */
int sd_attach(scsi_device_t *scsi_dev);
int sd_detach(scsi_device_t *scsi_dev);

#endif /* _SD_H */
