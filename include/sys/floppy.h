#ifndef _SYS_FLOPPY_H
#define _SYS_FLOPPY_H

#include <stdint.h>

#define FLOPPY_IOCTL_FORMAT_TRACK 0x4601

struct floppy_format_track {
    uint16_t cylinder;
    uint8_t head;
    uint8_t sector_size_code;
    uint8_t sectors_per_track;
    uint8_t gap3;
    uint8_t fill;
};

#endif
