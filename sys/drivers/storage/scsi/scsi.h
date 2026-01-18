/*
 * scsi.h - SCSI Mid-Layer (CAM-like) Header
 *
 * Unified SCSI abstraction layer providing transport-independent
 * command execution for ATAPI, USB Mass Storage, and VirtIO-SCSI.
 *
 * Architecture:
 *   scsi_device  - Represents a SCSI target (Bus:Target:LUN)
 *   scsi_link    - Transport adapter interface (execute callback)
 *   scsi_request - Command execution context (CDB, data, sense, completion)
 */

#ifndef _SCSI_H
#define _SCSI_H

#include <stdint.h>
#include <stddef.h>

/*
 * ============================================================
 * SCSI Constants (SPC-3, MMC-5, SBC-3)
 * ============================================================
 */

/* SCSI Command Opcodes */
#define SCSI_CMD_TEST_UNIT_READY    0x00
#define SCSI_CMD_REQUEST_SENSE      0x03
#define SCSI_CMD_INQUIRY            0x12
#define SCSI_CMD_MODE_SELECT_6      0x15
#define SCSI_CMD_MODE_SENSE_6       0x1A
#define SCSI_CMD_START_STOP_UNIT    0x1B
#define SCSI_CMD_PREVENT_ALLOW      0x1E
#define SCSI_CMD_READ_CAPACITY_10   0x25
#define SCSI_CMD_READ_10            0x28
#define SCSI_CMD_WRITE_10           0x2A
#define SCSI_CMD_VERIFY_10          0x2F
#define SCSI_CMD_SYNCHRONIZE_CACHE  0x35
#define SCSI_CMD_READ_TOC           0x43
#define SCSI_CMD_MODE_SELECT_10     0x55
#define SCSI_CMD_MODE_SENSE_10      0x5A
#define SCSI_CMD_REPORT_LUNS        0xA0
#define SCSI_CMD_READ_12            0xA8
#define SCSI_CMD_WRITE_12           0xAA
#define SCSI_CMD_READ_16            0x88
#define SCSI_CMD_WRITE_16           0x8A
#define SCSI_CMD_SERVICE_ACTION_16  0x9E
#define SCSI_CMD_READ_CAPACITY_16   0x9E  /* Service action 0x10 */

/* SCSI Status Codes */
#define SCSI_STATUS_GOOD            0x00
#define SCSI_STATUS_CHECK_CONDITION 0x02
#define SCSI_STATUS_CONDITION_MET   0x04
#define SCSI_STATUS_BUSY            0x08
#define SCSI_STATUS_RESERVATION_CONFLICT 0x18
#define SCSI_STATUS_TASK_SET_FULL   0x28
#define SCSI_STATUS_ACA_ACTIVE      0x30
#define SCSI_STATUS_TASK_ABORTED    0x40

/* Sense Key Values (SPC-3 Table 27) */
#define SCSI_SENSE_NO_SENSE         0x0
#define SCSI_SENSE_RECOVERED_ERROR  0x1
#define SCSI_SENSE_NOT_READY        0x2
#define SCSI_SENSE_MEDIUM_ERROR     0x3
#define SCSI_SENSE_HARDWARE_ERROR   0x4
#define SCSI_SENSE_ILLEGAL_REQUEST  0x5
#define SCSI_SENSE_UNIT_ATTENTION   0x6
#define SCSI_SENSE_DATA_PROTECT     0x7
#define SCSI_SENSE_BLANK_CHECK      0x8
#define SCSI_SENSE_VENDOR_SPECIFIC  0x9
#define SCSI_SENSE_COPY_ABORTED     0xA
#define SCSI_SENSE_ABORTED_COMMAND  0xB
#define SCSI_SENSE_VOLUME_OVERFLOW  0xD
#define SCSI_SENSE_MISCOMPARE       0xE

/* Device Type Codes (SPC-3 Table 82) */
#define SCSI_TYPE_DISK              0x00  /* Direct Access (SBC) */
#define SCSI_TYPE_TAPE              0x01  /* Sequential Access (SSC) */
#define SCSI_TYPE_PRINTER           0x02
#define SCSI_TYPE_PROCESSOR         0x03
#define SCSI_TYPE_WORM              0x04
#define SCSI_TYPE_ROM               0x05  /* CD/DVD-ROM (MMC) */
#define SCSI_TYPE_SCANNER           0x06
#define SCSI_TYPE_OPTICAL           0x07
#define SCSI_TYPE_CHANGER           0x08
#define SCSI_TYPE_COMM              0x09
#define SCSI_TYPE_RAID              0x0C
#define SCSI_TYPE_ENCLOSURE         0x0D
#define SCSI_TYPE_RBC               0x0E
#define SCSI_TYPE_OSD               0x11
#define SCSI_TYPE_ZBC               0x14  /* Zoned Block (ZBC) */
#define SCSI_TYPE_NO_DEVICE         0x1F

/* Request flags */
#define SCSI_REQ_READ               0x0001
#define SCSI_REQ_WRITE              0x0002
#define SCSI_REQ_NO_SENSE           0x0004  /* Don't auto-request sense */
#define SCSI_REQ_SYNC               0x0008  /* Synchronous execution */
#define SCSI_REQ_QUIET              0x0010  /* Suppress error logging */

/* Request state */
#define SCSI_REQ_STATE_FREE         0
#define SCSI_REQ_STATE_PENDING      1
#define SCSI_REQ_STATE_ACTIVE       2
#define SCSI_REQ_STATE_COMPLETE     3
#define SCSI_REQ_STATE_ERROR        4
#define SCSI_REQ_STATE_TIMEOUT      5

/* Limits */
#define SCSI_MAX_CDB_LEN            16
#define SCSI_MAX_SENSE_LEN          252
#define SCSI_INQUIRY_LEN            36
#define SCSI_MAX_BUSES              8
#define SCSI_MAX_TARGETS            16
#define SCSI_MAX_LUNS               256

/*
 * ============================================================
 * Fixed Sense Data (SPC-3 7.3.3)
 * ============================================================
 */
struct scsi_sense_fixed {
    uint8_t  response_code;     /* 0x70 or 0x71 */
    uint8_t  obsolete;
    uint8_t  sense_key;         /* Bits 0-3: sense key, Bit 5: ILI, Bit 6: EOM, Bit 7: FileMark */
    uint8_t  info[4];           /* Command-specific */
    uint8_t  add_sense_len;     /* Additional sense length (n-7) */
    uint8_t  cmd_specific[4];   /* Command-specific */
    uint8_t  asc;               /* Additional Sense Code */
    uint8_t  ascq;              /* Additional Sense Code Qualifier */
    uint8_t  fruc;              /* Field Replaceable Unit Code */
    uint8_t  sks[3];            /* Sense Key Specific */
    uint8_t  additional[];      /* Additional sense bytes */
} __attribute__((packed));

/*
 * ============================================================
 * INQUIRY Response Data (SPC-3 6.4.2)
 * ============================================================
 */
struct scsi_inquiry_data {
    uint8_t  device_type;       /* Bits 0-4: type, Bits 5-7: qualifier */
    uint8_t  rmb;               /* Bit 7: Removable Media */
    uint8_t  version;           /* SCSI version */
    uint8_t  response_format;   /* Bits 0-3: format, Bit 4: HiSup, Bit 5: NormACA */
    uint8_t  additional_len;    /* Additional length (n-4) */
    uint8_t  flags1;            /* SCCS, ACC, TPGS, 3PC, Protect */
    uint8_t  flags2;            /* Obsolete, EncServ, VS, MultiP, etc */
    uint8_t  flags3;            /* Obsolete, CmdQue, VS */
    char     vendor[8];         /* Vendor ID (ASCII) */
    char     product[16];       /* Product ID (ASCII) */
    char     revision[4];       /* Product Revision (ASCII) */
} __attribute__((packed));

/*
 * ============================================================
 * READ CAPACITY (10) Response (SBC-3)
 * ============================================================
 */
struct scsi_read_capacity_10 {
    uint32_t lba;               /* Last LBA (big-endian) */
    uint32_t block_size;        /* Block size (big-endian) */
} __attribute__((packed));

/*
 * ============================================================
 * REPORT LUNS Response (SPC-3 6.21)
 * ============================================================
 */
#define SCSI_MAX_LUNS_RESPONSE  64  /* Max LUNs in single response */

struct scsi_report_luns_data {
    uint32_t length;            /* LUN list length in bytes (big-endian) */
    uint32_t reserved;
    uint64_t luns[SCSI_MAX_LUNS_RESPONSE];  /* LUN descriptors (big-endian) */
} __attribute__((packed));

/*
 * ============================================================
 * Forward Declarations
 * ============================================================
 */
struct scsi_device;
struct scsi_request;
struct scsi_link;

/*
 * ============================================================
 * SCSI Request (Command Execution Context)
 * ============================================================
 */
typedef void (*scsi_callback_t)(struct scsi_request *req);

typedef struct scsi_request {
    /* Linkage */
    struct scsi_device *device;     /* Target device */
    struct scsi_request *next;      /* Queue linkage */
    
    /* Command Descriptor Block */
    uint8_t  cdb[SCSI_MAX_CDB_LEN]; /* Command bytes */
    uint8_t  cdb_len;               /* CDB length (6, 10, 12, or 16) */
    
    /* Data Transfer */
    void    *data;                  /* Data buffer */
    uint32_t data_len;              /* Data buffer length */
    uint32_t data_xfer;             /* Bytes actually transferred */
    uint16_t flags;                 /* SCSI_REQ_* flags */
    
    /* Sense Data */
    uint8_t  sense[SCSI_MAX_SENSE_LEN]; /* Sense buffer */
    uint8_t  sense_len;             /* Sense data length */
    
    /* Status */
    uint8_t  status;                /* SCSI status byte */
    uint8_t  state;                 /* SCSI_REQ_STATE_* */
    int      error;                 /* Host error code (0 = success) */
    
    /* Timeout */
    uint32_t timeout_ms;            /* Timeout in milliseconds */
    uint32_t retries;               /* Retry count remaining */
    
    /* Completion */
    scsi_callback_t callback;       /* Async completion callback */
    void    *callback_arg;          /* Callback argument */
    
    /* Timing */
    uint64_t submit_time;           /* Submission timestamp */
} scsi_request_t;

/*
 * ============================================================
 * SCSI Transport Link (HBA Adapter Interface)
 * ============================================================
 */
typedef struct scsi_link {
    const char *name;               /* Transport name (e.g., "atapi", "usb") */
    
    /* Execute SCSI command via this transport */
    int (*execute)(struct scsi_link *link, scsi_request_t *req);
    
    /* Reset target device */
    int (*reset_device)(struct scsi_link *link, struct scsi_device *dev);
    
    /* Reset entire bus */
    int (*reset_bus)(struct scsi_link *link);
    
    /* Transport-specific private data */
    void *priv;
    
    /* Statistics */
    uint64_t commands_issued;
    uint64_t commands_failed;
    uint64_t bytes_read;
    uint64_t bytes_written;
} scsi_link_t;

/*
 * ============================================================
 * SCSI Device (Target Representation)
 * ============================================================
 */
typedef struct scsi_device {
    /* Addressing */
    uint8_t  bus;                   /* Bus/Controller number */
    uint8_t  target;                /* Target ID */
    uint16_t lun;                   /* Logical Unit Number */
    
    /* Device Identity (from INQUIRY) */
    uint8_t  type;                  /* Device type (SCSI_TYPE_*) */
    uint8_t  removable;             /* Removable media flag */
    char     vendor[9];             /* Vendor string (null-terminated) */
    char     product[17];           /* Product string (null-terminated) */
    char     revision[5];           /* Revision string (null-terminated) */
    
    /* Geometry (for block devices) */
    uint64_t capacity;              /* Total sectors */
    uint32_t sector_size;           /* Bytes per sector */
    
    /* State */
    uint8_t  online;                /* Device is online/ready */
    uint8_t  media_present;         /* Media is present (for removable) */
    uint8_t  write_protected;       /* Media is write-protected */
    
    /* Transport */
    scsi_link_t *link;              /* Transport adapter */
    void    *link_priv;             /* Transport-specific device data */
    
    /* Request Queue */
    scsi_request_t *queue_head;     /* Pending requests */
    scsi_request_t *queue_tail;
    uint32_t queue_depth;           /* Current queue depth */
    uint32_t max_queue_depth;       /* Maximum queue depth */
    
    /* Registry */
    struct scsi_device *next;       /* Device list linkage */
} scsi_device_t;

/*
 * ============================================================
 * Public API
 * ============================================================
 */

/* Initialization */
void scsi_init(void);

/* Transport Registration */
int scsi_register_link(scsi_link_t *link);
void scsi_unregister_link(scsi_link_t *link);

/* Device Management */
scsi_device_t *scsi_device_alloc(void);
void scsi_device_free(scsi_device_t *dev);
int scsi_device_register(scsi_device_t *dev);
void scsi_device_unregister(scsi_device_t *dev);
scsi_device_t *scsi_device_lookup(uint8_t bus, uint8_t target, uint16_t lun);

/* Request Management */
scsi_request_t *scsi_request_alloc(void);
void scsi_request_free(scsi_request_t *req);
void scsi_request_init(scsi_request_t *req, scsi_device_t *dev);

/* Command Execution */
int scsi_execute(scsi_request_t *req);
int scsi_execute_sync(scsi_device_t *dev, uint8_t *cdb, uint8_t cdb_len,
                      void *data, uint32_t data_len, uint16_t flags,
                      uint32_t timeout_ms);

/* Discovery */
int scsi_scan_bus(scsi_link_t *link, uint8_t bus);
int scsi_probe_lun(scsi_link_t *link, uint8_t bus, uint8_t target, uint16_t lun);

/* Standard Commands */
int scsi_test_unit_ready(scsi_device_t *dev);
int scsi_inquiry(scsi_device_t *dev, struct scsi_inquiry_data *inq);
int scsi_read_capacity(scsi_device_t *dev, uint64_t *sectors, uint32_t *sector_size);
int scsi_request_sense(scsi_device_t *dev, uint8_t *sense, uint8_t len);
int scsi_start_stop(scsi_device_t *dev, int start, int load_eject);
int scsi_report_luns(scsi_device_t *dev, struct scsi_report_luns_data *luns);

/* Sense Data Parsing */
int scsi_sense_key(const uint8_t *sense, uint8_t len);
int scsi_sense_asc(const uint8_t *sense, uint8_t len);
int scsi_sense_ascq(const uint8_t *sense, uint8_t len);
const char *scsi_sense_string(uint8_t key, uint8_t asc, uint8_t ascq);

/* CDB Helpers */
void scsi_cdb_read_10(uint8_t *cdb, uint32_t lba, uint16_t count);
void scsi_cdb_write_10(uint8_t *cdb, uint32_t lba, uint16_t count);
void scsi_cdb_read_capacity_10(uint8_t *cdb);
void scsi_cdb_inquiry(uint8_t *cdb, uint8_t len);
void scsi_cdb_test_unit_ready(uint8_t *cdb);

/* Byte Order Helpers */
static inline uint16_t scsi_be16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | p[1];
}

static inline uint32_t scsi_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static inline void scsi_put_be16(uint8_t *p, uint16_t val) {
    p[0] = (val >> 8) & 0xFF;
    p[1] = val & 0xFF;
}

static inline void scsi_put_be32(uint8_t *p, uint32_t val) {
    p[0] = (val >> 24) & 0xFF;
    p[1] = (val >> 16) & 0xFF;
    p[2] = (val >> 8) & 0xFF;
    p[3] = val & 0xFF;
}

#endif /* _SCSI_H */
