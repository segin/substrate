/*
 * atapitur — an ATAPI command with no data (TEST UNIT READY) is issued.
 *
 * The AHCI driver refused any ATAPI command with no data (its byte-count
 * check rejected 0), and the IDE driver refused it the same way before
 * reaching its own non-data path: TEST UNIT READY, START STOP UNIT and
 * PREVENT ALLOW MEDIUM REMOVAL all failed without reaching the drive.
 * Sends TEST UNIT READY to every SCSI device node (attach an AHCI CD-ROM:
 * -device ahci,id=a -drive if=none,id=cd,media=cdrom
 * -device ide-cd,drive=cd,bus=a.0): the drive must answer -- GOOD, or
 * NOT READY / MEDIUM NOT PRESENT (02/3A) with no disc -- not the
 * ILLEGAL REQUEST / INVALID COMMAND (05/20) the SCSI layer made up when
 * the driver refused.  Run as root.  Substrate only.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* SCSI_IOCTL_SEND_CMD and its argument, i386 layout (scsi_ctl.c). */
#define SCSI_IOCTL_SEND_CMD 0x5305
struct scsi_ioctl_cmd {
    uint8_t  cdb[16];
    uint8_t  cdb_len;
    uint8_t  direction;
    uint32_t data_len;
    uint32_t data;
    uint8_t  sense[18];
    uint8_t  sense_len;
    uint8_t  status;
    uint32_t data_xfer;
    int32_t  error;
};

int main(void)
{
    DIR *d = opendir("/dev/storage/scsi");
    struct dirent *e;
    int tried = 0, refused = 0;

    if (!d) {
        printf("atapitur: no /dev/storage/scsi: FAIL\n");
        return 2;
    }
    while ((e = readdir(d)) != NULL) {
        char path[300];
        /* Device nodes are bus:target:lun; the others are buses. */
        if (e->d_name[0] == '.' || !strchr(e->d_name, ':')) continue;
        snprintf(path, sizeof(path), "/dev/storage/scsi/%s", e->d_name);
        int fd = open(path, O_RDWR);
        if (fd < 0) continue;
        struct scsi_ioctl_cmd c;
        memset(&c, 0, sizeof(c));
        c.cdb_len = 6;              /* TEST UNIT READY: all zero */
        errno = 0;
        int r = ioctl(fd, SCSI_IOCTL_SEND_CMD, &c);
        printf("  %s: ioctl %d (errno %d) status 0x%02x error %d sense %d:%02x/%02x/%02x\n",
               path, r, errno, c.status, c.error, c.sense_len,
               c.sense[2] & 0xF, c.sense[12], c.sense[13]);
        tried++;
        /* Refused: no status at all, or the 05/20 the layer made up. */
        if ((r < 0 && c.status == 0) ||
            (c.status == 0x02 && (c.sense[2] & 0xF) == 0x5 && c.sense[12] == 0x20))
            refused++;
        close(fd);
    }
    closedir(d);
    int pass = tried > 0 && refused == 0;
    printf("atapitur: %d tried, %d refused: %s\n", tried, refused,
           pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
