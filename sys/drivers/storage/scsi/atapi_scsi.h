/*
 * atapi_scsi.h - ATAPI SCSI Transport Adapter
 *
 * Bridges the SCSI mid-layer to IDE/ATAPI devices.
 */

#ifndef _ATAPI_SCSI_H
#define _ATAPI_SCSI_H

#include "scsi.h"

/*
 * Initialize ATAPI SCSI transport
 *
 * Probes for ATAPI devices on both IDE channels and registers
 * them with the SCSI mid-layer. Should be called after ide_init().
 */
void atapi_scsi_init(void);

/*
 * Get the ATAPI SCSI link
 *
 * Returns the scsi_link_t for the ATAPI transport, or NULL if
 * not initialized.
 */
scsi_link_t *atapi_get_link(void);

#endif /* _ATAPI_SCSI_H */
