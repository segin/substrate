#include <drivers/storage/nvme/nvme.h>

#include <kern/console.h>
#include <kern/device.h>
#include <kern/pci.h>
#include <kern/time.h>

#include <stdio.h>
#include <string.h>
#include <sys/dma.h>

#define NVME_PCI_CLASS_STORAGE 0x01
#define NVME_PCI_SUBCLASS_NVM  0x08
#define NVME_PCI_PROGIF_NVME   0x02

static nvme_controller_t nvme_controllers[NVME_MAX_CONTROLLERS];
static size_t nvme_controller_total;

#ifdef HOST_TEST
extern uint32_t nvme_test_mmio_read32(volatile uint8_t *mmio, uint32_t reg);
extern void nvme_test_mmio_write32(volatile uint8_t *mmio, uint32_t reg, uint32_t value);
#endif

static uint64_t nvme_mmio_read64(volatile uint8_t *mmio, uint32_t reg) {
    volatile uint32_t *regs = (volatile uint32_t *)(mmio + reg);

    return (uint64_t)regs[0] | ((uint64_t)regs[1] << 32);
}

static uint32_t nvme_mmio_read32(volatile uint8_t *mmio, uint32_t reg) {
#ifdef HOST_TEST
    return nvme_test_mmio_read32(mmio, reg);
#else
    volatile uint32_t *regs = (volatile uint32_t *)(mmio + reg);
    return regs[0];
#endif
}

static void nvme_mmio_write32(volatile uint8_t *mmio, uint32_t reg, uint32_t value) {
#ifdef HOST_TEST
    nvme_test_mmio_write32(mmio, reg, value);
#else
    volatile uint32_t *regs = (volatile uint32_t *)(mmio + reg);
    regs[0] = value;
#endif
}

static void nvme_mmio_write64(volatile uint8_t *mmio, uint32_t reg, uint64_t value) {
    nvme_mmio_write32(mmio, reg, (uint32_t)(value & 0xFFFFFFFFU));
    nvme_mmio_write32(mmio, reg + 4U, (uint32_t)(value >> 32));
}

int nvme_decode_cap(uint64_t cap_raw, nvme_capability_t *cap) {
    if (cap == NULL) {
        return -1;
    }

    memset(cap, 0, sizeof(*cap));
    cap->mqes = (uint16_t)(cap_raw & 0xFFFFU);
    cap->timeout = (uint8_t)((cap_raw >> 24) & 0xFFU);
    cap->doorbell_stride = (uint8_t)((cap_raw >> 32) & 0x0FU);
    cap->timeout_ms = (uint32_t)cap->timeout * 500U;
    cap->doorbell_stride_bytes = 4U << cap->doorbell_stride;
    return 0;
}

size_t nvme_scan_controllers(nvme_controller_t *controllers, size_t max_controllers) {
    pci_device_t *pdev;
    size_t count = 0;

    if (controllers == NULL || max_controllers == 0) {
        return 0;
    }

    memset(controllers, 0, sizeof(*controllers) * max_controllers);

    for (pdev = pci_first_device(); pdev != NULL; pdev = pci_next_device(pdev)) {
        volatile uint8_t *mmio;
        uint16_t command;
        uint16_t want;
        nvme_controller_t *ctrl;
        char log_buf[128];

        if (count >= max_controllers) {
            break;
        }
        if (pdev->kdev == NULL) {
            continue;
        }
        if (pdev->kdev->class != NVME_PCI_CLASS_STORAGE ||
            pdev->kdev->subclass != NVME_PCI_SUBCLASS_NVM ||
            pdev->kdev->progif != NVME_PCI_PROGIF_NVME) {
            continue;
        }

        command = pci_read_config16(pdev->bus, pdev->slot, pdev->func, PCI_CONFIG_COMMAND);
        want = (uint16_t)(command | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
        if (want != command) {
            pci_write_config16(pdev->bus, pdev->slot, pdev->func, PCI_CONFIG_COMMAND, want);
        }

        mmio = (volatile uint8_t *)pci_iomap(pdev, 0, 0);
        if (mmio == NULL) {
            continue;
        }

        ctrl = &controllers[count];
        ctrl->bus = pdev->bus;
        ctrl->slot = pdev->slot;
        ctrl->func = pdev->func;
        ctrl->vendor_id = pdev->vendor_id;
        ctrl->device_id = pdev->device_id;
        ctrl->mmio = mmio;
        ctrl->cap_raw = nvme_mmio_read64(mmio, NVME_REG_CAP);
        ctrl->present = 1;
        if (nvme_decode_cap(ctrl->cap_raw, &ctrl->cap) < 0) {
            continue;
        }

        snprintf(log_buf, sizeof(log_buf),
                 "nvme: %u:%u.%u cap.to=%u (%ums) dstrd=%u (%uB)\n",
                 ctrl->bus, ctrl->slot, ctrl->func,
                 ctrl->cap.timeout, ctrl->cap.timeout_ms,
                 ctrl->cap.doorbell_stride,
                 ctrl->cap.doorbell_stride_bytes);
        kprint(log_buf);
        count++;
    }

    return count;
}

int nvme_disable_controller(nvme_controller_t *ctrl) {
    uint32_t cc;
    uint64_t start_ms;
    uint32_t timeout_ms;

    if (ctrl == NULL || ctrl->mmio == NULL || !ctrl->present) {
        return -1;
    }

    cc = nvme_mmio_read32(ctrl->mmio, NVME_REG_CC);
    if ((cc & NVME_CC_EN) == 0) {
        ctrl->enabled = 0;
        return 0;
    }

    nvme_mmio_write32(ctrl->mmio, NVME_REG_CC, cc & ~NVME_CC_EN);
    timeout_ms = ctrl->cap.timeout_ms;
    if (timeout_ms == 0) {
        timeout_ms = 500;
    }

    start_ms = (uint64_t)get_uptime_ms();
    while ((nvme_mmio_read32(ctrl->mmio, NVME_REG_CSTS) & NVME_CSTS_RDY) != 0) {
        if (((uint64_t)get_uptime_ms() - start_ms) >= timeout_ms) {
            return -1;
        }
        __asm__ volatile("pause");
    }

    ctrl->enabled = 0;
    return 0;
}

int nvme_configure_admin_queue_attrs(nvme_controller_t *ctrl,
                                     uint16_t sq_entries,
                                     uint16_t cq_entries) {
    uint32_t cc;
    uint32_t aqa;
    uint32_t max_entries;

    if (ctrl == NULL || ctrl->mmio == NULL || !ctrl->present) {
        return -1;
    }
    if (sq_entries < NVME_ADMIN_QUEUE_MIN_ENTRIES ||
        cq_entries < NVME_ADMIN_QUEUE_MIN_ENTRIES ||
        sq_entries > NVME_ADMIN_QUEUE_MAX_ENTRIES ||
        cq_entries > NVME_ADMIN_QUEUE_MAX_ENTRIES) {
        return -1;
    }

    max_entries = (uint32_t)ctrl->cap.mqes + 1U;
    if (ctrl->cap.mqes != 0 &&
        ((uint32_t)sq_entries > max_entries || (uint32_t)cq_entries > max_entries)) {
        return -1;
    }

    cc = nvme_mmio_read32(ctrl->mmio, NVME_REG_CC);
    if ((cc & NVME_CC_EN) != 0 || ctrl->enabled) {
        return -1;
    }

    aqa = (((uint32_t)sq_entries - 1U) << 16) | ((uint32_t)cq_entries - 1U);
    nvme_mmio_write32(ctrl->mmio, NVME_REG_AQA, aqa);
    ctrl->admin_sq_entries = sq_entries;
    ctrl->admin_cq_entries = cq_entries;
    return 0;
}

int nvme_create_admin_queues(nvme_controller_t *ctrl) {
    uint32_t cc;
    uint64_t asq;
    uint64_t acq;

    if (ctrl == NULL || ctrl->mmio == NULL || !ctrl->present) {
        return -1;
    }
    if (ctrl->admin_sq_entries < NVME_ADMIN_QUEUE_MIN_ENTRIES ||
        ctrl->admin_cq_entries < NVME_ADMIN_QUEUE_MIN_ENTRIES) {
        return -1;
    }
    if (ctrl->admin_sq != NULL || ctrl->admin_cq != NULL) {
        return -1;
    }

    cc = nvme_mmio_read32(ctrl->mmio, NVME_REG_CC);
    if ((cc & NVME_CC_EN) != 0 || ctrl->enabled) {
        return -1;
    }

    ctrl->admin_sq_bytes = (size_t)ctrl->admin_sq_entries * NVME_ADMIN_SQ_ENTRY_SIZE;
    ctrl->admin_cq_bytes = (size_t)ctrl->admin_cq_entries * NVME_ADMIN_CQ_ENTRY_SIZE;
    ctrl->admin_sq = dma_alloc_coherent(ctrl->admin_sq_bytes, &ctrl->admin_sq_dma);
    if (ctrl->admin_sq == NULL) {
        ctrl->admin_sq_bytes = 0;
        return -1;
    }

    ctrl->admin_cq = dma_alloc_coherent(ctrl->admin_cq_bytes, &ctrl->admin_cq_dma);
    if (ctrl->admin_cq == NULL) {
        dma_free_coherent(ctrl->admin_sq, ctrl->admin_sq_bytes);
        ctrl->admin_sq = NULL;
        ctrl->admin_sq_dma = (dma_addr_t)0;
        ctrl->admin_sq_bytes = 0;
        ctrl->admin_cq_bytes = 0;
        return -1;
    }

    asq = (uint64_t)ctrl->admin_sq_dma;
    acq = (uint64_t)ctrl->admin_cq_dma;
    nvme_mmio_write64(ctrl->mmio, NVME_REG_ASQ, asq);
    nvme_mmio_write64(ctrl->mmio, NVME_REG_ACQ, acq);
    return 0;
}

int nvme_enable_controller(nvme_controller_t *ctrl) {
    uint32_t cc;
    uint64_t start_ms;
    uint32_t timeout_ms;

    if (ctrl == NULL || ctrl->mmio == NULL || !ctrl->present) {
        return -1;
    }
    if (ctrl->admin_sq == NULL || ctrl->admin_cq == NULL) {
        return -1;
    }

    cc = nvme_mmio_read32(ctrl->mmio, NVME_REG_CC);
    if ((cc & NVME_CC_EN) != 0) {
        if ((nvme_mmio_read32(ctrl->mmio, NVME_REG_CSTS) & NVME_CSTS_RDY) != 0) {
            ctrl->enabled = 1;
            return 0;
        }
        return -1;
    }

    cc = ((uint32_t)NVME_ADMIN_SQ_ENTRY_EXP << NVME_CC_IOSQES_SHIFT) |
         ((uint32_t)NVME_ADMIN_CQ_ENTRY_EXP << NVME_CC_IOCQES_SHIFT) |
         NVME_CC_EN;
    nvme_mmio_write32(ctrl->mmio, NVME_REG_CC, cc);

    timeout_ms = ctrl->cap.timeout_ms;
    if (timeout_ms == 0) {
        timeout_ms = 500;
    }

    start_ms = (uint64_t)get_uptime_ms();
    while ((nvme_mmio_read32(ctrl->mmio, NVME_REG_CSTS) & NVME_CSTS_RDY) == 0) {
        if (((uint64_t)get_uptime_ms() - start_ms) >= timeout_ms) {
            return -1;
        }
        __asm__ volatile("pause");
    }

    ctrl->enabled = 1;
    return 0;
}

size_t nvme_controller_count(void) {
    return nvme_controller_total;
}

const nvme_controller_t *nvme_get_controller(size_t index) {
    if (index >= nvme_controller_total) {
        return NULL;
    }
    return &nvme_controllers[index];
}

void nvme_init(void) {
    nvme_controller_total = nvme_scan_controllers(nvme_controllers,
                                                  NVME_MAX_CONTROLLERS);

    if (nvme_controller_total == 0) {
        kprint("nvme: no controllers detected\n");
    }
}
