#include <stdio.h>
#include <string.h>

#include <drivers/storage/nvme/nvme.h>
#include <kern/console.h>
#include <kern/device.h>
#include <kern/pci.h>
#include <kern/time.h>
#include <sys/dma.h>

#define NVME_PCI_CLASS_STORAGE 0x01
#define NVME_PCI_SUBCLASS_NVM  0x08
#define NVME_PCI_PROGIF_NVME   0x02
#define NVME_ADMIN_OP_DELETE_IO_SQ   0x00U
#define NVME_ADMIN_OP_CREATE_IO_SQ   0x01U
#define NVME_ADMIN_OP_DELETE_IO_CQ   0x04U
#define NVME_ADMIN_OP_CREATE_IO_CQ   0x05U
#define NVME_ADMIN_OP_IDENTIFY       0x06U
#define NVME_ADMIN_OP_SET_FEATURES   0x09U
#define NVME_NVM_OP_WRITE            0x01U
#define NVME_NVM_OP_READ             0x02U
#define NVME_FEATURE_NUMBER_OF_QUEUES 0x07U
#define NVME_QUEUE_FLAG_PC           0x0001U
#define NVME_QUEUE_FLAG_IEN          0x0002U
#define NVME_IDENTIFY_CNS_NAMESPACE  0x00U
#define NVME_IDENTIFY_CNS_CONTROLLER 0x01U
#define NVME_CQE_STATUS_PHASE 0x0001U
#define NVME_CQE_STATUS_MASK  0xFFFEU

typedef struct nvme_admin_cmd {
    uint8_t opcode;
    uint8_t flags;
    uint16_t cid;
    uint32_t nsid;
    uint64_t reserved;
    uint64_t metadata;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} nvme_admin_cmd_t;

typedef struct nvme_cqe {
    uint32_t result;
    uint32_t reserved;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cid;
    uint16_t status;
} nvme_cqe_t;

static nvme_controller_t nvme_controllers[NVME_MAX_CONTROLLERS];
static size_t nvme_controller_total;

#ifdef HOST_TEST
extern uint32_t nvme_test_mmio_read32(volatile uint8_t *mmio, uint32_t reg);
extern void nvme_test_mmio_write32(volatile uint8_t *mmio, uint32_t reg, uint32_t value);
extern void nvme_test_admin_kick(nvme_controller_t *ctrl, uint16_t sq_tail);
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

static uint32_t nvme_sq_doorbell_reg(const nvme_controller_t *ctrl, uint16_t qid) {
    uint32_t offset = (uint32_t)(2U * qid) * ctrl->cap.doorbell_stride_bytes;
    if (offset > 0x100000U) return NVME_REG_DBS; /* overflow guard */
    return NVME_REG_DBS + offset;
}

static uint32_t nvme_cq_doorbell_reg(const nvme_controller_t *ctrl, uint16_t qid) {
    uint32_t offset = (uint32_t)(2U * qid + 1U) * ctrl->cap.doorbell_stride_bytes;
    if (offset > 0x100000U) return NVME_REG_DBS; /* overflow guard */
    return NVME_REG_DBS + offset;
}

static uint32_t nvme_sq0_doorbell_reg(const nvme_controller_t *ctrl) {
    return nvme_sq_doorbell_reg(ctrl, 0);
}

static uint32_t nvme_cq0_doorbell_reg(const nvme_controller_t *ctrl) {
    return nvme_cq_doorbell_reg(ctrl, 0);
}

static void nvme_copy_trim_string(char *dst, size_t dst_len,
                                  const uint8_t *src, size_t src_len) {
    size_t copy_len;

    if (dst == NULL || dst_len == 0) {
        return;
    }

    copy_len = src_len;
    while (copy_len > 0 && src[copy_len - 1] == ' ') {
        copy_len--;
    }
    if (copy_len >= dst_len) {
        copy_len = dst_len - 1;
    }
    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
}

static uint32_t nvme_get_le32(const uint8_t *buf) {
    return (uint32_t)buf[0] |
           ((uint32_t)buf[1] << 8) |
           ((uint32_t)buf[2] << 16) |
           ((uint32_t)buf[3] << 24);
}

static uint64_t nvme_get_le64(const uint8_t *buf) {
    return (uint64_t)nvme_get_le32(buf) |
           ((uint64_t)nvme_get_le32(buf + 4) << 32);
}

static int nvme_admin_submit_sync(nvme_controller_t *ctrl,
                                  nvme_admin_cmd_t *cmd,
                                  nvme_cqe_t *cqe,
                                  uint32_t timeout_ms) {
    nvme_admin_cmd_t *sq;
    nvme_cqe_t *cq;
    uint16_t tail;
    uint16_t head;
    uint16_t cid;
    uint64_t start_ms;

    if (ctrl == NULL || cmd == NULL || cqe == NULL) {
        return -1;
    }
    if (!ctrl->enabled || ctrl->admin_sq == NULL || ctrl->admin_cq == NULL) {
        return -1;
    }

    sq = (nvme_admin_cmd_t *)ctrl->admin_sq;
    cq = (nvme_cqe_t *)ctrl->admin_cq;
    tail = ctrl->admin_sq_tail;
    head = ctrl->admin_cq_head;
    /* Modulo by queue depth so the CID can't wrap into the
     * 16-bit field and alias an in-flight command's id. */
    cid = ctrl->admin_cid_next;
    ctrl->admin_cid_next = (uint16_t)((cid + 1U) % ctrl->admin_sq_entries);

    memset(&sq[tail], 0, sizeof(*sq));
    cmd->cid = cid;
    sq[tail] = *cmd;

    ctrl->admin_sq_tail = (uint16_t)((tail + 1U) % ctrl->admin_sq_entries);
    nvme_mmio_write32(ctrl->mmio, nvme_sq0_doorbell_reg(ctrl), ctrl->admin_sq_tail);
#ifdef HOST_TEST
    nvme_test_admin_kick(ctrl, tail);
#endif

    start_ms = (uint64_t)get_uptime_ms();
    while ((cq[head].status & NVME_CQE_STATUS_PHASE) != ctrl->admin_cq_phase) {
        if (((uint64_t)get_uptime_ms() - start_ms) >= timeout_ms) {
            return -1;
        }
        __asm__ volatile("pause");
    }

    *cqe = cq[head];
    if ((cqe->status & NVME_CQE_STATUS_MASK) != 0 || cqe->cid != cid) {
        return -1;
    }

    memset(&cq[head], 0, sizeof(*cq));
    ctrl->admin_cq_head = (uint16_t)((head + 1U) % ctrl->admin_cq_entries);
    if (ctrl->admin_cq_head == 0) {
        ctrl->admin_cq_phase ^= 1U;
    }
    nvme_mmio_write32(ctrl->mmio, nvme_cq0_doorbell_reg(ctrl), ctrl->admin_cq_head);
    return 0;
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
    ctrl->admin_sq_tail = 0;
    ctrl->admin_cq_head = 0;
    ctrl->admin_cid_next = 0;
    ctrl->admin_cq_phase = 1;
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

int nvme_identify_controller(nvme_controller_t *ctrl) {
    nvme_admin_cmd_t cmd;
    nvme_cqe_t cqe;
    uint8_t *id_data;
    dma_addr_t id_dma;
    uint32_t timeout_ms;
    int rc;

    if (ctrl == NULL || ctrl->mmio == NULL || !ctrl->present) {
        return -1;
    }

    id_data = dma_alloc_coherent(4096U, &id_dma);
    if (id_data == NULL) {
        return -1;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_OP_IDENTIFY;
    cmd.prp1 = (uint64_t)id_dma;
    cmd.cdw10 = NVME_IDENTIFY_CNS_CONTROLLER;

    timeout_ms = ctrl->cap.timeout_ms;
    if (timeout_ms == 0) {
        timeout_ms = 500;
    }

    rc = nvme_admin_submit_sync(ctrl, &cmd, &cqe, timeout_ms);
    if (rc == 0) {
        ctrl->controller_id = (uint16_t)(id_data[78] | ((uint16_t)id_data[79] << 8));
        ctrl->namespace_total = nvme_get_le32(id_data + 516);
        nvme_copy_trim_string(ctrl->serial, sizeof(ctrl->serial), id_data + 4, 20);
        nvme_copy_trim_string(ctrl->model, sizeof(ctrl->model), id_data + 24, 40);
        nvme_copy_trim_string(ctrl->firmware, sizeof(ctrl->firmware), id_data + 64, 8);
        ctrl->identify_valid = 1;
    }

    dma_free_coherent(id_data, 4096U);
    return rc;
}

int nvme_identify_namespaces(nvme_controller_t *ctrl) {
    nvme_admin_cmd_t cmd;
    nvme_cqe_t cqe;
    uint8_t *id_data;
    dma_addr_t id_dma;
    uint32_t timeout_ms;
    uint32_t nsid;
    uint32_t found;

    if (ctrl == NULL || ctrl->mmio == NULL || !ctrl->present || !ctrl->identify_valid) {
        return -1;
    }

    id_data = dma_alloc_coherent(4096U, &id_dma);
    if (id_data == NULL) {
        return -1;
    }

    timeout_ms = ctrl->cap.timeout_ms;
    if (timeout_ms == 0) {
        timeout_ms = 500;
    }

    memset(ctrl->namespaces, 0, sizeof(ctrl->namespaces));
    ctrl->namespace_count = 0;
    found = 0;
    for (nsid = 1; nsid <= ctrl->namespace_total; nsid++) {
        uint64_t nsze;
        uint8_t flbas;
        uint8_t lbaf_index;
        uint8_t lba_shift;
        uint32_t block_size;

        memset(id_data, 0, 4096U);
        memset(&cmd, 0, sizeof(cmd));
        cmd.opcode = NVME_ADMIN_OP_IDENTIFY;
        cmd.nsid = nsid;
        cmd.prp1 = (uint64_t)id_dma;
        cmd.cdw10 = NVME_IDENTIFY_CNS_NAMESPACE;

        if (nvme_admin_submit_sync(ctrl, &cmd, &cqe, timeout_ms) < 0) {
            dma_free_coherent(id_data, 4096U);
            return -1;
        }

        nsze = nvme_get_le64(id_data + 0);
        if (nsze == 0) {
            continue;
        }
        if (found >= NVME_MAX_NAMESPACES) {
            break;
        }

        flbas = id_data[26];
        lbaf_index = (uint8_t)(flbas & 0x0FU);
        lba_shift = id_data[128 + (lbaf_index * 16U) + 2U];
        if (lba_shift >= 32U) {
            block_size = 0;
        } else {
            block_size = 1U << lba_shift;
        }

        ctrl->namespaces[found].nsid = nsid;
        ctrl->namespaces[found].nsze = nsze;
        ctrl->namespaces[found].ncap = nvme_get_le64(id_data + 8);
        ctrl->namespaces[found].nuse = nvme_get_le64(id_data + 16);
        ctrl->namespaces[found].lba_shift = lba_shift;
        ctrl->namespaces[found].block_size = block_size;
        ctrl->namespaces[found].valid = 1;
        found++;
    }

    ctrl->namespace_count = found;
    dma_free_coherent(id_data, 4096U);
    return 0;
}

#ifdef HOST_TEST
extern void nvme_test_io_kick(nvme_controller_t *ctrl, uint16_t sq_tail);
#endif

static int nvme_io_submit_sync(nvme_controller_t *ctrl,
                               nvme_admin_cmd_t *cmd,
                               nvme_cqe_t *cqe,
                               uint32_t timeout_ms) {
    nvme_io_queue_t *q;
    nvme_admin_cmd_t *sq;
    nvme_cqe_t *cq;
    uint16_t tail;
    uint16_t head;
    uint16_t cid;
    uint64_t start_ms;

    if (ctrl == NULL || cmd == NULL || cqe == NULL) {
        return -1;
    }
    q = &ctrl->io_queue;
    if (!ctrl->enabled || !q->valid || q->sq == NULL || q->cq == NULL) {
        return -1;
    }

    sq = (nvme_admin_cmd_t *)q->sq;
    cq = (nvme_cqe_t *)q->cq;
    tail = q->sq_tail;
    head = q->cq_head;
    cid = q->cid_next;
    q->cid_next = (uint16_t)((cid + 1U) % q->sq_entries);

    memset(&sq[tail], 0, sizeof(*sq));
    cmd->cid = cid;
    sq[tail] = *cmd;

    q->sq_tail = (uint16_t)((tail + 1U) % q->sq_entries);
    nvme_mmio_write32(ctrl->mmio, nvme_sq_doorbell_reg(ctrl, q->qid), q->sq_tail);
#ifdef HOST_TEST
    nvme_test_io_kick(ctrl, tail);
#endif

    start_ms = (uint64_t)get_uptime_ms();
    while ((cq[head].status & NVME_CQE_STATUS_PHASE) != q->cq_phase) {
        if (((uint64_t)get_uptime_ms() - start_ms) >= timeout_ms) {
            return -1;
        }
        __asm__ volatile("pause");
    }

    *cqe = cq[head];
    if ((cqe->status & NVME_CQE_STATUS_MASK) != 0 || cqe->cid != cid) {
        return -1;
    }

    memset(&cq[head], 0, sizeof(*cq));
    q->cq_head = (uint16_t)((head + 1U) % q->cq_entries);
    if (q->cq_head == 0) {
        q->cq_phase ^= 1U;
    }
    nvme_mmio_write32(ctrl->mmio, nvme_cq_doorbell_reg(ctrl, q->qid), q->cq_head);
    return 0;
}

int nvme_request_io_queue_count(nvme_controller_t *ctrl, uint16_t requested) {
    nvme_admin_cmd_t cmd;
    nvme_cqe_t cqe;
    uint32_t timeout_ms;
    uint16_t allocated_sq;
    uint16_t allocated_cq;

    if (ctrl == NULL || ctrl->mmio == NULL || !ctrl->present || !ctrl->enabled) {
        return -1;
    }
    if (requested == 0) {
        return -1;
    }

    timeout_ms = ctrl->cap.timeout_ms;
    if (timeout_ms == 0) {
        timeout_ms = 500;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_OP_SET_FEATURES;
    cmd.cdw10 = NVME_FEATURE_NUMBER_OF_QUEUES;
    cmd.cdw11 = (uint32_t)(requested - 1U) |
                ((uint32_t)(requested - 1U) << 16);

    if (nvme_admin_submit_sync(ctrl, &cmd, &cqe, timeout_ms) < 0) {
        return -1;
    }

    /* Result is 0's based: NSQA in [15:0], NCQA in [31:16]. */
    allocated_sq = (uint16_t)((cqe.result & 0xFFFFU) + 1U);
    allocated_cq = (uint16_t)((cqe.result >> 16) + 1U);
    ctrl->io_queue_count_requested = requested;
    ctrl->max_io_queues_alloc = allocated_sq < allocated_cq ?
                                allocated_sq : allocated_cq;
    if (ctrl->max_io_queues_alloc == 0) {
        return -1;
    }
    return 0;
}

int nvme_create_io_queues(nvme_controller_t *ctrl, uint16_t entries) {
    nvme_admin_cmd_t cmd;
    nvme_cqe_t cqe;
    nvme_io_queue_t *q;
    uint32_t timeout_ms;

    if (ctrl == NULL || ctrl->mmio == NULL || !ctrl->present || !ctrl->enabled) {
        return -1;
    }
    if (ctrl->max_io_queues_alloc == 0) {
        return -1;
    }
    if (entries < NVME_IO_QUEUE_MIN_ENTRIES ||
        entries > NVME_IO_QUEUE_MAX_ENTRIES) {
        return -1;
    }
    if (ctrl->cap.mqes != 0 && (uint32_t)entries > (uint32_t)ctrl->cap.mqes + 1U) {
        return -1;
    }

    q = &ctrl->io_queue;
    if (q->valid || q->sq != NULL || q->cq != NULL) {
        return -1;
    }

    timeout_ms = ctrl->cap.timeout_ms;
    if (timeout_ms == 0) {
        timeout_ms = 500;
    }

    q->qid = (uint16_t)NVME_IO_QID;
    q->sq_entries = entries;
    q->cq_entries = entries;
    q->sq_bytes = (size_t)entries * NVME_IO_SQ_ENTRY_SIZE;
    q->cq_bytes = (size_t)entries * NVME_IO_CQ_ENTRY_SIZE;

    q->cq = dma_alloc_coherent(q->cq_bytes, &q->cq_dma);
    if (q->cq == NULL) {
        memset(q, 0, sizeof(*q));
        return -1;
    }

    q->sq = dma_alloc_coherent(q->sq_bytes, &q->sq_dma);
    if (q->sq == NULL) {
        dma_free_coherent(q->cq, q->cq_bytes);
        memset(q, 0, sizeof(*q));
        return -1;
    }

    /* Create I/O Completion Queue first (SQ references it). */
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_OP_CREATE_IO_CQ;
    cmd.prp1 = (uint64_t)q->cq_dma;
    cmd.cdw10 = (uint32_t)q->qid |
                ((uint32_t)(q->cq_entries - 1U) << 16);
    cmd.cdw11 = NVME_QUEUE_FLAG_PC;
    if (nvme_admin_submit_sync(ctrl, &cmd, &cqe, timeout_ms) < 0) {
        dma_free_coherent(q->sq, q->sq_bytes);
        dma_free_coherent(q->cq, q->cq_bytes);
        memset(q, 0, sizeof(*q));
        return -1;
    }

    /* Then create the I/O Submission Queue, bound to the new CQ. */
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_OP_CREATE_IO_SQ;
    cmd.prp1 = (uint64_t)q->sq_dma;
    cmd.cdw10 = (uint32_t)q->qid |
                ((uint32_t)(q->sq_entries - 1U) << 16);
    cmd.cdw11 = NVME_QUEUE_FLAG_PC | ((uint32_t)q->qid << 16);
    if (nvme_admin_submit_sync(ctrl, &cmd, &cqe, timeout_ms) < 0) {
        nvme_admin_cmd_t del;
        memset(&del, 0, sizeof(del));
        del.opcode = NVME_ADMIN_OP_DELETE_IO_CQ;
        del.cdw10 = (uint32_t)q->qid;
        (void)nvme_admin_submit_sync(ctrl, &del, &cqe, timeout_ms);
        dma_free_coherent(q->sq, q->sq_bytes);
        dma_free_coherent(q->cq, q->cq_bytes);
        memset(q, 0, sizeof(*q));
        return -1;
    }

    q->sq_tail = 0;
    q->cq_head = 0;
    q->cid_next = 0;
    q->cq_phase = 1;
    q->valid = 1;
    return 0;
}

int nvme_destroy_io_queues(nvme_controller_t *ctrl) {
    nvme_admin_cmd_t cmd;
    nvme_cqe_t cqe;
    nvme_io_queue_t *q;
    uint32_t timeout_ms;
    int rc = 0;

    if (ctrl == NULL || ctrl->mmio == NULL || !ctrl->present) {
        return -1;
    }
    q = &ctrl->io_queue;
    if (!q->valid) {
        return 0;
    }

    timeout_ms = ctrl->cap.timeout_ms;
    if (timeout_ms == 0) {
        timeout_ms = 500;
    }

    /* Delete SQ before CQ (controller rejects CQ delete with active SQs). */
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_OP_DELETE_IO_SQ;
    cmd.cdw10 = (uint32_t)q->qid;
    if (nvme_admin_submit_sync(ctrl, &cmd, &cqe, timeout_ms) < 0) {
        rc = -1;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_OP_DELETE_IO_CQ;
    cmd.cdw10 = (uint32_t)q->qid;
    if (nvme_admin_submit_sync(ctrl, &cmd, &cqe, timeout_ms) < 0) {
        rc = -1;
    }

    if (q->sq != NULL) {
        dma_free_coherent(q->sq, q->sq_bytes);
    }
    if (q->cq != NULL) {
        dma_free_coherent(q->cq, q->cq_bytes);
    }
    memset(q, 0, sizeof(*q));
    return rc;
}

/*
 * Build a PRP (Physical Region Page) descriptor pair for a contiguous data
 * buffer described by its physical start, byte length, and accompanying CPU
 * pointer.  The caller passes pointers that receive prp1, prp2, and an
 * optional PRP-list page allocated when the transfer spans 3 or more pages.
 *
 * Returns 0 on success, -1 if the transfer cannot be described (e.g. PRP list
 * allocation failed, or the buffer is larger than a single PRP list can
 * cover).  On success, *prp_list_out and *prp_list_dma_out are non-NULL when
 * a list page was allocated; the caller must dma_free_coherent the list page
 * after the I/O completes.
 */
static int nvme_build_prp(dma_addr_t buffer_dma, size_t length,
                          uint64_t *prp1_out, uint64_t *prp2_out,
                          void **prp_list_out, dma_addr_t *prp_list_dma_out,
                          size_t *prp_list_bytes_out) {
    uint64_t base = (uint64_t)buffer_dma;
    uint64_t offset = base & NVME_PAGE_MASK;
    size_t first_len;
    size_t remaining;

    *prp1_out = base;
    *prp2_out = 0;
    *prp_list_out = NULL;
    *prp_list_dma_out = (dma_addr_t)0;
    *prp_list_bytes_out = 0;

    if (length == 0) {
        return 0;
    }

    first_len = (size_t)(NVME_PAGE_SIZE - offset);
    if (length <= first_len) {
        return 0;
    }
    remaining = length - first_len;

    if (remaining <= NVME_PAGE_SIZE) {
        *prp2_out = (base + first_len) & ~(uint64_t)NVME_PAGE_MASK;
        return 0;
    }

    {
        uint64_t list_base = (base + first_len) & ~(uint64_t)NVME_PAGE_MASK;
        size_t list_pages = (remaining + NVME_PAGE_SIZE - 1U) / NVME_PAGE_SIZE;
        uint64_t *list;
        dma_addr_t list_dma;
        size_t i;

        if (list_pages > NVME_PRP_LIST_ENTRIES) {
            return -1;
        }
        list = (uint64_t *)dma_alloc_coherent(NVME_PAGE_SIZE, &list_dma);
        if (list == NULL) {
            return -1;
        }
        memset(list, 0, NVME_PAGE_SIZE);
        for (i = 0; i < list_pages; i++) {
            list[i] = list_base + (uint64_t)(i * NVME_PAGE_SIZE);
        }
        *prp_list_out = list;
        *prp_list_dma_out = list_dma;
        *prp_list_bytes_out = NVME_PAGE_SIZE;
        *prp2_out = (uint64_t)list_dma;
    }
    return 0;
}

static const nvme_namespace_t *nvme_namespace_by_id(const nvme_controller_t *ctrl,
                                                    uint32_t nsid) {
    uint32_t i;

    for (i = 0; i < ctrl->namespace_count; i++) {
        if (ctrl->namespaces[i].valid && ctrl->namespaces[i].nsid == nsid) {
            return &ctrl->namespaces[i];
        }
    }
    return NULL;
}

static int nvme_io_rw(nvme_controller_t *ctrl, uint8_t opcode, uint32_t nsid,
                      uint64_t slba, uint16_t nblocks, void *buffer,
                      size_t buffer_len) {
    const nvme_namespace_t *ns;
    nvme_admin_cmd_t cmd;
    nvme_cqe_t cqe;
    void *bounce;
    dma_addr_t bounce_dma;
    void *prp_list;
    dma_addr_t prp_list_dma;
    size_t prp_list_bytes;
    size_t expected_bytes;
    uint32_t timeout_ms;
    int rc;

    if (ctrl == NULL || buffer == NULL || nblocks == 0) {
        return -1;
    }
    if (!ctrl->enabled || !ctrl->io_queue.valid) {
        return -1;
    }
    ns = nvme_namespace_by_id(ctrl, nsid);
    if (ns == NULL || ns->block_size == 0) {
        return -1;
    }

    expected_bytes = (size_t)nblocks * (size_t)ns->block_size;
    if (buffer_len < expected_bytes) {
        return -1;
    }

    bounce = dma_alloc_coherent(expected_bytes, &bounce_dma);
    if (bounce == NULL) {
        return -1;
    }

    if (opcode == NVME_NVM_OP_WRITE) {
        memcpy(bounce, buffer, expected_bytes);
    } else {
        memset(bounce, 0, expected_bytes);
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = opcode;
    cmd.nsid = nsid;
    cmd.cdw10 = (uint32_t)(slba & 0xFFFFFFFFU);
    cmd.cdw11 = (uint32_t)(slba >> 32);
    cmd.cdw12 = (uint32_t)(nblocks - 1U);

    if (nvme_build_prp(bounce_dma, expected_bytes, &cmd.prp1, &cmd.prp2,
                       &prp_list, &prp_list_dma, &prp_list_bytes) < 0) {
        dma_free_coherent(bounce, expected_bytes);
        return -1;
    }

    timeout_ms = ctrl->cap.timeout_ms;
    if (timeout_ms == 0) {
        timeout_ms = 500;
    }

    rc = nvme_io_submit_sync(ctrl, &cmd, &cqe, timeout_ms);
    if (rc == 0 && opcode == NVME_NVM_OP_READ) {
        memcpy(buffer, bounce, expected_bytes);
    }

    if (prp_list != NULL) {
        dma_free_coherent(prp_list, prp_list_bytes);
    }
    dma_free_coherent(bounce, expected_bytes);
    return rc;
}

int nvme_io_read(nvme_controller_t *ctrl, uint32_t nsid, uint64_t slba,
                 uint16_t nblocks, void *buffer, size_t buffer_len) {
    return nvme_io_rw(ctrl, NVME_NVM_OP_READ, nsid, slba, nblocks, buffer,
                      buffer_len);
}

int nvme_io_write(nvme_controller_t *ctrl, uint32_t nsid, uint64_t slba,
                  uint16_t nblocks, const void *buffer, size_t buffer_len) {
    return nvme_io_rw(ctrl, NVME_NVM_OP_WRITE, nsid, slba, nblocks,
                      (void *)(uintptr_t)buffer, buffer_len);
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
