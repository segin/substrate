/*
 * uhci.h - UHCI (Universal Host Controller Interface) Driver
 *
 * Intel USB 1.1 host controller driver for QEMU/piix3 and real hardware.
 * I/O-port--based register access, frame list scheduling, TD/QH management.
 *
 * References:
 *   Universal Host Controller Interface (UHCI) Design Guide, Intel Rev 1.1
 */

#ifndef _UHCI_H
#define _UHCI_H

#include <stdint.h>

/*
 * ============================================================
 * UHCI I/O Port Registers (offsets from IOBASE)
 * ============================================================
 */
#define UHCI_USBCMD         0x00    /* USB Command */
#define UHCI_USBSTS         0x02    /* USB Status */
#define UHCI_USBINTR        0x04    /* USB Interrupt Enable */
#define UHCI_FRNUM          0x06    /* Frame Number */
#define UHCI_FLBASEADD      0x08    /* Frame List Base Address */
#define UHCI_SOFMOD          0x0C    /* Start of Frame Modify */
#define UHCI_PORTSC1        0x10    /* Port 1 Status/Control */
#define UHCI_PORTSC2        0x12    /* Port 2 Status/Control */

/*
 * USBCMD bits
 */
#define UHCI_CMD_RS         0x0001  /* Run/Stop */
#define UHCI_CMD_HCRESET    0x0002  /* Host Controller Reset */
#define UHCI_CMD_GRESET     0x0004  /* Global Reset */
#define UHCI_CMD_EGSM       0x0008  /* Enter Global Suspend Mode */
#define UHCI_CMD_FGR        0x0010  /* Force Global Resume */
#define UHCI_CMD_SWDBG      0x0020  /* Software Debug */
#define UHCI_CMD_CF         0x0040  /* Configure Flag */
#define UHCI_CMD_MAXP       0x0080  /* Max Packet (1=64 bytes, 0=32 bytes) */

/*
 * USBSTS bits
 */
#define UHCI_STS_USBINT     0x0001  /* USB Interrupt (transfer complete) */
#define UHCI_STS_ERROR      0x0002  /* USB Error Interrupt */
#define UHCI_STS_RD         0x0004  /* Resume Detect */
#define UHCI_STS_HSE        0x0008  /* Host System Error */
#define UHCI_STS_HCPE       0x0010  /* Host Controller Process Error */
#define UHCI_STS_HCH        0x0020  /* Host Controller Halted */

/*
 * USBINTR bits
 */
#define UHCI_INTR_TIMEOUT   0x0001  /* Timeout/CRC Interrupt Enable */
#define UHCI_INTR_RESUME    0x0002  /* Resume Interrupt Enable */
#define UHCI_INTR_IOC       0x0004  /* Interrupt on Complete Enable */
#define UHCI_INTR_SHORT     0x0008  /* Short Packet Interrupt Enable */

/*
 * PORTSC bits
 */
#define UHCI_PORTSC_CCS     0x0001  /* Current Connect Status */
#define UHCI_PORTSC_CSC     0x0002  /* Connect Status Change */
#define UHCI_PORTSC_PE      0x0004  /* Port Enabled */
#define UHCI_PORTSC_PEC     0x0008  /* Port Enable Change */
#define UHCI_PORTSC_LS      0x0030  /* Line Status (D+/D-) */
#define UHCI_PORTSC_LS_SHIFT 4
#define UHCI_PORTSC_RD      0x0040  /* Resume Detect */
#define UHCI_PORTSC_LSDA    0x0100  /* Low Speed Device Attached */
#define UHCI_PORTSC_PR      0x0200  /* Port Reset */
#define UHCI_PORTSC_SUSP    0x1000  /* Suspend */

/* Line status values */
#define UHCI_LS_SE0         0x00    /* SE0 (reset/disconnect) */
#define UHCI_LS_J           0x02    /* J-state (Full-speed) */
#define UHCI_LS_K           0x01    /* K-state (Low-speed) */

/*
 * ============================================================
 * Transfer Descriptor (TD) - 32 bytes, 16-byte aligned
 * ============================================================
 */

/* TD link pointer flags */
#define UHCI_TD_LINK_T      0x00000001  /* Terminate */
#define UHCI_TD_LINK_QH     0x00000002  /* QH (vs TD) */
#define UHCI_TD_LINK_VF     0x00000004  /* Depth-first (vs breadth) */

/* TD control/status bits */
#define UHCI_TD_CTRL_SPD    (1 << 29)   /* Short Packet Detect */
#define UHCI_TD_CTRL_CERR_SHIFT 27
#define UHCI_TD_CTRL_CERR_MASK  (3 << 27)
#define UHCI_TD_CTRL_LS     (1 << 26)   /* Low Speed Device */
#define UHCI_TD_CTRL_IOS    (1 << 25)   /* Isochronous Select */
#define UHCI_TD_CTRL_IOC    (1 << 24)   /* Interrupt on Complete */
#define UHCI_TD_CTRL_ACTIVE (1 << 23)   /* Active */
#define UHCI_TD_CTRL_STALLED (1 << 22)  /* Stalled */
#define UHCI_TD_CTRL_DBUFERR (1 << 21)  /* Data Buffer Error */
#define UHCI_TD_CTRL_BABBLE  (1 << 20)  /* Babble Detected */
#define UHCI_TD_CTRL_NAK     (1 << 19)  /* NAK Received */
#define UHCI_TD_CTRL_CRCTMO  (1 << 18)  /* CRC/Timeout Error */
#define UHCI_TD_CTRL_BITSTUFF (1 << 17) /* Bitstuff Error */

#define UHCI_TD_CTRL_ERRMASK (UHCI_TD_CTRL_STALLED | UHCI_TD_CTRL_DBUFERR | \
                              UHCI_TD_CTRL_BABBLE | UHCI_TD_CTRL_NAK | \
                              UHCI_TD_CTRL_CRCTMO | UHCI_TD_CTRL_BITSTUFF)

/* TD actlen: bits 0-10 of status word (actual length = actlen+1, 0x7FF = zero-length) */
#define UHCI_TD_ACTLEN_MASK     0x7FF
#define UHCI_TD_ACTLEN_NULL     0x7FF

/* TD token PIDs */
#define UHCI_TD_PID_SETUP   0x2D
#define UHCI_TD_PID_IN      0x69
#define UHCI_TD_PID_OUT     0xE1

/* TD token field construction */
#define UHCI_TD_TOKEN(pid, addr, ep, toggle, maxlen) \
    ((uint32_t)(pid) | \
     ((uint32_t)(addr) << 8) | \
     ((uint32_t)(ep) << 15) | \
     ((uint32_t)(toggle) << 19) | \
     ((uint32_t)((maxlen) - 1) << 21))

/* Special max length encoding for zero-length packets */
#define UHCI_TD_TOKEN_ZERO(pid, addr, ep, toggle) \
    ((uint32_t)(pid) | \
     ((uint32_t)(addr) << 8) | \
     ((uint32_t)(ep) << 15) | \
     ((uint32_t)(toggle) << 19) | \
     (0x7FFU << 21))

struct uhci_td {
    uint32_t link;          /* Link pointer (next TD or QH) */
    uint32_t ctrl_status;   /* Control and Status */
    uint32_t token;         /* Token (PID, address, endpoint, toggle, maxlen) */
    uint32_t buffer;        /* Buffer pointer (physical) */

    /* Software-use fields (not read by hardware) */
    uint32_t _pad[4];       /* Pad to 32 bytes for alignment */
} __attribute__((packed, aligned(16)));

/*
 * ============================================================
 * Queue Head (QH) - 16 bytes, 16-byte aligned
 * ============================================================
 */

#define UHCI_QH_LINK_T      0x00000001  /* Terminate */
#define UHCI_QH_LINK_QH     0x00000002  /* QH (vs TD) */

struct uhci_qh {
    uint32_t head_link;     /* Horizontal link (next QH) */
    uint32_t element_link;  /* Vertical link (first TD in queue) */

    /* Software use */
    uint32_t _pad[2];       /* Pad to 16 bytes */
} __attribute__((packed, aligned(16)));

/*
 * ============================================================
 * UHCI Driver Constants
 * ============================================================
 */
#define UHCI_FRAME_LIST_SIZE    1024    /* Frame list entries */
#define UHCI_MAX_TDS            64      /* TD pool size */
#define UHCI_MAX_QHS            8       /* QH pool size */
#define UHCI_NUM_PORTS          2       /* Root hub ports */

/*
 * PCI class/subclass/progif for UHCI
 */
#define UHCI_PCI_CLASS      0x0C0300    /* Serial Bus / USB / UHCI */

/*
 * ============================================================
 * Public API
 * ============================================================
 */
void uhci_init(void);

#endif /* _UHCI_H */
