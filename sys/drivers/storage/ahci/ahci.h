/*
 * ahci.h - AHCI (Advanced Host Controller Interface) Driver
 *
 * SATA host controller driver per AHCI 1.3.1 specification.
 * Supports SATA disks (via blkdev) and SATAPI (via SCSI mid-layer).
 *
 * References:
 * - Serial ATA AHCI 1.3.1 Specification (Intel)
 * - Serial ATA Revision 3.0 Specification (SATA-IO)
 * - ATA/ATAPI Command Set (ACS-3)
 */

#ifndef _AHCI_H
#define _AHCI_H

#include <stdint.h>
#include <stddef.h>

/*
 * ============================================================
 * FIS (Frame Information Structure) Types
 * ============================================================
 */
#define FIS_TYPE_REG_H2D    0x27    /* Register FIS - Host to Device */
#define FIS_TYPE_REG_D2H    0x34    /* Register FIS - Device to Host */
#define FIS_TYPE_DMA_ACT    0x39    /* DMA Activate FIS */
#define FIS_TYPE_DMA_SETUP  0x41    /* DMA Setup FIS - Bidirectional */
#define FIS_TYPE_DATA       0x46    /* Data FIS - Bidirectional */
#define FIS_TYPE_BIST       0x58    /* BIST Activate FIS */
#define FIS_TYPE_PIO_SETUP  0x5F    /* PIO Setup FIS - Device to Host */
#define FIS_TYPE_DEV_BITS   0xA1    /* Set Device Bits FIS - Device to Host */

/*
 * Register FIS - Host to Device (H2D)
 * Used to send ATA commands to the device.
 */
struct fis_reg_h2d {
    uint8_t  fis_type;      /* FIS_TYPE_REG_H2D */
    uint8_t  pmport_c;      /* [7:4] PM Port, [7] C = 1 for command */
    uint8_t  command;       /* ATA Command */
    uint8_t  featurel;      /* Feature (7:0) */

    uint8_t  lba0;          /* LBA (7:0) */
    uint8_t  lba1;          /* LBA (15:8) */
    uint8_t  lba2;          /* LBA (23:16) */
    uint8_t  device;        /* Device register */

    uint8_t  lba3;          /* LBA (31:24) */
    uint8_t  lba4;          /* LBA (39:32) */
    uint8_t  lba5;          /* LBA (47:40) */
    uint8_t  featureh;      /* Feature (15:8) */

    uint8_t  countl;        /* Sector Count (7:0) */
    uint8_t  counth;        /* Sector Count (15:8) */
    uint8_t  icc;           /* Isochronous Command Completion */
    uint8_t  control;       /* Control */

    uint8_t  rsv[4];        /* Reserved */
} __attribute__((packed));

/*
 * Register FIS - Device to Host (D2H)
 * Response to host-issued commands.
 */
struct fis_reg_d2h {
    uint8_t  fis_type;      /* FIS_TYPE_REG_D2H */
    uint8_t  pmport_i;      /* [7:4] PM Port, [6] I = interrupt */
    uint8_t  status;        /* Status register */
    uint8_t  error;         /* Error register */

    uint8_t  lba0;
    uint8_t  lba1;
    uint8_t  lba2;
    uint8_t  device;

    uint8_t  lba3;
    uint8_t  lba4;
    uint8_t  lba5;
    uint8_t  rsv0;

    uint8_t  countl;
    uint8_t  counth;
    uint8_t  rsv1[6];
} __attribute__((packed));

/*
 * PIO Setup FIS - Device to Host
 */
struct fis_pio_setup {
    uint8_t  fis_type;      /* FIS_TYPE_PIO_SETUP */
    uint8_t  pmport_id;     /* [7:4] PM Port, [6] I, [5] D */
    uint8_t  status;
    uint8_t  error;

    uint8_t  lba0;
    uint8_t  lba1;
    uint8_t  lba2;
    uint8_t  device;

    uint8_t  lba3;
    uint8_t  lba4;
    uint8_t  lba5;
    uint8_t  rsv0;

    uint8_t  countl;
    uint8_t  counth;
    uint8_t  rsv1;
    uint8_t  e_status;      /* New value of Status register */

    uint16_t tc;            /* Transfer Count */
    uint8_t  rsv2[2];
} __attribute__((packed));

/*
 * DMA Setup FIS - Bidirectional
 */
struct fis_dma_setup {
    uint8_t  fis_type;      /* FIS_TYPE_DMA_SETUP */
    uint8_t  pmport_aid;    /* [7:4] PM Port, [6] I, [5] D, [4] A */
    uint8_t  rsv0[2];

    uint64_t dma_buf_id;    /* DMA Buffer Identifier */
    uint32_t rsv1;
    uint32_t dma_buf_offset;
    uint32_t xfer_count;
    uint32_t rsv2;
} __attribute__((packed));

/*
 * Set Device Bits FIS - Device to Host
 */
struct fis_dev_bits {
    uint8_t  fis_type;      /* FIS_TYPE_DEV_BITS */
    uint8_t  pmport_in;     /* [7:4] PM Port, [6] I, [5] N */
    uint8_t  status;        /* Status register (hi/lo bits) */
    uint8_t  error;         /* Error register */

    uint32_t protocol;      /* Protocol-specific */
} __attribute__((packed));

/*
 * ============================================================
 * HBA Memory Registers (AHCI 1.3.1 Section 3.1)
 * ============================================================
 */

/* Port Signatures */
#define SATA_SIG_ATA    0x00000101  /* SATA drive */
#define SATA_SIG_ATAPI  0xEB140101  /* SATAPI drive */
#define SATA_SIG_SEMB   0xC33C0101  /* Enclosure management bridge */
#define SATA_SIG_PM     0x96690101  /* Port multiplier */

/* HBA Capabilities (CAP) bits */
#define HBA_CAP_S64A    (1U << 31)  /* 64-bit Addressing */
#define HBA_CAP_SNCQ    (1U << 30)  /* NCQ */
#define HBA_CAP_SSNTF   (1U << 29)  /* SNotification Register */
#define HBA_CAP_SMPS    (1U << 28)  /* Mechanical Presence Switch */
#define HBA_CAP_SSS     (1U << 27)  /* Staggered Spin-up */
#define HBA_CAP_SALP    (1U << 26)  /* Aggressive Link Power Mgmt */
#define HBA_CAP_SAL     (1U << 25)  /* Activity LED */
#define HBA_CAP_SCLO    (1U << 24)  /* Command List Override */
#define HBA_CAP_ISS_MASK (0xFU << 20)   /* Interface Speed Support */
#define HBA_CAP_ISS_SHIFT 20
#define HBA_CAP_SAM     (1U << 18)  /* AHCI Mode Only */
#define HBA_CAP_SPM     (1U << 17)  /* Port Multiplier */
#define HBA_CAP_FBSS    (1U << 16)  /* FIS-based Switching */
#define HBA_CAP_PMD     (1U << 15)  /* PIO Multiple DRQ Block */
#define HBA_CAP_SSC     (1U << 14)  /* Slumber State Capable */
#define HBA_CAP_PSC     (1U << 13)  /* Partial State Capable */
#define HBA_CAP_NCS_MASK (0x1FU << 8)   /* Number of Command Slots */
#define HBA_CAP_NCS_SHIFT 8
#define HBA_CAP_CCCS    (1U << 7)   /* Command Completion Coalescing */
#define HBA_CAP_EMS     (1U << 6)   /* Enclosure Management */
#define HBA_CAP_SXS     (1U << 5)   /* External SATA */
#define HBA_CAP_NP_MASK 0x1FU       /* Number of Ports (0-based) */

/* Global HBA Control (GHC) bits */
#define HBA_GHC_AE      (1U << 31)  /* AHCI Enable */
#define HBA_GHC_MRSM    (1U << 2)   /* MSI Revert to Single Message */
#define HBA_GHC_IE      (1U << 1)   /* Interrupt Enable */
#define HBA_GHC_HR      (1U << 0)   /* HBA Reset */

/* Port Command and Status (PxCMD) bits */
#define HBA_PXCMD_ICC_MASK  (0xFU << 28)  /* Interface Comm Control */
#define HBA_PXCMD_ASP       (1U << 27)    /* Aggressive Slumber / Partial */
#define HBA_PXCMD_ALPE      (1U << 26)    /* Aggressive Link PM Enable */
#define HBA_PXCMD_DLAE      (1U << 25)    /* Drive LED on ATAPI Enable */
#define HBA_PXCMD_ATAPI     (1U << 24)    /* Device is ATAPI */
#define HBA_PXCMD_APSTE     (1U << 23)    /* Auto Partial to Slumber */
#define HBA_PXCMD_FBSCP     (1U << 22)    /* FIS-based Switching Capable */
#define HBA_PXCMD_ESP       (1U << 21)    /* External SATA Port */
#define HBA_PXCMD_CPD       (1U << 20)    /* Cold Presence Detection */
#define HBA_PXCMD_MPSP      (1U << 19)    /* Mechanical Presence Switch */
#define HBA_PXCMD_HPCP      (1U << 18)    /* Hot Plug Capable */
#define HBA_PXCMD_PMA       (1U << 17)    /* Port Multiplier Attached */
#define HBA_PXCMD_CPS       (1U << 16)    /* Cold Presence State */
#define HBA_PXCMD_CR        (1U << 15)    /* Command List Running */
#define HBA_PXCMD_FR        (1U << 14)    /* FIS Receive Running */
#define HBA_PXCMD_MPSS      (1U << 13)    /* Mechanical Presence Switch State */
#define HBA_PXCMD_CCS_MASK  (0x1FU << 8)  /* Current Command Slot */
#define HBA_PXCMD_FRE       (1U << 4)     /* FIS Receive Enable */
#define HBA_PXCMD_CLO       (1U << 3)     /* Command List Override */
#define HBA_PXCMD_POD       (1U << 2)     /* Power On Device */
#define HBA_PXCMD_SUD       (1U << 1)     /* Spin-Up Device */
#define HBA_PXCMD_ST        (1U << 0)     /* Start */

/* Port Interrupt Status / Enable (PxIS / PxIE) bits */
#define HBA_PXIS_CPDS   (1U << 31)  /* Cold Port Detect Status */
#define HBA_PXIS_TFES   (1U << 30)  /* Task File Error Status */
#define HBA_PXIS_HBFS   (1U << 29)  /* Host Bus Fatal Error */
#define HBA_PXIS_HBDS   (1U << 28)  /* Host Bus Data Error */
#define HBA_PXIS_IFS    (1U << 27)  /* Interface Fatal Error */
#define HBA_PXIS_INFS   (1U << 26)  /* Interface Non-fatal Error */
#define HBA_PXIS_OFS    (1U << 24)  /* Overflow Status */
#define HBA_PXIS_IPMS   (1U << 23)  /* Incorrect Port Multiplier Status */
#define HBA_PXIS_PRCS   (1U << 22)  /* PhyRdy Change Status */
#define HBA_PXIS_DMPS   (1U << 7)   /* Device Mechanical Presence Status */
#define HBA_PXIS_PCS    (1U << 6)   /* Port Connect Change Status */
#define HBA_PXIS_DPS    (1U << 5)   /* Descriptor Processed */
#define HBA_PXIS_UFS    (1U << 4)   /* Unknown FIS */
#define HBA_PXIS_SDBS   (1U << 3)   /* Set Device Bits Interrupt */
#define HBA_PXIS_DSS    (1U << 2)   /* DMA Setup FIS Interrupt */
#define HBA_PXIS_PSS    (1U << 1)   /* PIO Setup FIS Interrupt */
#define HBA_PXIS_DHRS   (1U << 0)   /* D2H Register FIS Interrupt */

/* Port error bits (fatal errors to check) */
#define HBA_PXIS_FATAL  (HBA_PXIS_HBFS | HBA_PXIS_HBDS | \
                          HBA_PXIS_IFS  | HBA_PXIS_TFES)

/* Port Task File Data (PxTFD) bits */
#define HBA_PXTFD_ERR   (1U << 0)   /* Error */
#define HBA_PXTFD_DRQ   (1U << 3)   /* Data Request */
#define HBA_PXTFD_BSY   (1U << 7)   /* Busy */

/* Port SATA Status (PxSSTS) */
#define HBA_PXSSTS_DET_MASK     0x0FU
#define HBA_PXSSTS_DET_NONE     0x00U   /* No device, no phy */
#define HBA_PXSSTS_DET_PRESENT  0x01U   /* Device present, no phy comm */
#define HBA_PXSSTS_DET_ACTIVE   0x03U   /* Device present, phy comm established */
#define HBA_PXSSTS_DET_OFFLINE  0x04U   /* Phy in offline mode */
#define HBA_PXSSTS_SPD_MASK     (0x0FU << 4)
#define HBA_PXSSTS_IPM_MASK     (0x0FU << 8)
#define HBA_PXSSTS_IPM_ACTIVE   (0x01U << 8)

/* Port SATA Control (PxSCTL) */
#define HBA_PXSCTL_DET_MASK     0x0FU
#define HBA_PXSCTL_DET_INIT     0x01U   /* Perform interface comm init */
#define HBA_PXSCTL_DET_DISABLE  0x04U   /* Disable SATA interface */

/* ATA Commands used over AHCI */
#define AHCI_ATA_CMD_IDENTIFY        0xEC
#define AHCI_ATA_CMD_IDENTIFY_PACKET 0xA1
#define AHCI_ATA_CMD_READ_DMA_EXT    0x25
#define AHCI_ATA_CMD_WRITE_DMA_EXT   0x35
#define AHCI_ATA_CMD_FLUSH_CACHE_EXT 0xEA
#define AHCI_ATA_CMD_PACKET          0xA0

/* ATA Status bits */
#define AHCI_ATA_STATUS_ERR          0x01
#define AHCI_ATA_STATUS_DRQ          0x08
#define AHCI_ATA_STATUS_DF           0x20
#define AHCI_ATA_STATUS_DRDY         0x40
#define AHCI_ATA_STATUS_BSY          0x80

/*
 * ============================================================
 * HBA Port Registers (one set per port, offset 0x100 + port*0x80)
 * ============================================================
 */
typedef volatile struct hba_port {
    uint32_t clb;       /* 0x00: Command List Base Address (low) */
    uint32_t clbu;      /* 0x04: Command List Base Address (high) */
    uint32_t fb;        /* 0x08: FIS Base Address (low) */
    uint32_t fbu;       /* 0x0C: FIS Base Address (high) */
    uint32_t is;        /* 0x10: Interrupt Status */
    uint32_t ie;        /* 0x14: Interrupt Enable */
    uint32_t cmd;       /* 0x18: Command and Status */
    uint32_t rsv0;      /* 0x1C: Reserved */
    uint32_t tfd;       /* 0x20: Task File Data */
    uint32_t sig;       /* 0x24: Signature */
    uint32_t ssts;      /* 0x28: SATA Status (SCR0: SStatus) */
    uint32_t sctl;      /* 0x2C: SATA Control (SCR2: SControl) */
    uint32_t serr;      /* 0x30: SATA Error (SCR1: SError) */
    uint32_t sact;      /* 0x34: SATA Active (NCQ) */
    uint32_t ci;        /* 0x38: Command Issue */
    uint32_t sntf;      /* 0x3C: SNotification */
    uint32_t fbs;       /* 0x40: FIS-based Switching Control */
    uint32_t devslp;    /* 0x44: Device Sleep */
    uint32_t rsv1[10];  /* 0x48-0x6F: Reserved */
    uint32_t vs[4];     /* 0x70-0x7F: Vendor Specific */
} hba_port_t;

/*
 * ============================================================
 * HBA Memory Registers (Generic Host Control, offset 0x00)
 * ============================================================
 */
typedef volatile struct hba_mem {
    /* Generic Host Control (0x00 - 0x2B) */
    uint32_t cap;       /* 0x00: Host Capability */
    uint32_t ghc;       /* 0x04: Global Host Control */
    uint32_t is;        /* 0x08: Interrupt Status */
    uint32_t pi;        /* 0x0C: Port Implemented */
    uint32_t vs;        /* 0x10: Version */
    uint32_t ccc_ctl;   /* 0x14: Command Completion Coalescing Control */
    uint32_t ccc_pts;   /* 0x18: Command Completion Coalescing Ports */
    uint32_t em_loc;    /* 0x1C: Enclosure Management Location */
    uint32_t em_ctl;    /* 0x20: Enclosure Management Control */
    uint32_t cap2;      /* 0x24: Host Capabilities Extended */
    uint32_t bohc;      /* 0x28: BIOS/OS Handoff Control and Status */

    /* Reserved (0x2C - 0x9F) */
    uint8_t  rsv[0xA0 - 0x2C];

    /* Vendor Specific (0xA0 - 0xFF) */
    uint8_t  vendor[0x100 - 0xA0];

    /* Port Registers (0x100 - ...) */
    hba_port_t ports[];
} hba_mem_t;

/*
 * ============================================================
 * Command List Structures (AHCI 1.3.1 Section 4.2.2)
 * ============================================================
 */

/*
 * Command Header - one per command slot (32 per port)
 * Must be 1024-byte aligned. 32 headers = 1024 bytes per port.
 */
typedef struct hba_cmd_header {
    /* DW0 */
    uint8_t  cfl : 5;      /* Command FIS Length in DWORDs (2-16) */
    uint8_t  a : 1;        /* ATAPI */
    uint8_t  w : 1;        /* Write (1=H2D, 0=D2H) */
    uint8_t  p : 1;        /* Prefetchable */

    uint8_t  r : 1;        /* Reset */
    uint8_t  b : 1;        /* BIST */
    uint8_t  c : 1;        /* Clear Busy upon R_OK */
    uint8_t  rsv0 : 1;
    uint8_t  pmp : 4;      /* Port Multiplier Port */

    uint16_t prdtl;         /* Physical Region Descriptor Table Length */

    /* DW1 */
    volatile uint32_t prdbc; /* PRD Byte Count (transferred) */

    /* DW2-3 */
    uint32_t ctba;          /* Command Table Descriptor Base Address (low) */
    uint32_t ctbau;         /* Command Table Descriptor Base Address (high) */

    /* DW4-7: Reserved */
    uint32_t rsv1[4];
} __attribute__((packed)) hba_cmd_header_t;

/*
 * Physical Region Descriptor Table Entry
 * Data transfer scatter/gather entry.
 */
typedef struct hba_prdt_entry {
    uint32_t dba;           /* Data Base Address (low) */
    uint32_t dbau;          /* Data Base Address (high) */
    uint32_t rsv0;          /* Reserved */

    /* DW3 */
    uint32_t dbc : 22;     /* Data Byte Count (0-based, max 4MB) */
    uint32_t rsv1 : 9;     /* Reserved */
    uint32_t i : 1;        /* Interrupt on Completion */
} __attribute__((packed)) hba_prdt_entry_t;

/*
 * Command Table - one per command slot
 * Contains the Command FIS, ATAPI Command, and PRDT.
 * Must be 128-byte aligned.
 */
#define AHCI_MAX_PRDT_ENTRIES 8  /* Enough for 32KB per command (4KB per PRDT) */

typedef struct hba_cmd_table {
    uint8_t  cfis[64];     /* Command FIS */
    uint8_t  acmd[16];     /* ATAPI Command (12 or 16 bytes) */
    uint8_t  rsv[48];      /* Reserved */

    hba_prdt_entry_t prdt[AHCI_MAX_PRDT_ENTRIES];
} __attribute__((packed)) hba_cmd_table_t;

/*
 * ============================================================
 * FIS Receive Area (AHCI 1.3.1 Section 4.2.1)
 * ============================================================
 * 256 bytes, must be 256-byte aligned.
 */
typedef struct hba_fis {
    struct fis_dma_setup dsfis;     /* 0x00: DMA Setup FIS */
    uint8_t  pad0[4];

    struct fis_pio_setup psfis;     /* 0x20: PIO Setup FIS */
    uint8_t  pad1[12];

    struct fis_reg_d2h   rfis;      /* 0x40: Register D2H FIS */
    uint8_t  pad2[4];

    struct fis_dev_bits  sdbfis;    /* 0x58: Set Device Bits FIS */

    uint8_t  ufis[64];             /* 0x60: Unknown FIS */
    uint8_t  rsv[0x100 - 0xA0];   /* 0xA0: Reserved */
} __attribute__((packed)) hba_fis_t;

_Static_assert(sizeof(hba_fis_t) == 256, "hba_fis_t must be exactly 256 bytes");

/*
 * ============================================================
 * Driver Limits
 * ============================================================
 */
#define AHCI_MAX_PORTS      32
#define AHCI_MAX_CMD_SLOTS  32
#define AHCI_CMD_SLOT       0       /* Use slot 0 for single-threaded I/O */
#define AHCI_SECTOR_SIZE    512

/* Timeout values (milliseconds) */
#define AHCI_TIMEOUT_SPINUP     5000
#define AHCI_TIMEOUT_RESET      1000
#define AHCI_TIMEOUT_CMD        30000
#define AHCI_TIMEOUT_IDENTIFY   5000

/*
 * ============================================================
 * Public API
 * ============================================================
 */

void ahci_init(void);

#endif /* _AHCI_H */
