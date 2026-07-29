/*
 * ahci.c - AHCI (Advanced Host Controller Interface) Driver
 *
 * Full SATA host controller driver implementing:
 * - PCI-based AHCI controller detection and initialization
 * - Per-port command list, FIS receive, and command table DMA setup
 * - SATA device detection via port signature (COMRESET)
 * - ATA IDENTIFY for disk geometry
 * - DMA read/write via ATA READ/WRITE DMA EXT commands
 * - Block device registration for SATA disks (blkdev_t)
 * - SCSI transport registration for SATAPI devices (scsi_link_t)
 * - Integration with Substrate driver model (PCI bus type)
 *
 * Architecture: polling mode (no IRQ). Single command slot (slot 0).
 *
 * References:
 * - Serial ATA AHCI 1.3.1 Specification
 * - ATA/ATAPI Command Set (ACS-3)
 */

#include <string.h>
#include <stdio.h>
#include <kern/console.h>
#include <kern/pci.h>
#include <kern/device.h>
#include <kern/driver.h>
#include <kern/resource.h>
#include <kern/time.h>
#include <kern/sched.h>
#include <sys/dma.h>
#include <sys/lock.h>
#include <sys/irq.h>

/* Pause-spin iterations to await command completion before yielding the CPU
 * in ahci_port_issue_cmd() — long enough to catch a fast (KVM/emulated)
 * completion at low latency, short enough that a real-latency transfer yields
 * (cmd_lock is a mutex, so a contender sleeps rather than spins). */
#define AHCI_POLL_SPIN_LIMIT   2048

/* When the completion IRQ is wired up, spin only this briefly for an
 * instant completion (avoids a context switch for sub-microsecond transfers)
 * before sleeping until ahci_irq() wakes us -- vs AHCI_POLL_SPIN_LIMIT MMIO
 * reads (each a VM-exit under KVM) burning the CPU for the whole DMA. */
#define AHCI_IRQ_FASTSPIN      64

/* COMRESET assert time.  SATA requires PxSCTL.DET=1 be held for at least
 * 1 ms; get_uptime_ms() advances in 4 ms ticks at HZ=250, so a deadline of
 * "+1" can expire on the very next tick after ~0 real time.  Ask for enough
 * ticks that at least 1 ms has provably elapsed. */
#define AHCI_COMRESET_HOLD_MS  10

/* ahci_port_issue_cmd() return for "the command failed AND the port could not
 * be quiesced".  Distinct from -1 because the HBA may still own the command
 * slot: the caller must leave every buffer it handed us alone rather than
 * returning it to the allocator. */
#define AHCI_CMD_WEDGED        (-2)

/* Per-port interrupt-enable mask: command-completion FIS interrupts plus the
 * fatal error bits the wait loop already checks (so an error wakes the waiter
 * immediately rather than after the re-check timeout). */
#define AHCI_PORT_IE_MASK  (HBA_PXIS_DHRS | HBA_PXIS_PSS | HBA_PXIS_DSS | \
                            HBA_PXIS_SDBS | HBA_PXIS_FATAL)
#include <drivers/storage/ahci/ahci.h>
#include <drivers/storage/blkdev.h>
#include <drivers/storage/scsi/scsi.h>

/*
 * ============================================================
 * Per-Port Driver State
 * ============================================================
 */

#define AHCI_PORT_TYPE_NONE     0
#define AHCI_PORT_TYPE_SATA     1
#define AHCI_PORT_TYPE_SATAPI   2
#define AHCI_PORT_TYPE_SEMB     3
#define AHCI_PORT_TYPE_PM       4

struct ahci_controller;

typedef struct ahci_port {
    int                port_num;
    int                type;        /* AHCI_PORT_TYPE_* */
    hba_port_t        *regs;        /* MMIO port registers */
    struct ahci_controller *ctrl;  /* owning HBA (for irq_ready) */
    char               io_wait;     /* completion wait channel; ahci_irq() wakes &io_wait */

    /* DMA areas */
    hba_cmd_header_t  *cmd_list;    /* Command list (32 entries, 1024 bytes) */
    dma_addr_t         cmd_list_dma;
    hba_fis_t         *fis_recv;    /* FIS receive area (256 bytes) */
    dma_addr_t         fis_recv_dma;
    hba_cmd_table_t   *cmd_table;   /* Command table for slot 0 */
    dma_addr_t         cmd_table_dma;

    /* Disk identity (from IDENTIFY) */
    uint64_t           sectors;
    uint32_t           sector_size;
    char               model[41];
    char               serial[21];
    char               firmware[9];
    int                lba48;

    /* Block device (for SATA disks) */
    blkdev_t           bdev;
    int                disk_index;  /* sata0, sata1, ... */

    /* Serialises the single shared command slot / cmd_table against
     * concurrent callers.  The AHCI engine here uses one command slot and
     * one command table per port; with kernel preemption enabled two threads
     * (e.g. two multi-sector reads from exec/ldd) can otherwise enter the
     * command path at once and clobber each other's FIS/PRDT, producing
     * corrupted or zero-filled reads.  A mutex (not a spinlock): the block
     * cache drops its bio spinlock before calling ->read (blkdev.c), so the
     * I/O path is sleepable, and ahci_port_issue_cmd() yields the CPU while
     * waiting for completion instead of pinning it (a spinlock would
     * disable preemption across the whole DMA, freezing the scheduler). */
    mutex_t            cmd_lock;

    /* PxIS bits the ISR has already consumed.  ahci_irq() must RW1C the
     * port's interrupt status to stop the interrupt reasserting, which
     * destroys exactly the FATAL bits the command waiter polls for -- so
     * the ISR ORs what it cleared in here and the waiter tests both.
     * Written by the ISR, cleared by the waiter before each command. */
    volatile uint32_t  pending_is;

    /* Set when the port could not be quiesced after a failed command.  The
     * HBA may still own the command slot and DMA into the buffers we handed
     * it, so nothing may be reclaimed and no further command may be issued
     * on this port. */
    int                wedged;
} ahci_port_t;

/*
 * ============================================================
 * Controller State
 * ============================================================
 */
typedef struct ahci_controller {
    hba_mem_t      *abar;           /* MMIO base (ioremap'd BAR5) */
    pci_device_t   *pci_dev;
    uint32_t        cap;            /* Cached CAP register */
    uint32_t        pi;             /* Ports Implemented bitmap */
    int             num_ports;      /* Number of ports (from CAP) */
    int             num_cmd_slots;  /* Number of command slots (from CAP) */
    ahci_port_t     ports[AHCI_MAX_PORTS];
    int             port_count;     /* Number of active ports */
    int             disk_count;     /* Running sata disk index */
    uint8_t         irq;            /* PCI interrupt line */
    volatile int    irq_ready;      /* completion IRQ hooked + enabled */
} ahci_controller_t;

/*
 * Substrate supports multiple AHCI HBAs (e.g. several `-device ich9-ahci`
 * in qemu).  Each PCI controller gets its own slot so a second HBA does
 * not clobber the first's port / blkdev state, and disk names come from a
 * single global counter so disks enumerate sata0, sata1, sata2, ... across
 * all controllers instead of every HBA restarting at sata0 (which would
 * collide on /dev/storage/sata0 and wedge the root mount).
 */
#define AHCI_MAX_CONTROLLERS 4
static ahci_controller_t ahci_ctrls[AHCI_MAX_CONTROLLERS];
static int ahci_ctrl_count;     /* number of attached HBAs */
static int ahci_disk_count;     /* global running sata%d index */
static int ahci_initialized;

static void *ahci_dma_bounce_alloc(size_t size, dma_addr_t *dma_handle) {
    return dma_alloc_coherent(size, dma_handle);
}

static void ahci_dma_bounce_free(void *buf, size_t size) {
    dma_free_coherent(buf, size);
}

/* SCSI link for SATAPI devices */
static scsi_link_t ahci_scsi_link;
static int ahci_scsi_registered;

/*
 * ============================================================
 * Timing Helpers
 * ============================================================
 */

static inline uint64_t ahci_time_ms(void) {
    return (uint64_t)get_uptime_ms();
}

/*
 * ============================================================
 * Port Command Engine Control
 * ============================================================
 */

/* Stop command engine on a port */
static int ahci_port_stop(hba_port_t *port) {
    uint64_t deadline;

    /* Clear ST (command engine) */
    port->cmd &= ~HBA_PXCMD_ST;

    /* Wait for CR to clear (command list not running) */
    deadline = ahci_time_ms() + 500;
    while (port->cmd & HBA_PXCMD_CR) {
        if (ahci_time_ms() > deadline) {
            kprint("ahci: port stop timeout (CR stuck)\n");
            return -1;
        }
        __asm__ volatile("pause");
    }

    /* Clear FRE (FIS receive) */
    port->cmd &= ~HBA_PXCMD_FRE;

    /* Wait for FR to clear */
    deadline = ahci_time_ms() + 500;
    while (port->cmd & HBA_PXCMD_FR) {
        if (ahci_time_ms() > deadline) {
            kprint("ahci: port stop timeout (FR stuck)\n");
            return -1;
        }
        __asm__ volatile("pause");
    }

    return 0;
}

/* Start command engine on a port.  Returns 0 on success, -1 if CR never
 * cleared -- in which case NOTHING was enabled and the caller must not
 * assume the port is usable. */
static int ahci_port_start(hba_port_t *port) {
    uint64_t deadline;

    /* Wait until CR clears before setting ST */
    deadline = ahci_time_ms() + 500;
    while (port->cmd & HBA_PXCMD_CR) {
        if (ahci_time_ms() > deadline) {
            kprint("ahci: port start timeout (CR stuck)\n");
            return -1;
        }
        __asm__ volatile("pause");
    }

    port->cmd |= HBA_PXCMD_FRE;
    port->cmd |= HBA_PXCMD_ST;
    return 0;
}

/*
 * Quiesce a port after a failed command, so the HBA provably no longer owns
 * the command slot or the buffers its PRDT points at.
 *
 * A plain stop/start is not enough.  When ahci_port_stop() gives up with
 * "CR stuck" the engine is still running, it never even reaches the FRE
 * clear, and ahci_port_start() then bails at its own CR wait having enabled
 * nothing -- leaving ST=0, CR=1, FRE=1, the slot outstanding, and the caller
 * free to hand the buffers back to the allocator.  AHCI 1.3.1 s10.4.2 says
 * the only recovery when CR will not clear is a port reset, so do that.
 *
 * Returns 0 when the port is quiesced and restarted (safe to reclaim
 * buffers), -1 when it is not (caller must reclaim nothing).
 */
static int ahci_port_recover(ahci_port_t *ap) {
    hba_port_t *port = ap->regs;
    uint64_t deadline;

    /* Clear latched errors first so the restart does not immediately trip. */
    port->serr = port->serr;
    port->is   = port->is;

    if (ahci_port_stop(port) == 0)
        return ahci_port_start(port);

    /* Stop failed: COMRESET.  DET=1 asserts the reset; it must be held for
     * at least 1 ms, then cleared, after which DET should read 3 (device
     * present, PHY communication established). */
    port->sctl = (port->sctl & ~HBA_PXSCTL_DET_MASK) | HBA_PXSCTL_DET_INIT;
    deadline = ahci_time_ms() + AHCI_COMRESET_HOLD_MS;
    while (ahci_time_ms() < deadline)
        __asm__ volatile("pause");
    port->sctl &= ~(uint32_t)HBA_PXSCTL_DET_MASK;

    deadline = ahci_time_ms() + 500;
    while ((port->ssts & HBA_PXSSTS_DET_MASK) != HBA_PXSSTS_DET_ACTIVE) {
        if (ahci_time_ms() > deadline) {
            kprint("ahci: port reset failed; port left wedged\n");
            return -1;
        }
        __asm__ volatile("pause");
    }

    port->serr = port->serr;
    port->is   = port->is;

    /* The reset dropped the engine, so CR must be clear now; if the stop
     * still will not take, give up rather than pretend. */
    if (ahci_port_stop(port) != 0) {
        kprint("ahci: engine still running after reset; port left wedged\n");
        return -1;
    }
    return ahci_port_start(port);
}

/*
 * ============================================================
 * Port Initialization
 * ============================================================
 */

static int ahci_port_alloc(ahci_port_t *ap) {
    /* Allocate Command List (1024 bytes, 1024-byte aligned) */
    ap->cmd_list = dma_alloc_coherent(sizeof(hba_cmd_header_t) * AHCI_MAX_CMD_SLOTS,
                                       &ap->cmd_list_dma);
    if (!ap->cmd_list) {
        kprint("ahci: failed to alloc command list\n");
        return -1;
    }

    /* Allocate FIS Receive Area (256 bytes, 256-byte aligned) */
    ap->fis_recv = dma_alloc_coherent(sizeof(hba_fis_t), &ap->fis_recv_dma);
    if (!ap->fis_recv) {
        kprint("ahci: failed to alloc FIS receive\n");
        dma_free_coherent(ap->cmd_list,
                          sizeof(hba_cmd_header_t) * AHCI_MAX_CMD_SLOTS);
        ap->cmd_list = NULL;
        return -1;
    }

    /* Allocate Command Table for slot 0 (128-byte aligned) */
    ap->cmd_table = dma_alloc_coherent(sizeof(hba_cmd_table_t), &ap->cmd_table_dma);
    if (!ap->cmd_table) {
        kprint("ahci: failed to alloc command table\n");
        dma_free_coherent(ap->fis_recv, sizeof(hba_fis_t));
        dma_free_coherent(ap->cmd_list,
                          sizeof(hba_cmd_header_t) * AHCI_MAX_CMD_SLOTS);
        ap->fis_recv = NULL;
        ap->cmd_list = NULL;
        return -1;
    }

    /* Point command header slot 0 at the command table */
    ap->cmd_list[AHCI_CMD_SLOT].ctba = (uint32_t)ap->cmd_table_dma;
    ap->cmd_list[AHCI_CMD_SLOT].ctbau = 0; /* 32-bit addresses */
    ap->cmd_list[AHCI_CMD_SLOT].prdtl = AHCI_MAX_PRDT_ENTRIES;

    return 0;
}

static int ahci_port_init(ahci_port_t *ap, hba_port_t *port_regs, int port_num) {
    ap->port_num = port_num;
    ap->regs = port_regs;
    ap->type = AHCI_PORT_TYPE_NONE;
    mutex_init(&ap->cmd_lock, "ahci_cmd");

    /* Stop command engine */
    if (ahci_port_stop(port_regs) < 0) {
        return -1;
    }

    /* Perform COMRESET to ensure clean port state */
    port_regs->sctl = (port_regs->sctl & ~HBA_PXSCTL_DET_MASK) | HBA_PXSCTL_DET_INIT;
    /* COMRESET must be asserted for at least 1ms (SATA spec) */
    {
        uint64_t comreset_end = ahci_time_ms() + 2;
        while (ahci_time_ms() < comreset_end)
            __asm__ volatile("pause");
    }
    port_regs->sctl &= ~HBA_PXSCTL_DET_MASK;  /* Clear DET to re-establish */

    /* Wait for device detection (DET=3 means phy communication established) */
    {
        uint64_t detect_deadline = ahci_time_ms() + AHCI_TIMEOUT_SPINUP;
        uint64_t empty_deadline = ahci_time_ms() + 200; /* 200ms fast-fail for empty ports */
        int empty_checked = 0;
        while ((port_regs->ssts & HBA_PXSSTS_DET_MASK) != HBA_PXSSTS_DET_ACTIVE) {
            if (ahci_time_ms() > detect_deadline) {
                break;  /* No device or slow device — will be caught by detect */
            }
            /* Fast-fail: if DET is still 0 after 200ms, no device attached */
            if (!empty_checked && ahci_time_ms() > empty_deadline) {
                empty_checked = 1;
                if ((port_regs->ssts & HBA_PXSSTS_DET_MASK) == 0) {
                    break;  /* No device responding at all */
                }
            }
            __asm__ volatile("pause");
        }
    }

    /* Clear errors accumulated during COMRESET */
    port_regs->serr = 0xFFFFFFFF;

    /* Allocate DMA memory */
    if (ahci_port_alloc(ap) < 0) {
        return -1;
    }

    /* Program CLB and FB registers */
    port_regs->clb  = (uint32_t)ap->cmd_list_dma;
    port_regs->clbu = 0;
    port_regs->fb   = (uint32_t)ap->fis_recv_dma;
    port_regs->fbu  = 0;

    /* Clear pending interrupts and errors */
    port_regs->serr = 0xFFFFFFFF;   /* Write-1-to-clear all error bits */
    port_regs->is   = 0xFFFFFFFF;   /* Write-1-to-clear all interrupt bits */

    /* Start command engine */
    ahci_port_start(port_regs);

    return 0;
}

/*
 * ============================================================
 * Device Detection
 * ============================================================
 */

static int ahci_port_detect_device(ahci_port_t *ap) {
    uint32_t ssts = ap->regs->ssts;
    uint8_t  det = ssts & HBA_PXSSTS_DET_MASK;
    uint8_t  ipm = (ssts & HBA_PXSSTS_IPM_MASK) >> 8;

    if (det != 0x03 || ipm != 0x01) {
        ap->type = AHCI_PORT_TYPE_NONE;
        return 0;   /* No device or not active */
    }

    switch (ap->regs->sig) {
    case SATA_SIG_ATA:
        ap->type = AHCI_PORT_TYPE_SATA;
        break;
    case SATA_SIG_ATAPI:
        ap->type = AHCI_PORT_TYPE_SATAPI;
        break;
    case SATA_SIG_SEMB:
        ap->type = AHCI_PORT_TYPE_SEMB;
        break;
    case SATA_SIG_PM:
        ap->type = AHCI_PORT_TYPE_PM;
        break;
    default:
        ap->type = AHCI_PORT_TYPE_SATA;  /* Assume SATA for unknown sigs */
        break;
    }

    return 1;   /* Device present */
}

/*
 * ============================================================
 * Command Execution (Polling)
 * ============================================================
 */

/*
 * Completion interrupt handler.  Shared-IRQ safe: returns 0 (not handled) when
 * this HBA has no pending interrupt, so other devices on the line still get a
 * look.  For each port flagged in HBA.IS, clear that port's latched PxIS and
 * wake any waiter parked in ahci_port_issue_cmd().  Clear order is PxIS then
 * HBA.IS (both write-1-to-clear), per AHCI 1.3.1 section 10.7.2.
 */
static int ahci_irq(unsigned int irq, void *dev_id, void *frame) {
    (void)irq; (void)frame;
    ahci_controller_t *ctrl = (ahci_controller_t *)dev_id;
    if (!ctrl || !ctrl->abar) return 0;

    uint32_t is = ctrl->abar->is;
    if (!is) return 0;                      /* not ours (shared line) */

    for (int p = 0; p < AHCI_MAX_PORTS; p++) {
        if (!(is & (1U << p))) continue;
        ahci_port_t *ap = &ctrl->ports[p];
        if (!ap->regs) continue;
        /* RW1C the port status -- but record what we cleared first.  The
         * command waiter's only error check is PxIS.FATAL, and clearing it
         * here without saving it leaves the waiter unable to see that the
         * command failed (PxCI is not cleared on a task-file error either),
         * so it spins to the full timeout on every recoverable device error. */
        uint32_t pis = ap->regs->is;
        ap->pending_is |= pis;
        ap->regs->is = pis;
        sched_wakeup(&ap->io_wait);          /* wake the command waiter, if any */
    }

    ctrl->abar->is = is;                     /* clear serviced HBA.IS bits (RW1C) */
    return 1;
}

/*
 * Arm completion interrupts on every active port and hook the HBA up via MSI,
 * then flip irq_ready so ahci_port_issue_cmd() switches from busy-polling to
 * sleeping until ahci_irq() wakes it.  Called once per controller after the
 * ports are probed (the probe itself runs polled, before this).  On any failure
 * the controller simply stays in polled mode.
 *
 * MSI (not the legacy INTx line) is used deliberately: MSI delivers to a
 * PRIVATE, edge-triggered LAPIC vector, so AHCI completions never share the
 * 8259 INTx line with other PCI devices.  Sharing INTx with a device that
 * asserts a level interrupt nothing acks (e.g. a polled NIC) storms the CPU;
 * a private MSI vector cannot.
 */
static void ahci_enable_interrupts(ahci_controller_t *ctrl) {
    hba_mem_t *abar = ctrl->abar;
    char buf[80];
    int vec;

    for (int p = 0; p < AHCI_MAX_PORTS; p++) {
        ahci_port_t *ap = &ctrl->ports[p];
        if (ap->type == AHCI_PORT_TYPE_NONE || !ap->regs) continue;
        ap->regs->is = ap->regs->is;         /* clear stale PxIS */
        ap->regs->ie = AHCI_PORT_IE_MASK;    /* enable completion/error IRQs */
    }

    abar->is = abar->is;                     /* clear stale HBA.IS */

    vec = irq_alloc_vector();
    if (vec < 0) {
        kprint("ahci: no free MSI vector; staying in polled mode\n");
        return;
    }
    if (request_irq((unsigned int)vec, ahci_irq, 0, "ahci", ctrl) != 0) {
        irq_free_vector(vec);
        kprint("ahci: request_irq failed; staying in polled mode\n");
        return;
    }
    if (pci_enable_msi(ctrl->pci_dev, (uint8_t)vec) != 0) {
        free_irq((unsigned int)vec, ctrl);
        irq_free_vector(vec);
        kprint("ahci: HBA has no MSI capability; staying in polled mode\n");
        return;
    }

    ctrl->irq = (uint8_t)vec;
    abar->ghc |= HBA_GHC_IE;                 /* global interrupt enable */
    ctrl->irq_ready = 1;

    snprintf(buf, sizeof(buf),
             "ahci: MSI vector 0x%x wired; completion-driven I/O\n",
             (unsigned)vec);
    kprint(buf);
}

/*
 * Issue a command in slot 0 and wait for completion.
 * The caller must have filled in cmd_list[0] and cmd_table before calling.
 */
static int ahci_port_issue_cmd(ahci_port_t *ap, uint32_t timeout_ms) {
    hba_port_t *port = ap->regs;
    uint64_t deadline;

    /* A port we failed to quiesce may still have the HBA writing into the
     * previous command's buffers.  Issuing another command would hand it a
     * second set. */
    if (ap->wedged)
        return AHCI_CMD_WEDGED;

    /* Clear interrupt status, and the copy the ISR accumulates for us. */
    ap->pending_is = 0;
    port->is = 0xFFFFFFFF;

    /* Issue command in slot 0 */
    port->ci = (1U << AHCI_CMD_SLOT);

    /* Poll for completion */
    deadline = ahci_time_ms() + timeout_ms;
    unsigned spins = 0;
    while (1) {
        /* Check if slot is done */
        if ((port->ci & (1U << AHCI_CMD_SLOT)) == 0) {
            break;
        }

        /* Check for fatal errors.  Test the ISR's accumulator too: once the
         * completion IRQ is live, ahci_irq() RW1Cs PxIS before we get here,
         * which would otherwise erase the very bits we are looking for and
         * leave us spinning until the timeout. */
        if ((port->is | ap->pending_is) & HBA_PXIS_FATAL) {
            char buf[96];
            snprintf(buf, sizeof(buf),
                     "ahci: port %d fatal error IS=0x%08x SERR=0x%08x TFD=0x%08x\n",
                     ap->port_num, port->is | ap->pending_is, port->serr,
                     port->tfd);
            kprint(buf);
            if (ahci_port_recover(ap) != 0) {
                ap->wedged = 1;
                return AHCI_CMD_WEDGED;
            }
            return -1;
        }

        /* Timeout check */
        if (ahci_time_ms() > deadline) {
            char buf[64];
            snprintf(buf, sizeof(buf),
                     "ahci: port %d command timeout\n", ap->port_num);
            kprint(buf);
            /*
             * The command is still outstanding (CI slot set): the HBA may
             * yet DMA into the command table / PRDT / data buffer.  If we
             * just returned, the caller would reclaim and reuse slot 0's
             * cmd_table for the next command while the controller finishes
             * the old transfer -> DMA into recycled memory (DRV-05).  Stop
             * the command engine to make the HBA relinquish the slot, clear
             * the latched errors, and restart, mirroring the fatal-error
             * path above (IDE quiesces on timeout the same way).
             *
             * ahci_port_recover() reports whether that actually worked.  If
             * it did not, the slot is still outstanding and the caller must
             * not give the buffers back to the allocator.
             */
            if (ahci_port_recover(ap) != 0) {
                ap->wedged = 1;
                return AHCI_CMD_WEDGED;
            }
            return -1;
        }

        /*
         * Completion wait.  cmd_lock is a mutex held across this wait, so a
         * contending thread sleeps on it rather than spinning.
         *
         * Once the completion IRQ is wired up (ap->ctrl->irq_ready), spin only
         * briefly for an instant completion, then sched_sleep_until() on this
         * port's channel: ahci_irq() clears PxIS and wakes us the moment the
         * DMA finishes, so the CPU idles (hlt) instead of burning thousands of
         * MMIO polls (each a VM-exit under KVM) for the whole transfer.  The
         * short re-check deadline is a lost-wakeup safety net -- if the IRQ
         * fires in the window between the CI check above and blocking, we wake
         * on the deadline and re-check CI rather than hang.
         *
         * Before IRQs are enabled (early boot / disk probe) fall back to the
         * original tight-spin-then-yield poll so those paths still work. */
        if (ap->ctrl && ap->ctrl->irq_ready && spins >= AHCI_IRQ_FASTSPIN) {
            uint32_t hz = get_hz();
            uint64_t d = get_ticks() + (hz >= 100 ? hz / 100u : 1u);  /* ~10 ms */
            sched_sleep_until(&ap->io_wait, d);
        } else if (spins < AHCI_POLL_SPIN_LIMIT) {
            spins++;
            __asm__ volatile("pause");
        } else {
            sched_yield();
        }
    }

    /* Check Task File for errors */
    if (port->tfd & (HBA_PXTFD_ERR | HBA_PXTFD_BSY)) {
        return -1;
    }

    return 0;
}

/*
 * Build a Register H2D FIS for an ATA command.
 */
static void ahci_build_h2d_fis(struct fis_reg_h2d *fis, uint8_t command,
                                 uint64_t lba, uint32_t count, uint8_t device) {
    memset(fis, 0, sizeof(*fis));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pmport_c = 0x80;   /* C bit = 1 → this is a command FIS */
    fis->command  = command;
    fis->device   = device;

    fis->lba0 = (uint8_t)(lba);
    fis->lba1 = (uint8_t)(lba >> 8);
    fis->lba2 = (uint8_t)(lba >> 16);
    fis->lba3 = (uint8_t)(lba >> 24);
    fis->lba4 = (uint8_t)(lba >> 32);
    fis->lba5 = (uint8_t)(lba >> 40);

    fis->countl = (uint8_t)(count);
    fis->counth = (uint8_t)(count >> 8);
}

/*
 * Execute a single ATA DMA command (read or write) on a port.
 * data_virt must be a kernel virtual address suitable for DMA.
 */
static int ahci_ata_dma_cmd(ahci_port_t *ap, uint8_t command,
                             uint64_t lba, uint32_t sector_count,
                             void *data_virt, int is_write) {
    hba_cmd_header_t *hdr = &ap->cmd_list[AHCI_CMD_SLOT];
    hba_cmd_table_t *tbl  = ap->cmd_table;
    struct fis_reg_h2d *fis;
    void *dma_buf;
    dma_addr_t data_dma;
    uint32_t byte_count;

    if (ap->sector_size != 0 && sector_count > SIZE_MAX / ap->sector_size) {
        return -1;
    }
    byte_count = sector_count * ap->sector_size;

    /* Serialise the shared command slot / table against concurrent callers
     * (kernel preemption can otherwise interleave two reads, corrupting both).
     * Held across issue + completion poll + data copy. */
    mutex_lock(&ap->cmd_lock);

    /* Clear command table */
    memset(tbl, 0, sizeof(hba_cmd_table_t));

    /* Build command FIS */
    fis = (struct fis_reg_h2d *)tbl->cfis;
    ahci_build_h2d_fis(fis, command, lba, sector_count, 0x40); /* LBA mode */

    dma_buf = NULL;
    data_dma = 0;
    if (byte_count > 0) {
        /*
         * DMA into a coherent bounce buffer instead of arbitrary kernel/user
         * virtual memory. The generic dma_map_single() path is currently a
         * direct phys translation and does not provide scatter-gather.
         */
        dma_buf = ahci_dma_bounce_alloc(byte_count, &data_dma);
        if (!dma_buf) {
            mutex_unlock(&ap->cmd_lock);
            return -1;
        }
        if (is_write) {
            memcpy(dma_buf, data_virt, byte_count);
        }
        tbl->prdt[0].dba  = (uint32_t)data_dma;
        tbl->prdt[0].dbau = 0;
        tbl->prdt[0].dbc  = byte_count - 1;  /* 0-based */
        tbl->prdt[0].i    = 0;
    }

    /* Setup command header */
    hdr->cfl   = sizeof(struct fis_reg_h2d) / 4;  /* 5 DWORDs */
    hdr->a     = 0;
    hdr->w     = is_write ? 1 : 0;
    hdr->p     = 1;    /* Prefetchable */
    hdr->r     = 0;
    hdr->b     = 0;
    hdr->c     = 1;    /* Clear BSY on R_OK */
    hdr->pmp   = 0;
    hdr->prdtl = 1;    /* 1 PRDT entry */
    hdr->prdbc = 0;

    /* Issue and wait */
    int ret = ahci_port_issue_cmd(ap, AHCI_TIMEOUT_CMD);

    if (dma_buf) {
        if (!is_write && ret == 0) {
            memcpy(data_virt, dma_buf, byte_count);
        }
        /* AHCI_CMD_WEDGED means the port could not be quiesced, so the HBA may
         * still complete this transfer into dma_buf.  Handing those pages back
         * to the allocator would let it DMA a sector into whatever is allocated
         * next; deliberately leak them instead. */
        if (ret != AHCI_CMD_WEDGED)
            ahci_dma_bounce_free(dma_buf, byte_count);
    }

    mutex_unlock(&ap->cmd_lock);
    return ret;
}

/*
 * ============================================================
 * IDENTIFY DEVICE
 * ============================================================
 */

static int ahci_identify(ahci_port_t *ap) {
    uint16_t *id_buf;
    dma_addr_t id_dma;
    hba_cmd_header_t *hdr;
    hba_cmd_table_t *tbl;
    struct fis_reg_h2d *fis;
    uint8_t cmd;
    int ret;
    int i;

    id_buf = dma_alloc_coherent(512, &id_dma);
    if (!id_buf) {
        kprint("ahci: failed to alloc identify buffer\n");
        return -1;
    }

    memset(ap->cmd_table, 0, sizeof(hba_cmd_table_t));

    /* Choose IDENTIFY command based on device type */
    cmd = (ap->type == AHCI_PORT_TYPE_SATAPI)
              ? AHCI_ATA_CMD_IDENTIFY_PACKET
              : AHCI_ATA_CMD_IDENTIFY;

    /* Build command FIS */
    tbl = ap->cmd_table;
    fis = (struct fis_reg_h2d *)tbl->cfis;
    memset(fis, 0, sizeof(*fis));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pmport_c = 0x80;
    fis->command  = cmd;
    fis->device   = 0;

    /* Single PRDT entry for 512-byte identify data */
    tbl->prdt[0].dba  = (uint32_t)id_dma;
    tbl->prdt[0].dbau = 0;
    tbl->prdt[0].dbc  = 511;  /* 0-based */
    tbl->prdt[0].i    = 0;

    /* Command header */
    hdr = &ap->cmd_list[AHCI_CMD_SLOT];
    hdr->cfl   = sizeof(struct fis_reg_h2d) / 4;
    hdr->a     = 0;
    hdr->w     = 0;     /* D2H (read) */
    hdr->p     = 1;
    hdr->r     = 0;
    hdr->b     = 0;
    hdr->c     = 1;
    hdr->pmp   = 0;
    hdr->prdtl = 1;
    hdr->prdbc = 0;

    ret = ahci_port_issue_cmd(ap, AHCI_TIMEOUT_IDENTIFY);
    if (ret < 0) {
        /* See ahci_ata_dma_cmd(): a wedged port may still write id_buf. */
        if (ret != AHCI_CMD_WEDGED)
            dma_free_coherent(id_buf, 512);
        return -1;
    }

    /* Parse IDENTIFY data */
    /* Model string: words 27-46 (byte-swapped pairs) */
    for (i = 0; i < 20; i++) {
        uint16_t w = id_buf[27 + i];
        ap->model[i * 2]     = (char)(w >> 8);
        ap->model[i * 2 + 1] = (char)(w & 0xFF);
    }
    ap->model[40] = '\0';
    /* Trim trailing spaces */
    for (i = 39; i >= 0 && ap->model[i] == ' '; i--) {
        ap->model[i] = '\0';
    }

    /* Serial number: words 10-19 */
    for (i = 0; i < 10; i++) {
        uint16_t w = id_buf[10 + i];
        ap->serial[i * 2]     = (char)(w >> 8);
        ap->serial[i * 2 + 1] = (char)(w & 0xFF);
    }
    ap->serial[20] = '\0';
    for (i = 19; i >= 0 && ap->serial[i] == ' '; i--) {
        ap->serial[i] = '\0';
    }

    /* Firmware revision: words 23-26 */
    for (i = 0; i < 4; i++) {
        uint16_t w = id_buf[23 + i];
        ap->firmware[i * 2]     = (char)(w >> 8);
        ap->firmware[i * 2 + 1] = (char)(w & 0xFF);
    }
    ap->firmware[8] = '\0';
    for (i = 7; i >= 0 && ap->firmware[i] == ' '; i--) {
        ap->firmware[i] = '\0';
    }

    /* Sector size.  The IDENTIFY response is attacker-controlled (a
     * malicious or buggy SATA device).  An out-of-range value here
     * cascades into byte_count overflow in PRDT setup and division by
     * zero in max_sectors math.  Accept only the four ATA-spec sector
     * sizes; otherwise fall back to 512. */
    ap->sector_size = AHCI_SECTOR_SIZE;
    if ((id_buf[106] & (1 << 14)) && !(id_buf[106] & (1 << 15))) {
        if (id_buf[106] & (1 << 12)) {
            uint32_t reported = ((uint32_t)id_buf[117] |
                                 ((uint32_t)id_buf[118] << 16)) * 2;
            switch (reported) {
            case 512: case 1024: case 2048: case 4096:
                ap->sector_size = reported;
                break;
            default:
                kprintf("ahci: ignoring nonsense sector_size=%u, using 512\n",
                        reported);
                break;
            }
        }
    }

    /* LBA48 support: word 83 bit 10, word 86 bit 10 */
    if ((id_buf[83] & (1 << 10)) && (id_buf[86] & (1 << 10))) {
        ap->lba48 = 1;
        ap->sectors = (uint64_t)id_buf[100] |
                      ((uint64_t)id_buf[101] << 16) |
                      ((uint64_t)id_buf[102] << 32) |
                      ((uint64_t)id_buf[103] << 48);
    } else {
        ap->lba48 = 0;
        ap->sectors = (uint64_t)id_buf[60] | ((uint64_t)id_buf[61] << 16);
    }

    dma_free_coherent(id_buf, 512);
    return 0;
}

/*
 * ============================================================
 * Block Device Interface (SATA Disks)
 * ============================================================
 */

static ahci_port_t *ahci_port_from_bdev(blkdev_t *bdev) {
    return (ahci_port_t *)bdev->priv;
}

static int ahci_bdev_read(blkdev_t *bdev, uint64_t sector,
                           uint32_t count, void *buffer) {
    ahci_port_t *ap = ahci_port_from_bdev(bdev);
    uint8_t *buf = (uint8_t *)buffer;
    uint32_t max_sectors;
    uint32_t chunk;

    if (!ap || ap->type != AHCI_PORT_TYPE_SATA) {
        return -1;
    }

    /* Max sectors per command: limited by PRDT (single entry, ~4MB) */
    max_sectors = (4 * 1024 * 1024) / ap->sector_size;
    if (max_sectors > 65535) {
        max_sectors = 65535;
    }

    while (count > 0) {
        chunk = (count > max_sectors) ? max_sectors : count;

        if (ahci_ata_dma_cmd(ap, AHCI_ATA_CMD_READ_DMA_EXT,
                              sector, chunk, buf, 0) < 0) {
            return -1;
        }

        sector += chunk;
        count  -= chunk;
        buf    += (uint64_t)chunk * ap->sector_size;
    }

    return 0;
}

static int ahci_bdev_write(blkdev_t *bdev, uint64_t sector,
                            uint32_t count, const void *buffer) {
    ahci_port_t *ap = ahci_port_from_bdev(bdev);
    const uint8_t *buf = (const uint8_t *)buffer;
    uint32_t max_sectors;
    uint32_t chunk;

    if (!ap || ap->type != AHCI_PORT_TYPE_SATA) {
        return -1;
    }

    max_sectors = (4 * 1024 * 1024) / ap->sector_size;
    if (max_sectors > 65535) {
        max_sectors = 65535;
    }

    while (count > 0) {
        chunk = (count > max_sectors) ? max_sectors : count;

        if (ahci_ata_dma_cmd(ap, AHCI_ATA_CMD_WRITE_DMA_EXT,
                              sector, chunk, (void *)buf, 1) < 0) {
            return -1;
        }

        sector += chunk;
        count  -= chunk;
        buf    += (uint64_t)chunk * ap->sector_size;
    }

    return 0;
}

static void ahci_register_disk(ahci_port_t *ap) {
    char buf[128];

    ap->disk_index = ahci_disk_count++;

    memset(&ap->bdev, 0, sizeof(blkdev_t));
    snprintf(ap->bdev.name, sizeof(ap->bdev.name), "sata%d", ap->disk_index);
    ap->bdev.sector_size   = ap->sector_size;
    ap->bdev.total_sectors = ap->sectors;
    ap->bdev.priv          = ap;
    ap->bdev.read          = ahci_bdev_read;
    ap->bdev.write         = ahci_bdev_write;

    blkdev_register_disk(&ap->bdev);

    snprintf(buf, sizeof(buf),
             "ahci: port %d: %s (%llu sectors, %u bytes/sect) -> /dev/storage/%s\n",
             ap->port_num, ap->model,
             (unsigned long long)ap->sectors, ap->sector_size,
             ap->bdev.name);
    kprint(buf);
}

/*
 * ============================================================
 * SCSI Transport Interface (SATAPI Devices)
 * ============================================================
 */

static int ahci_scsi_execute(scsi_link_t *link, scsi_request_t *req) {
    ahci_port_t *ap;
    hba_cmd_header_t *hdr;
    hba_cmd_table_t *tbl;
    struct fis_reg_h2d *fis;
    void *dma_buf;
    dma_addr_t data_dma;
    int port_idx;
    int ret;

    (void)link;

    if (!req || !req->device) {
        return -1;
    }

    /* Map SCSI target to AHCI port.
     * Target ID is the port index in our registration order. */
    port_idx = req->device->target;
    ap = NULL;
    for (int c = 0; c < ahci_ctrl_count && !ap; c++) {
        for (int i = 0; i < AHCI_MAX_PORTS; i++) {
            if (ahci_ctrls[c].ports[i].type == AHCI_PORT_TYPE_SATAPI) {
                if (port_idx == 0) {
                    ap = &ahci_ctrls[c].ports[i];
                    break;
                }
                port_idx--;
            }
        }
    }

    if (!ap) {
        req->status = SCSI_STATUS_CHECK_CONDITION;
        return -1;
    }

    /* Serialise the shared command slot / table (see ahci_ata_dma_cmd). */
    mutex_lock(&ap->cmd_lock);

    /* Build ATAPI command via AHCI */
    tbl = ap->cmd_table;
    memset(tbl, 0, sizeof(hba_cmd_table_t));

    /* Command FIS: ATA PACKET command */
    fis = (struct fis_reg_h2d *)tbl->cfis;
    memset(fis, 0, sizeof(*fis));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pmport_c = 0x80;
    fis->command  = AHCI_ATA_CMD_PACKET;
    /* Feature bit 0 = DMA mode, bit 2 = DMADIR (1=D2H read, 0=H2D write) */
    fis->featurel = (req->flags & SCSI_REQ_WRITE) ? 0x01 : 0x05;
    fis->lba1     = (uint8_t)(req->data_len);         /* Byte count low */
    fis->lba2     = (uint8_t)(req->data_len >> 8);    /* Byte count high */

    /* Copy SCSI CDB into ACMD field */
    memcpy(tbl->acmd, req->cdb,
           req->cdb_len > 16 ? 16 : req->cdb_len);

    /* Setup PRDT if there's data */
    hdr = &ap->cmd_list[AHCI_CMD_SLOT];
    hdr->prdtl = 0;
    dma_buf = NULL;
    data_dma = 0;

    if (req->data && req->data_len > 0) {
        dma_buf = ahci_dma_bounce_alloc(req->data_len, &data_dma);
        if (!dma_buf) {
            req->status = SCSI_STATUS_CHECK_CONDITION;
            mutex_unlock(&ap->cmd_lock);
            return -1;
        }
        if (req->flags & SCSI_REQ_WRITE) {
            memcpy(dma_buf, req->data, req->data_len);
        }
        tbl->prdt[0].dba  = (uint32_t)data_dma;
        tbl->prdt[0].dbau = 0;
        tbl->prdt[0].dbc  = req->data_len - 1;
        tbl->prdt[0].i    = 0;
        hdr->prdtl = 1;
    }

    /* Command header */
    hdr->cfl   = sizeof(struct fis_reg_h2d) / 4;
    hdr->a     = 1;    /* ATAPI command */
    hdr->w     = (req->flags & SCSI_REQ_WRITE) ? 1 : 0;
    hdr->p     = 1;
    hdr->r     = 0;
    hdr->b     = 0;
    hdr->c     = 1;
    hdr->pmp   = 0;
    hdr->prdbc = 0;

    ret = ahci_port_issue_cmd(ap, req->timeout_ms ? req->timeout_ms : AHCI_TIMEOUT_CMD);

    if (dma_buf) {
        if (!(req->flags & SCSI_REQ_WRITE) && ret == 0) {
            memcpy(req->data, dma_buf, req->data_len);
        }
        /* See ahci_ata_dma_cmd(): never reclaim from a wedged port. */
        if (ret != AHCI_CMD_WEDGED)
            ahci_dma_bounce_free(dma_buf, req->data_len);
    }

    if (ret < 0) {
        req->status = SCSI_STATUS_CHECK_CONDITION;
        mutex_unlock(&ap->cmd_lock);
        return -1;
    }

    req->status = SCSI_STATUS_GOOD;
    req->data_xfer = req->data_len;
    mutex_unlock(&ap->cmd_lock);
    return 0;
}

static int ahci_scsi_reset_device(scsi_link_t *link, scsi_device_t *dev) {
    (void)link;
    (void)dev;
    /* Port reset could be implemented via COMRESET */
    return 0;
}

static int ahci_scsi_reset_bus(scsi_link_t *link) {
    (void)link;
    return 0;
}

static void ahci_register_satapi_devices(void) {
    int satapi_count = 0;

    if (ahci_scsi_registered) {
        return;
    }
    for (int c = 0; c < ahci_ctrl_count; c++) {
        for (int i = 0; i < AHCI_MAX_PORTS; i++) {
            if (ahci_ctrls[c].ports[i].type == AHCI_PORT_TYPE_SATAPI) {
                satapi_count++;
            }
        }
    }

    if (satapi_count == 0) {
        return;
    }

    memset(&ahci_scsi_link, 0, sizeof(ahci_scsi_link));
    snprintf(ahci_scsi_link.name, sizeof(ahci_scsi_link.name), "ahci0");
    ahci_scsi_link.bus_id              = 1;  /* After ATAPI bus 0 */
    ahci_scsi_link.max_targets         = (uint8_t)satapi_count;
    ahci_scsi_link.max_luns            = 1;
    ahci_scsi_link.adapter_queue_depth = 1;
    ahci_scsi_link.execute             = ahci_scsi_execute;
    ahci_scsi_link.reset_device        = ahci_scsi_reset_device;
    ahci_scsi_link.reset_bus           = ahci_scsi_reset_bus;

    if (scsi_register_link(&ahci_scsi_link) == 0) {
        char buf[64];
        snprintf(buf, sizeof(buf),
                 "ahci: registered SATAPI transport (%d devices)\n",
                 satapi_count);
        kprint(buf);
        ahci_scsi_registered = 1;
    }
}

/*
 * ============================================================
 * HBA Initialization
 * ============================================================
 */

static int ahci_hba_init(ahci_controller_t *ctrl) {
    hba_mem_t *abar = ctrl->abar;
    uint32_t version;
    char buf[128];

    /* BIOS/OS Handoff (BOHC): take ownership from BIOS if needed */
    if (abar->cap2 & (1U << 0)) {  /* CAP2.BOH - BIOS/OS Handoff supported */
        if (abar->bohc & (1U << 0)) {  /* BOS - BIOS owns semaphore */
            uint64_t bohc_deadline;
            abar->bohc |= (1U << 1);   /* Set OOS - OS requests ownership */
            /* Wait for BIOS to release (BOS clears) */
            bohc_deadline = ahci_time_ms() + 2000;
            while (abar->bohc & (1U << 0)) {
                if (ahci_time_ms() > bohc_deadline) {
                    kprint("ahci: BIOS handoff timeout, forcing ownership\n");
                    break;
                }
                __asm__ volatile("pause");
            }
        }
    }

    /* Enable AHCI mode (GHC.AE) */
    abar->ghc |= HBA_GHC_AE;

    /* Read capabilities */
    ctrl->cap = abar->cap;
    ctrl->pi  = abar->pi;
    ctrl->num_ports = (ctrl->cap & HBA_CAP_NP_MASK) + 1;
    ctrl->num_cmd_slots = ((ctrl->cap & HBA_CAP_NCS_MASK) >> HBA_CAP_NCS_SHIFT) + 1;
    version = abar->vs;

    snprintf(buf, sizeof(buf),
             "ahci: version %d.%d%d, %d ports, %d cmd slots, PI=0x%08x\n",
             (int)((version >> 16) & 0xFFFF),
             (int)((version >> 8) & 0xFF),
             (int)(version & 0xFF),
             ctrl->num_ports,
             ctrl->num_cmd_slots,
             ctrl->pi);
    kprint(buf);

    if (ctrl->cap & HBA_CAP_S64A) {
        kprint("ahci: 64-bit addressing supported (using 32-bit)\n");
    }
    if (ctrl->cap & HBA_CAP_SNCQ) {
        kprint("ahci: NCQ supported (not used)\n");
    }

    return 0;
}

/*
 * ============================================================
 * Port Probing
 * ============================================================
 */

static void ahci_probe_ports(ahci_controller_t *ctrl) {
    uint32_t pi = ctrl->pi;
    int port;
    char buf[128];

    ctrl->port_count = 0;

    for (port = 0; port < AHCI_MAX_PORTS; port++) {
        ahci_port_t *ap;
        const char *type_str;
        uint32_t ssts;

        if (!(pi & (1U << port))) {
            continue;
        }

        /* Quick pre-check: if port shows no device at all, skip init */
        ssts = ctrl->abar->ports[port].ssts & HBA_PXSSTS_DET_MASK;
        if (ssts == 0x00) {
            continue;  /* No device, no phy — skip expensive COMRESET */
        }

        ap = &ctrl->ports[port];

        if (ahci_port_init(ap, &ctrl->abar->ports[port], port) < 0) {
            snprintf(buf, sizeof(buf),
                     "ahci: port %d init failed\n", port);
            kprint(buf);
            continue;
        }
        ap->ctrl = ctrl;   /* back-pointer so issue_cmd can see irq_ready */

        if (!ahci_port_detect_device(ap)) {
            continue;
        }

        switch (ap->type) {
        case AHCI_PORT_TYPE_SATA:   type_str = "SATA disk";   break;
        case AHCI_PORT_TYPE_SATAPI: type_str = "SATAPI";      break;
        case AHCI_PORT_TYPE_SEMB:   type_str = "SEMB";        break;
        case AHCI_PORT_TYPE_PM:     type_str = "Port Mult";   break;
        default:                    type_str = "unknown";      break;
        }

        snprintf(buf, sizeof(buf),
                 "ahci: port %d: %s detected (sig=0x%08x)\n",
                 port, type_str, ap->regs->sig);
        kprint(buf);

        ctrl->port_count++;

        /* IDENTIFY for SATA disks */
        if (ap->type == AHCI_PORT_TYPE_SATA) {
            if (ahci_identify(ap) == 0) {
                ahci_register_disk(ap);
            } else {
                snprintf(buf, sizeof(buf),
                         "ahci: port %d: IDENTIFY failed\n", port);
                kprint(buf);
            }
        }
    }

    if (ctrl->port_count == 0) {
        kprint("ahci: no devices found\n");
    }
    /* SATAPI/SCSI registration happens once, after every HBA is attached
     * (see ahci_init), so a single SCSI link can span all controllers. */
}

/*
 * ============================================================
 * PCI Driver Model Integration
 * ============================================================
 */

static int ahci_pci_attach(struct device *dev) {
    pci_device_t *pdev;
    void *mmio_base;
    size_t bar_sz;

    pdev = pci_find_device_by_kdev(dev);
    if (!pdev) {
        return -1;
    }

    /* Verify BAR5 is memory-mapped */
    if (pci_bar_type(pdev, 5) != PCI_BAR_MEM32) {
        kprint("ahci: BAR5 is not a 32-bit memory BAR\n");
        return -1;
    }

    bar_sz = pci_bar_size(pdev, 5);
    if (bar_sz == 0) {
        kprint("ahci: BAR5 size is 0\n");
        return -1;
    }

    /* Enable bus mastering and memory space */
    pci_write_config16(pdev->bus, pdev->slot, pdev->func,
                       PCI_CONFIG_COMMAND,
                       pci_read_config16(pdev->bus, pdev->slot, pdev->func,
                                         PCI_CONFIG_COMMAND) |
                       PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);

    /* Map AHCI registers */
    mmio_base = pci_iomap(pdev, 5, bar_sz);
    if (!mmio_base) {
        kprint("ahci: failed to iomap BAR5\n");
        return -1;
    }

    if (ahci_ctrl_count >= AHCI_MAX_CONTROLLERS) {
        kprint("ahci: too many AHCI controllers; ignoring this one\n");
        return -1;
    }

    ahci_controller_t *ctrl = &ahci_ctrls[ahci_ctrl_count];
    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->abar    = (hba_mem_t *)mmio_base;
    ctrl->pci_dev = pdev;

    if (ahci_hba_init(ctrl) < 0) {
        kprint("ahci: HBA init failed\n");
        return -1;
    }

    ahci_ctrl_count++;          /* commit the controller before probing */
    ahci_probe_ports(ctrl);     /* probe runs polled (irq_ready still 0) */
    ahci_enable_interrupts(ctrl);   /* switch to completion-driven I/O */
    return 0;
}

/* PCI ID table: Class 01h (Mass Storage), Subclass 06h (SATA), ProgIF 01h (AHCI) */
static const device_id_t ahci_pci_ids[] = {
    { DEVICE_ID_ANY, DEVICE_ID_ANY, 0x00010601U, 0x00FFFFFF, 0 },
    { 0, 0, 0, 0, 0 },
};

static struct driver ahci_pci_driver = {
    .name     = "ahci",
    .id_table = ahci_pci_ids,
    .attach   = ahci_pci_attach,
};

/*
 * ============================================================
 * Module Entry Point
 * ============================================================
 */

void ahci_init(void) {
    if (ahci_initialized) {
        return;
    }

    ahci_ctrl_count = 0;
    ahci_disk_count = 0;

    if (!pci_present()) {
        return;
    }

    /* driver_register synchronously attaches every matching PCI HBA, so by
     * the time it returns ahci_ctrls[0..ahci_ctrl_count) are all probed. */
    (void)driver_register(&ahci_pci_driver, &pci_bus_type);

    /* Register SATAPI (ATAPI) devices across all controllers in one SCSI
     * link, now that every HBA has been probed. */
    ahci_register_satapi_devices();

    ahci_initialized = 1;
}
