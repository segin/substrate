#include "scsi.h"
#include "../../video/vga.h"

void scsi_init(void) {
    vga_write("SCSI Stack Initialized.\n", 24);
}

