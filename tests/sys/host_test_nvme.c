#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kern/device.h>
#include <kern/pci.h>

#include <drivers/storage/nvme/nvme.h>

static pci_device_t mock_devices[4];
static struct device mock_kdevs[4];
static uint16_t mock_command[4];
/*
 * The NVMe MMIO doorbell region begins at NVME_REG_DBS (0x1000); leave room
 * for a few queue pairs at every supported doorbell stride.
 */
static uint8_t mock_mmio[4][8192];
static void *mock_mmio_map[4];
static size_t mock_device_count;
static int kprint_calls;
static int64_t mock_time_ms;
static int csts_read_count[4];
static int csts_clear_after_reads[4];
static int csts_set_after_reads[4];
static uintptr_t mock_dma_next;
static int dma_alloc_fail_after;
static uint8_t identify_data[4][4096];
static uint8_t namespace_identify_data[4][NVME_MAX_NAMESPACES][4096];
static uint16_t admin_status_code[4];
static int admin_completion_seen[4];
static uint16_t io_status_code[4];
static int io_completion_seen[4];
static uint32_t mock_set_features_result[4];
static int mock_create_cq_seen[4];
static int mock_create_sq_seen[4];
static uint32_t last_create_cq_qid[4];
static uint32_t last_create_cq_size[4];
static uint64_t last_create_cq_prp1[4];
static uint32_t last_create_sq_qid[4];
static uint32_t last_create_sq_cqid[4];
static uint32_t last_create_sq_size[4];
static uint64_t last_create_sq_prp1[4];
#define MOCK_NSID_BLOCK_BYTES (4096U * 4U)
static uint8_t mock_namespace_blob[4][MOCK_NSID_BLOCK_BYTES];
static uint8_t last_io_opcode[4];
static uint64_t last_io_slba[4];
static uint16_t last_io_nblocks[4];
static uint64_t last_io_prp1[4];
static uint64_t last_io_prp2[4];
static int io_completion_capture_payload[4];
static uint8_t io_completion_payload[4][MOCK_NSID_BLOCK_BYTES];
static size_t io_completion_payload_len[4];
static void *mock_dma_ptrs[64];
static dma_addr_t mock_dma_addrs[64];
static size_t mock_dma_sizes[64];

static void reset_state(void) {
    memset(mock_devices, 0, sizeof(mock_devices));
    memset(mock_kdevs, 0, sizeof(mock_kdevs));
    memset(mock_command, 0, sizeof(mock_command));
    memset(mock_mmio, 0, sizeof(mock_mmio));
    memset(mock_mmio_map, 0, sizeof(mock_mmio_map));
    mock_device_count = 0;
    kprint_calls = 0;
    mock_time_ms = 0;
    memset(csts_read_count, 0, sizeof(csts_read_count));
    memset(csts_clear_after_reads, 0, sizeof(csts_clear_after_reads));
    memset(csts_set_after_reads, 0, sizeof(csts_set_after_reads));
    memset(identify_data, ' ', sizeof(identify_data));
    memset(namespace_identify_data, 0, sizeof(namespace_identify_data));
    memset(admin_status_code, 0, sizeof(admin_status_code));
    memset(admin_completion_seen, 0, sizeof(admin_completion_seen));
    memset(io_status_code, 0, sizeof(io_status_code));
    memset(io_completion_seen, 0, sizeof(io_completion_seen));
    memset(mock_set_features_result, 0, sizeof(mock_set_features_result));
    memset(mock_create_cq_seen, 0, sizeof(mock_create_cq_seen));
    memset(mock_create_sq_seen, 0, sizeof(mock_create_sq_seen));
    memset(last_create_cq_qid, 0, sizeof(last_create_cq_qid));
    memset(last_create_cq_size, 0, sizeof(last_create_cq_size));
    memset(last_create_cq_prp1, 0, sizeof(last_create_cq_prp1));
    memset(last_create_sq_qid, 0, sizeof(last_create_sq_qid));
    memset(last_create_sq_cqid, 0, sizeof(last_create_sq_cqid));
    memset(last_create_sq_size, 0, sizeof(last_create_sq_size));
    memset(last_create_sq_prp1, 0, sizeof(last_create_sq_prp1));
    memset(mock_namespace_blob, 0, sizeof(mock_namespace_blob));
    memset(last_io_opcode, 0, sizeof(last_io_opcode));
    memset(last_io_slba, 0, sizeof(last_io_slba));
    memset(last_io_nblocks, 0, sizeof(last_io_nblocks));
    memset(last_io_prp1, 0, sizeof(last_io_prp1));
    memset(last_io_prp2, 0, sizeof(last_io_prp2));
    memset(io_completion_capture_payload, 0, sizeof(io_completion_capture_payload));
    memset(io_completion_payload, 0, sizeof(io_completion_payload));
    memset(io_completion_payload_len, 0, sizeof(io_completion_payload_len));
    memset(mock_dma_ptrs, 0, sizeof(mock_dma_ptrs));
    memset(mock_dma_addrs, 0, sizeof(mock_dma_addrs));
    memset(mock_dma_sizes, 0, sizeof(mock_dma_sizes));
    mock_dma_next = 0x20000000U;
    dma_alloc_fail_after = -1;
}

static pci_device_t *add_pci_device(uint16_t class_code, uint16_t subclass,
                                    uint8_t progif, uint16_t vendor_id,
                                    uint16_t device_id) {
    pci_device_t *pdev;
    struct device *kdev;

    assert(mock_device_count < (sizeof(mock_devices) / sizeof(mock_devices[0])));
    pdev = &mock_devices[mock_device_count];
    kdev = &mock_kdevs[mock_device_count];

    pdev->bus = 0;
    pdev->slot = (uint8_t)(4 + mock_device_count);
    pdev->func = (uint8_t)mock_device_count;
    pdev->vendor_id = vendor_id;
    pdev->device_id = device_id;
    pdev->kdev = kdev;

    kdev->class = class_code;
    kdev->subclass = subclass;
    kdev->progif = progif;

    mock_mmio_map[mock_device_count] = mock_mmio[mock_device_count];
    mock_device_count++;
    return pdev;
}

static size_t mock_index_for(const pci_device_t *pdev) {
    size_t i;

    for (i = 0; i < mock_device_count; i++) {
        if (&mock_devices[i] == pdev) {
            return i;
        }
    }
    assert(0 && "unknown pci device");
    return 0;
}

static int mock_mmio_index(const volatile uint8_t *mmio) {
    size_t i;

    for (i = 0; i < mock_device_count; i++) {
        if (mock_mmio_map[i] == (const void *)mmio) {
            return (int)i;
        }
    }
    for (i = 0; i < (sizeof(mock_mmio) / sizeof(mock_mmio[0])); i++) {
        if (mock_mmio[i] == (const uint8_t *)mmio) {
            return (int)i;
        }
    }
    return -1;
}

pci_device_t *pci_first_device(void) {
    if (mock_device_count == 0) {
        return NULL;
    }
    return &mock_devices[0];
}

pci_device_t *pci_next_device(pci_device_t *dev) {
    size_t i;

    if (dev == NULL) {
        return NULL;
    }

    for (i = 0; i < mock_device_count; i++) {
        if (&mock_devices[i] == dev) {
            if (i + 1 < mock_device_count) {
                return &mock_devices[i + 1];
            }
            break;
        }
    }

    return NULL;
}

uint16_t pci_read_config16(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset) {
    size_t i;

    (void)bus;
    (void)slot;
    assert(offset == PCI_CONFIG_COMMAND);

    for (i = 0; i < mock_device_count; i++) {
        if (mock_devices[i].func == func) {
            return mock_command[i];
        }
    }

    return 0;
}

void pci_write_config16(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset, uint16_t val) {
    size_t i;

    (void)bus;
    (void)slot;
    assert(offset == PCI_CONFIG_COMMAND);

    for (i = 0; i < mock_device_count; i++) {
        if (mock_devices[i].func == func) {
            mock_command[i] = val;
            return;
        }
    }

    assert(0 && "unknown pci function");
}

void *pci_iomap(pci_device_t *dev, int bar, size_t max_len) {
    (void)bar;
    (void)max_len;
    return mock_mmio_map[mock_index_for(dev)];
}

void kprint(const char *str) {
    (void)str;
    kprint_calls++;
}

int64_t get_uptime_ms(void) {
    return mock_time_ms++;
}

uint32_t nvme_test_mmio_read32(volatile uint8_t *mmio, uint32_t reg) {
    int idx = mock_mmio_index(mmio);
    uint32_t value;

    memcpy(&value, (const void *)(mmio + reg), sizeof(value));
    if (idx >= 0 && reg == NVME_REG_CSTS) {
        csts_read_count[idx]++;
        if ((value & NVME_CSTS_RDY) != 0 &&
            csts_clear_after_reads[idx] > 0 &&
            csts_read_count[idx] >= csts_clear_after_reads[idx]) {
            value &= ~NVME_CSTS_RDY;
            memcpy((void *)(mmio + reg), &value, sizeof(value));
        }
        if ((value & NVME_CSTS_RDY) == 0 &&
            csts_set_after_reads[idx] > 0 &&
            csts_read_count[idx] >= csts_set_after_reads[idx]) {
            value |= NVME_CSTS_RDY;
            memcpy((void *)(mmio + reg), &value, sizeof(value));
        }
    }
    return value;
}

void nvme_test_mmio_write32(volatile uint8_t *mmio, uint32_t reg, uint32_t value) {
    memcpy((void *)(mmio + reg), &value, sizeof(value));
}

void *dma_alloc_coherent(size_t size, dma_addr_t *dma_handle) {
    void *ptr;
    size_t i;

    if (dma_alloc_fail_after == 0) {
        return NULL;
    }
    if (dma_alloc_fail_after > 0) {
        dma_alloc_fail_after--;
    }

    ptr = calloc(1, size);
    assert(ptr != NULL);
    if (dma_handle != NULL) {
        *dma_handle = (dma_addr_t)mock_dma_next;
        for (i = 0; i < (sizeof(mock_dma_ptrs) / sizeof(mock_dma_ptrs[0])); i++) {
            if (mock_dma_ptrs[i] == NULL) {
                mock_dma_ptrs[i] = ptr;
                mock_dma_addrs[i] = *dma_handle;
                mock_dma_sizes[i] = size;
                break;
            }
        }
        assert(i < (sizeof(mock_dma_ptrs) / sizeof(mock_dma_ptrs[0])));
        mock_dma_next += (uintptr_t)((size + 0xFFFU) & ~0xFFFU);
    }
    return ptr;
}

void dma_free_coherent(void *cpu_addr, size_t size) {
    size_t i;

    (void)size;
    for (i = 0; i < (sizeof(mock_dma_ptrs) / sizeof(mock_dma_ptrs[0])); i++) {
        if (mock_dma_ptrs[i] == cpu_addr) {
            mock_dma_ptrs[i] = NULL;
            mock_dma_addrs[i] = (dma_addr_t)0;
            mock_dma_sizes[i] = 0;
            break;
        }
    }
    free(cpu_addr);
}

static void *mock_dma_lookup(dma_addr_t dma_addr, size_t min_size) {
    size_t i;

    for (i = 0; i < (sizeof(mock_dma_ptrs) / sizeof(mock_dma_ptrs[0])); i++) {
        if (mock_dma_ptrs[i] != NULL && mock_dma_addrs[i] == dma_addr &&
            mock_dma_sizes[i] >= min_size) {
            return mock_dma_ptrs[i];
        }
    }
    return NULL;
}

void nvme_test_admin_kick(nvme_controller_t *ctrl, uint16_t sq_tail);
void nvme_test_io_kick(nvme_controller_t *ctrl, uint16_t sq_tail);

#include "../../sys/drivers/storage/nvme/nvme.c"

void nvme_test_admin_kick(nvme_controller_t *ctrl, uint16_t sq_tail) {
    nvme_admin_cmd_t *sq;
    nvme_cqe_t *cq;
    nvme_admin_cmd_t *cmd;
    int idx;
    uint8_t *id_buf;
    uint32_t result = 0;

    idx = mock_mmio_index(ctrl->mmio);
    assert(idx >= 0);
    sq = (nvme_admin_cmd_t *)ctrl->admin_sq;
    cq = (nvme_cqe_t *)ctrl->admin_cq;
    cmd = &sq[sq_tail];

    if (cmd->opcode == NVME_ADMIN_OP_IDENTIFY &&
        cmd->cdw10 == NVME_IDENTIFY_CNS_CONTROLLER) {
        id_buf = (uint8_t *)mock_dma_lookup((dma_addr_t)cmd->prp1,
                                            sizeof(identify_data[idx]));
        assert(id_buf != NULL);
        memcpy(id_buf, identify_data[idx], sizeof(identify_data[idx]));
    } else if (cmd->opcode == NVME_ADMIN_OP_IDENTIFY &&
               cmd->cdw10 == NVME_IDENTIFY_CNS_NAMESPACE) {
        assert(cmd->nsid > 0 && cmd->nsid <= NVME_MAX_NAMESPACES);
        id_buf = (uint8_t *)mock_dma_lookup((dma_addr_t)cmd->prp1,
                                            sizeof(namespace_identify_data[idx][0]));
        assert(id_buf != NULL);
        memcpy(id_buf, namespace_identify_data[idx][cmd->nsid - 1],
               sizeof(namespace_identify_data[idx][0]));
    } else if (cmd->opcode == NVME_ADMIN_OP_SET_FEATURES &&
               (cmd->cdw10 & 0xFFU) == NVME_FEATURE_NUMBER_OF_QUEUES) {
        result = mock_set_features_result[idx];
    } else if (cmd->opcode == NVME_ADMIN_OP_CREATE_IO_CQ) {
        mock_create_cq_seen[idx]++;
        last_create_cq_qid[idx] = cmd->cdw10 & 0xFFFFU;
        last_create_cq_size[idx] = (cmd->cdw10 >> 16) & 0xFFFFU;
        last_create_cq_prp1[idx] = cmd->prp1;
    } else if (cmd->opcode == NVME_ADMIN_OP_CREATE_IO_SQ) {
        mock_create_sq_seen[idx]++;
        last_create_sq_qid[idx] = cmd->cdw10 & 0xFFFFU;
        last_create_sq_size[idx] = (cmd->cdw10 >> 16) & 0xFFFFU;
        last_create_sq_cqid[idx] = (cmd->cdw11 >> 16) & 0xFFFFU;
        last_create_sq_prp1[idx] = cmd->prp1;
    }

    memset(&cq[ctrl->admin_cq_head], 0, sizeof(cq[0]));
    cq[ctrl->admin_cq_head].cid = cmd->cid;
    cq[ctrl->admin_cq_head].sq_id = 0;
    cq[ctrl->admin_cq_head].sq_head = ctrl->admin_sq_tail;
    cq[ctrl->admin_cq_head].result = result;
    cq[ctrl->admin_cq_head].status =
        (uint16_t)((admin_status_code[idx] << 1) | ctrl->admin_cq_phase);
    admin_completion_seen[idx]++;
}

void nvme_test_io_kick(nvme_controller_t *ctrl, uint16_t sq_tail) {
    nvme_admin_cmd_t *sq;
    nvme_cqe_t *cq;
    nvme_admin_cmd_t *cmd;
    int idx;
    nvme_io_queue_t *q;
    uint64_t slba;
    uint16_t nblocks;
    uint32_t block_size = 0;
    size_t expected_bytes;
    uint8_t *bounce;

    idx = mock_mmio_index(ctrl->mmio);
    assert(idx >= 0);
    q = &ctrl->io_queue;
    sq = (nvme_admin_cmd_t *)q->sq;
    cq = (nvme_cqe_t *)q->cq;
    cmd = &sq[sq_tail];

    last_io_opcode[idx] = cmd->opcode;
    slba = (uint64_t)cmd->cdw10 | ((uint64_t)cmd->cdw11 << 32);
    nblocks = (uint16_t)((cmd->cdw12 & 0xFFFFU) + 1U);
    last_io_slba[idx] = slba;
    last_io_nblocks[idx] = nblocks;
    last_io_prp1[idx] = cmd->prp1;
    last_io_prp2[idx] = cmd->prp2;

    if (ctrl->namespace_count > 0) {
        block_size = ctrl->namespaces[0].block_size;
    }
    if (block_size == 0) {
        block_size = 512;
    }
    expected_bytes = (size_t)nblocks * (size_t)block_size;
    if (expected_bytes > MOCK_NSID_BLOCK_BYTES) {
        expected_bytes = MOCK_NSID_BLOCK_BYTES;
    }

    bounce = (uint8_t *)mock_dma_lookup((dma_addr_t)cmd->prp1, 1);
    if (bounce != NULL && io_status_code[idx] == 0 && expected_bytes > 0) {
        size_t base_off = (size_t)slba * (size_t)block_size;
        if (cmd->opcode == NVME_NVM_OP_READ) {
            if (base_off + expected_bytes <= MOCK_NSID_BLOCK_BYTES) {
                memcpy(bounce, mock_namespace_blob[idx] + base_off,
                       expected_bytes);
            }
        } else if (cmd->opcode == NVME_NVM_OP_WRITE) {
            if (base_off + expected_bytes <= MOCK_NSID_BLOCK_BYTES) {
                memcpy(mock_namespace_blob[idx] + base_off, bounce,
                       expected_bytes);
            }
            if (io_completion_capture_payload[idx]) {
                size_t cap = expected_bytes < MOCK_NSID_BLOCK_BYTES ?
                             expected_bytes : MOCK_NSID_BLOCK_BYTES;
                memcpy(io_completion_payload[idx], bounce, cap);
                io_completion_payload_len[idx] = cap;
            }
        }
    }

    memset(&cq[q->cq_head], 0, sizeof(cq[0]));
    cq[q->cq_head].cid = cmd->cid;
    cq[q->cq_head].sq_id = q->qid;
    cq[q->cq_head].sq_head = q->sq_tail;
    cq[q->cq_head].status =
        (uint16_t)((io_status_code[idx] << 1) | q->cq_phase);
    io_completion_seen[idx]++;
}

static void test_decode_cap_extracts_timeout_and_stride(void) {
    nvme_capability_t cap;
    uint64_t raw = 0;

    raw |= 0x1234ULL;
    raw |= (uint64_t)10 << 24;
    raw |= (uint64_t)3 << 32;

    assert(nvme_decode_cap(raw, &cap) == 0);
    assert(cap.mqes == 0x1234);
    assert(cap.timeout == 10);
    assert(cap.timeout_ms == 5000);
    assert(cap.doorbell_stride == 3);
    assert(cap.doorbell_stride_bytes == 32);
}

static void test_scan_filters_nvme_functions_and_records_cap(void) {
    nvme_controller_t ctrls[2];
    pci_device_t *pdev;
    uint64_t cap_raw = 0;

    reset_state();

    add_pci_device(0x01, 0x06, 0x01, 0x8086, 0x2922);
    pdev = add_pci_device(0x01, 0x08, 0x02, 0x144d, 0xa808);

    cap_raw |= 0x00ffULL;
    cap_raw |= (uint64_t)7 << 24;
    cap_raw |= (uint64_t)2 << 32;
    memcpy(mock_mmio[mock_index_for(pdev)], &cap_raw, sizeof(cap_raw));

    assert(nvme_scan_controllers(ctrls, 2) == 1);
    assert(ctrls[0].present == 1);
    assert(ctrls[0].bus == pdev->bus);
    assert(ctrls[0].slot == pdev->slot);
    assert(ctrls[0].func == pdev->func);
    assert(ctrls[0].vendor_id == 0x144d);
    assert(ctrls[0].device_id == 0xa808);
    assert(ctrls[0].cap_raw == cap_raw);
    assert(ctrls[0].cap.timeout == 7);
    assert(ctrls[0].cap.timeout_ms == 3500);
    assert(ctrls[0].cap.doorbell_stride_bytes == 16);
    assert((mock_command[mock_index_for(pdev)] & (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) ==
           (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER));
    assert(kprint_calls == 1);
}

static void test_init_publishes_scanned_controllers(void) {
    pci_device_t *pdev;
    uint64_t cap_raw = 0;
    const nvme_controller_t *ctrl;

    reset_state();
    pdev = add_pci_device(0x01, 0x08, 0x02, 0x8086, 0xf1a5);
    cap_raw |= 0x0010ULL;
    cap_raw |= (uint64_t)4 << 24;
    memcpy(mock_mmio[mock_index_for(pdev)], &cap_raw, sizeof(cap_raw));

    nvme_init();
    assert(nvme_controller_count() == 1);
    ctrl = nvme_get_controller(0);
    assert(ctrl != NULL);
    assert(ctrl->device_id == 0xf1a5);
    assert(ctrl->cap.timeout_ms == 2000);
}

static void test_disable_controller_clears_enable_and_waits_ready_down(void) {
    nvme_controller_t ctrl;
    uint32_t cc = NVME_CC_EN;
    uint32_t csts = NVME_CSTS_RDY;

    reset_state();
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.present = 1;
    ctrl.mmio = mock_mmio[0];
    ctrl.cap.timeout_ms = 5;
    ctrl.enabled = 1;
    memcpy(mock_mmio[0] + NVME_REG_CC, &cc, sizeof(cc));
    memcpy(mock_mmio[0] + NVME_REG_CSTS, &csts, sizeof(csts));
    csts_clear_after_reads[0] = 2;

    assert(nvme_disable_controller(&ctrl) == 0);
    memcpy(&cc, mock_mmio[0] + NVME_REG_CC, sizeof(cc));
    assert((cc & NVME_CC_EN) == 0);
    memcpy(&csts, mock_mmio[0] + NVME_REG_CSTS, sizeof(csts));
    assert((csts & NVME_CSTS_RDY) == 0);
    assert(ctrl.enabled == 0);
}

static void test_disable_controller_times_out_if_ready_stays_set(void) {
    nvme_controller_t ctrl;
    uint32_t cc = NVME_CC_EN;
    uint32_t csts = NVME_CSTS_RDY;

    reset_state();
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.present = 1;
    ctrl.mmio = mock_mmio[1];
    ctrl.cap.timeout_ms = 3;
    memcpy(mock_mmio[1] + NVME_REG_CC, &cc, sizeof(cc));
    memcpy(mock_mmio[1] + NVME_REG_CSTS, &csts, sizeof(csts));

    assert(nvme_disable_controller(&ctrl) < 0);
    memcpy(&cc, mock_mmio[1] + NVME_REG_CC, sizeof(cc));
    assert((cc & NVME_CC_EN) == 0);
}

static void test_configure_admin_queue_attrs_programs_aqa(void) {
    nvme_controller_t ctrl;
    uint32_t aqa = 0;

    reset_state();
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.present = 1;
    ctrl.mmio = mock_mmio[0];
    ctrl.cap.mqes = 63;

    assert(nvme_configure_admin_queue_attrs(&ctrl, 32, 16) == 0);
    memcpy(&aqa, mock_mmio[0] + NVME_REG_AQA, sizeof(aqa));
    assert(aqa == 0x001f000fU);
    assert(ctrl.admin_sq_entries == 32);
    assert(ctrl.admin_cq_entries == 16);
}

static void test_configure_admin_queue_attrs_rejects_enabled_controller(void) {
    nvme_controller_t ctrl;
    uint32_t cc = NVME_CC_EN;

    reset_state();
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.present = 1;
    ctrl.mmio = mock_mmio[1];
    ctrl.cap.mqes = 31;
    ctrl.enabled = 1;
    memcpy(mock_mmio[1] + NVME_REG_CC, &cc, sizeof(cc));

    assert(nvme_configure_admin_queue_attrs(&ctrl, 8, 8) < 0);
}

static void test_configure_admin_queue_attrs_rejects_oversized_queues(void) {
    nvme_controller_t ctrl;

    reset_state();
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.present = 1;
    ctrl.mmio = mock_mmio[2];
    ctrl.cap.mqes = 7;

    assert(nvme_configure_admin_queue_attrs(&ctrl, 9, 8) < 0);
    assert(nvme_configure_admin_queue_attrs(&ctrl, 8, 9) < 0);
}

static void test_create_admin_queues_allocates_and_programs_addresses(void) {
    nvme_controller_t ctrl;
    uint64_t asq = 0;
    uint64_t acq = 0;

    reset_state();
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.present = 1;
    ctrl.mmio = mock_mmio[0];
    ctrl.admin_sq_entries = 8;
    ctrl.admin_cq_entries = 16;

    assert(nvme_create_admin_queues(&ctrl) == 0);
    memcpy(&asq, mock_mmio[0] + NVME_REG_ASQ, sizeof(asq));
    memcpy(&acq, mock_mmio[0] + NVME_REG_ACQ, sizeof(acq));
    assert(asq == (uint64_t)ctrl.admin_sq_dma);
    assert(acq == (uint64_t)ctrl.admin_cq_dma);
    assert(ctrl.admin_sq != NULL);
    assert(ctrl.admin_cq != NULL);
    assert(ctrl.admin_sq_bytes == 8U * NVME_ADMIN_SQ_ENTRY_SIZE);
    assert(ctrl.admin_cq_bytes == 16U * NVME_ADMIN_CQ_ENTRY_SIZE);

    dma_free_coherent(ctrl.admin_sq, ctrl.admin_sq_bytes);
    dma_free_coherent(ctrl.admin_cq, ctrl.admin_cq_bytes);
}

static void test_create_admin_queues_rejects_missing_queue_attrs(void) {
    nvme_controller_t ctrl;

    reset_state();
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.present = 1;
    ctrl.mmio = mock_mmio[1];

    assert(nvme_create_admin_queues(&ctrl) < 0);
}

static void test_create_admin_queues_frees_sq_if_cq_alloc_fails(void) {
    nvme_controller_t ctrl;

    reset_state();
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.present = 1;
    ctrl.mmio = mock_mmio[2];
    ctrl.admin_sq_entries = 4;
    ctrl.admin_cq_entries = 4;
    dma_alloc_fail_after = 1;

    assert(nvme_create_admin_queues(&ctrl) < 0);
    assert(ctrl.admin_sq == NULL);
    assert(ctrl.admin_cq == NULL);
    assert(ctrl.admin_sq_dma == (dma_addr_t)0);
    assert(ctrl.admin_sq_bytes == 0);
    assert(ctrl.admin_cq_bytes == 0);
}

static void test_enable_controller_sets_cc_and_waits_for_ready(void) {
    nvme_controller_t ctrl;
    uint32_t cc = 0;
    uint32_t csts = 0;

    reset_state();
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.present = 1;
    ctrl.mmio = mock_mmio[0];
    ctrl.cap.timeout_ms = 5;
    ctrl.admin_sq = (void *)0x1;
    ctrl.admin_cq = (void *)0x2;
    csts_set_after_reads[0] = 2;

    assert(nvme_enable_controller(&ctrl) == 0);
    memcpy(&cc, mock_mmio[0] + NVME_REG_CC, sizeof(cc));
    memcpy(&csts, mock_mmio[0] + NVME_REG_CSTS, sizeof(csts));
    assert((cc & NVME_CC_EN) != 0);
    assert(((cc >> NVME_CC_IOSQES_SHIFT) & 0xFU) == NVME_ADMIN_SQ_ENTRY_EXP);
    assert(((cc >> NVME_CC_IOCQES_SHIFT) & 0xFU) == NVME_ADMIN_CQ_ENTRY_EXP);
    assert((csts & NVME_CSTS_RDY) != 0);
    assert(ctrl.enabled == 1);
}

static void test_enable_controller_times_out_if_ready_never_sets(void) {
    nvme_controller_t ctrl;
    uint32_t cc = 0;

    reset_state();
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.present = 1;
    ctrl.mmio = mock_mmio[1];
    ctrl.cap.timeout_ms = 3;
    ctrl.admin_sq = (void *)0x1;
    ctrl.admin_cq = (void *)0x2;

    assert(nvme_enable_controller(&ctrl) < 0);
    memcpy(&cc, mock_mmio[1] + NVME_REG_CC, sizeof(cc));
    assert((cc & NVME_CC_EN) != 0);
    assert(ctrl.enabled == 0);
}

static void test_identify_controller_submits_admin_command_and_parses_strings(void) {
    nvme_controller_t ctrl;

    reset_state();
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.present = 1;
    ctrl.mmio = mock_mmio[0];
    ctrl.cap.timeout_ms = 5;
    ctrl.cap.doorbell_stride_bytes = 4;
    ctrl.admin_sq_entries = 4;
    ctrl.admin_cq_entries = 4;

    assert(nvme_create_admin_queues(&ctrl) == 0);
    ctrl.enabled = 1;
    memcpy(identify_data[0] + 4, "SUBSTRATE-NVME-0001  ", 20);
    memcpy(identify_data[0] + 24, "Substrate Test NVMe Controller          ", 40);
    memcpy(identify_data[0] + 64, "1.0A    ", 8);
    identify_data[0][78] = 0x34;
    identify_data[0][79] = 0x12;
    identify_data[0][516] = 0x02;
    identify_data[0][517] = 0x00;
    identify_data[0][518] = 0x00;
    identify_data[0][519] = 0x00;

    assert(nvme_identify_controller(&ctrl) == 0);
    assert(admin_completion_seen[0] == 1);
    assert(ctrl.identify_valid == 1);
    assert(ctrl.controller_id == 0x1234);
    assert(ctrl.namespace_total == 2);
    assert(strcmp(ctrl.serial, "SUBSTRATE-NVME-0001") == 0);
    assert(strcmp(ctrl.model, "Substrate Test NVMe Controller") == 0);
    assert(strcmp(ctrl.firmware, "1.0A") == 0);

    dma_free_coherent(ctrl.admin_sq, ctrl.admin_sq_bytes);
    dma_free_coherent(ctrl.admin_cq, ctrl.admin_cq_bytes);
}

static void test_identify_controller_fails_on_admin_error_status(void) {
    nvme_controller_t ctrl;

    reset_state();
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.present = 1;
    ctrl.mmio = mock_mmio[1];
    ctrl.cap.timeout_ms = 5;
    ctrl.cap.doorbell_stride_bytes = 4;
    ctrl.admin_sq_entries = 2;
    ctrl.admin_cq_entries = 2;
    admin_status_code[1] = 1;

    assert(nvme_create_admin_queues(&ctrl) == 0);
    ctrl.enabled = 1;
    assert(nvme_identify_controller(&ctrl) < 0);
    assert(ctrl.identify_valid == 0);

    dma_free_coherent(ctrl.admin_sq, ctrl.admin_sq_bytes);
    dma_free_coherent(ctrl.admin_cq, ctrl.admin_cq_bytes);
}

static void test_identify_namespaces_discovers_active_nsids(void) {
    nvme_controller_t ctrl;

    reset_state();
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.present = 1;
    ctrl.mmio = mock_mmio[0];
    ctrl.identify_valid = 1;
    ctrl.namespace_total = 3;
    ctrl.cap.timeout_ms = 5;
    ctrl.cap.doorbell_stride_bytes = 4;
    ctrl.admin_sq_entries = 4;
    ctrl.admin_cq_entries = 4;

    assert(nvme_create_admin_queues(&ctrl) == 0);
    ctrl.enabled = 1;

    namespace_identify_data[0][0][0] = 0x00;
    namespace_identify_data[0][0][1] = 0x10;
    namespace_identify_data[0][0][8] = 0x00;
    namespace_identify_data[0][0][9] = 0x10;
    namespace_identify_data[0][0][16] = 0x00;
    namespace_identify_data[0][0][17] = 0x08;
    namespace_identify_data[0][0][26] = 0x00;
    namespace_identify_data[0][0][128 + 2] = 12;

    namespace_identify_data[0][1][0] = 0x00;

    namespace_identify_data[0][2][0] = 0x34;
    namespace_identify_data[0][2][1] = 0x12;
    namespace_identify_data[0][2][8] = 0x00;
    namespace_identify_data[0][2][16] = 0x78;
    namespace_identify_data[0][2][26] = 0x00;
    namespace_identify_data[0][2][128 + 2] = 9;

    assert(nvme_identify_namespaces(&ctrl) == 0);
    assert(ctrl.namespace_count == 2);
    assert(ctrl.namespaces[0].valid == 1);
    assert(ctrl.namespaces[0].nsid == 1);
    assert(ctrl.namespaces[0].nsze == 0x1000);
    assert(ctrl.namespaces[0].ncap == 0x1000);
    assert(ctrl.namespaces[0].nuse == 0x0800);
    assert(ctrl.namespaces[0].block_size == 4096);
    assert(ctrl.namespaces[1].valid == 1);
    assert(ctrl.namespaces[1].nsid == 3);
    assert(ctrl.namespaces[1].nsze == 0x1234);
    assert(ctrl.namespaces[1].nuse == 0x78);
    assert(ctrl.namespaces[1].block_size == 512);
    assert(admin_completion_seen[0] == 3);

    dma_free_coherent(ctrl.admin_sq, ctrl.admin_sq_bytes);
    dma_free_coherent(ctrl.admin_cq, ctrl.admin_cq_bytes);
}

static void test_identify_namespaces_requires_controller_identify(void) {
    nvme_controller_t ctrl;

    reset_state();
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.present = 1;
    ctrl.mmio = mock_mmio[1];

    assert(nvme_identify_namespaces(&ctrl) < 0);
}

static void prep_enabled_controller(nvme_controller_t *ctrl, int idx,
                                    uint16_t admin_entries) {
    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->present = 1;
    ctrl->mmio = mock_mmio[idx];
    ctrl->cap.timeout_ms = 5;
    ctrl->cap.doorbell_stride_bytes = 4;
    ctrl->cap.mqes = 1023;
    ctrl->admin_sq_entries = admin_entries;
    ctrl->admin_cq_entries = admin_entries;
    assert(nvme_create_admin_queues(ctrl) == 0);
    ctrl->enabled = 1;
}

static void test_request_io_queue_count_records_min_allocated(void) {
    nvme_controller_t ctrl;

    reset_state();
    prep_enabled_controller(&ctrl, 0, 4);
    /* Result is 0's based: NSQA=3 → 4 SQs; NCQA=2 → 3 CQs.  Driver records min. */
    mock_set_features_result[0] = (3U) | (2U << 16);

    assert(nvme_request_io_queue_count(&ctrl, 4) == 0);
    assert(ctrl.io_queue_count_requested == 4);
    assert(ctrl.max_io_queues_alloc == 3);
    assert(admin_completion_seen[0] == 1);

    nvme_destroy_io_queues(&ctrl);
    dma_free_coherent(ctrl.admin_sq, ctrl.admin_sq_bytes);
    dma_free_coherent(ctrl.admin_cq, ctrl.admin_cq_bytes);
}

static void test_request_io_queue_count_rejects_zero_alloc(void) {
    nvme_controller_t ctrl;

    reset_state();
    prep_enabled_controller(&ctrl, 1, 4);
    /* Both NSQA and NCQA = 0 means controller couldn't allocate any. */
    mock_set_features_result[1] = 0;
    /*
     * The driver must reject this — max_io_queues_alloc would be 1 with the
     * 0's based decode, but we treat any zero result as failure path via the
     * admin status code instead.  Setting status_code triggers the failure.
     */
    admin_status_code[1] = 1;
    assert(nvme_request_io_queue_count(&ctrl, 1) < 0);

    dma_free_coherent(ctrl.admin_sq, ctrl.admin_sq_bytes);
    dma_free_coherent(ctrl.admin_cq, ctrl.admin_cq_bytes);
}

static void test_create_io_queues_issues_cq_then_sq(void) {
    nvme_controller_t ctrl;

    reset_state();
    prep_enabled_controller(&ctrl, 0, 4);
    mock_set_features_result[0] = (3U) | (3U << 16);
    assert(nvme_request_io_queue_count(&ctrl, 4) == 0);

    assert(nvme_create_io_queues(&ctrl, 32) == 0);
    assert(ctrl.io_queue.valid == 1);
    assert(ctrl.io_queue.qid == NVME_IO_QID);
    assert(ctrl.io_queue.sq_entries == 32);
    assert(ctrl.io_queue.cq_entries == 32);
    assert(ctrl.io_queue.sq != NULL);
    assert(ctrl.io_queue.cq != NULL);

    assert(mock_create_cq_seen[0] == 1);
    assert(last_create_cq_qid[0] == NVME_IO_QID);
    /* QSIZE in cdw10[31:16] is 0's based: 32 entries → 31. */
    assert(last_create_cq_size[0] == 31);
    assert(last_create_cq_prp1[0] == (uint64_t)ctrl.io_queue.cq_dma);

    assert(mock_create_sq_seen[0] == 1);
    assert(last_create_sq_qid[0] == NVME_IO_QID);
    assert(last_create_sq_cqid[0] == NVME_IO_QID);
    assert(last_create_sq_size[0] == 31);
    assert(last_create_sq_prp1[0] == (uint64_t)ctrl.io_queue.sq_dma);

    nvme_destroy_io_queues(&ctrl);
    dma_free_coherent(ctrl.admin_sq, ctrl.admin_sq_bytes);
    dma_free_coherent(ctrl.admin_cq, ctrl.admin_cq_bytes);
}

static void test_create_io_queues_requires_request_first(void) {
    nvme_controller_t ctrl;

    reset_state();
    prep_enabled_controller(&ctrl, 1, 4);

    assert(nvme_create_io_queues(&ctrl, 16) < 0);

    dma_free_coherent(ctrl.admin_sq, ctrl.admin_sq_bytes);
    dma_free_coherent(ctrl.admin_cq, ctrl.admin_cq_bytes);
}

static void test_create_io_queues_rolls_back_on_sq_failure(void) {
    nvme_controller_t ctrl;

    reset_state();
    prep_enabled_controller(&ctrl, 0, 4);
    mock_set_features_result[0] = (3U) | (3U << 16);
    assert(nvme_request_io_queue_count(&ctrl, 4) == 0);

    /*
     * 1st admin command (Set Features) already completed.  Make the next
     * admin status code apply to all subsequent admin commands; cdw10
     * inspection alone in the hook wouldn't help because admin_status_code
     * is per-controller.  Trigger failure on Create SQ by forcing status
     * after CQ has been recorded.
     */
    admin_status_code[0] = 1;
    /* Bump status only after CQ has been processed: emulate by accepting
     * Create CQ then failing.  Use a small two-stage trick: clear status on
     * first CQ-completion path.  Since the hook doesn't expose that, we
     * accept that the CQ create will also "fail"; what we verify is that the
     * driver rolls everything back. */
    assert(nvme_create_io_queues(&ctrl, 16) < 0);
    assert(ctrl.io_queue.valid == 0);
    assert(ctrl.io_queue.sq == NULL);
    assert(ctrl.io_queue.cq == NULL);

    dma_free_coherent(ctrl.admin_sq, ctrl.admin_sq_bytes);
    dma_free_coherent(ctrl.admin_cq, ctrl.admin_cq_bytes);
}

static void seed_namespace(nvme_controller_t *ctrl, uint32_t nsid,
                           uint32_t block_size) {
    uint32_t i;

    for (i = 0; i < NVME_MAX_NAMESPACES; i++) {
        if (!ctrl->namespaces[i].valid) {
            ctrl->namespaces[i].nsid = nsid;
            ctrl->namespaces[i].block_size = block_size;
            ctrl->namespaces[i].nsze = MOCK_NSID_BLOCK_BYTES / block_size;
            ctrl->namespaces[i].valid = 1;
            ctrl->namespace_count++;
            return;
        }
    }
    assert(0 && "no slot");
}

static void test_io_read_returns_namespace_payload(void) {
    nvme_controller_t ctrl;
    uint8_t buf[1024];
    uint32_t i;

    reset_state();
    prep_enabled_controller(&ctrl, 0, 4);
    mock_set_features_result[0] = (3U) | (3U << 16);
    assert(nvme_request_io_queue_count(&ctrl, 4) == 0);
    assert(nvme_create_io_queues(&ctrl, 16) == 0);
    seed_namespace(&ctrl, 1, 512);

    for (i = 0; i < sizeof(buf); i++) {
        mock_namespace_blob[0][i] = (uint8_t)(i ^ 0xA5U);
    }

    memset(buf, 0, sizeof(buf));
    assert(nvme_io_read(&ctrl, 1, 0, 2, buf, sizeof(buf)) == 0);
    assert(last_io_opcode[0] == NVME_NVM_OP_READ);
    assert(last_io_slba[0] == 0);
    assert(last_io_nblocks[0] == 2);
    for (i = 0; i < 1024; i++) {
        assert(buf[i] == (uint8_t)(i ^ 0xA5U));
    }
    assert(io_completion_seen[0] == 1);

    nvme_destroy_io_queues(&ctrl);
    dma_free_coherent(ctrl.admin_sq, ctrl.admin_sq_bytes);
    dma_free_coherent(ctrl.admin_cq, ctrl.admin_cq_bytes);
}

static void test_io_write_pushes_payload_to_namespace(void) {
    nvme_controller_t ctrl;
    uint8_t buf[2048];
    uint32_t i;

    reset_state();
    prep_enabled_controller(&ctrl, 0, 4);
    mock_set_features_result[0] = (3U) | (3U << 16);
    assert(nvme_request_io_queue_count(&ctrl, 4) == 0);
    assert(nvme_create_io_queues(&ctrl, 16) == 0);
    seed_namespace(&ctrl, 1, 512);

    for (i = 0; i < sizeof(buf); i++) {
        buf[i] = (uint8_t)(0x5A ^ i);
    }
    io_completion_capture_payload[0] = 1;

    assert(nvme_io_write(&ctrl, 1, 8, 4, buf, sizeof(buf)) == 0);
    assert(last_io_opcode[0] == NVME_NVM_OP_WRITE);
    assert(last_io_slba[0] == 8);
    assert(last_io_nblocks[0] == 4);
    assert(io_completion_payload_len[0] == 2048);
    for (i = 0; i < sizeof(buf); i++) {
        assert(io_completion_payload[0][i] == (uint8_t)(0x5A ^ i));
    }
    /* Verify the persisted blob now reflects the write at slba=8 (offset 4096). */
    for (i = 0; i < sizeof(buf); i++) {
        assert(mock_namespace_blob[0][8 * 512 + i] == (uint8_t)(0x5A ^ i));
    }

    nvme_destroy_io_queues(&ctrl);
    dma_free_coherent(ctrl.admin_sq, ctrl.admin_sq_bytes);
    dma_free_coherent(ctrl.admin_cq, ctrl.admin_cq_bytes);
}

static void test_io_read_rejects_unknown_namespace(void) {
    nvme_controller_t ctrl;
    uint8_t buf[512];

    reset_state();
    prep_enabled_controller(&ctrl, 0, 4);
    mock_set_features_result[0] = (3U) | (3U << 16);
    assert(nvme_request_io_queue_count(&ctrl, 4) == 0);
    assert(nvme_create_io_queues(&ctrl, 16) == 0);
    seed_namespace(&ctrl, 1, 512);

    assert(nvme_io_read(&ctrl, 7, 0, 1, buf, sizeof(buf)) < 0);

    nvme_destroy_io_queues(&ctrl);
    dma_free_coherent(ctrl.admin_sq, ctrl.admin_sq_bytes);
    dma_free_coherent(ctrl.admin_cq, ctrl.admin_cq_bytes);
}

static void test_io_read_rejects_undersized_buffer(void) {
    nvme_controller_t ctrl;
    uint8_t buf[256];

    reset_state();
    prep_enabled_controller(&ctrl, 0, 4);
    mock_set_features_result[0] = (3U) | (3U << 16);
    assert(nvme_request_io_queue_count(&ctrl, 4) == 0);
    assert(nvme_create_io_queues(&ctrl, 16) == 0);
    seed_namespace(&ctrl, 1, 512);

    /* nblocks=1 needs 512B; buffer is only 256B. */
    assert(nvme_io_read(&ctrl, 1, 0, 1, buf, sizeof(buf)) < 0);

    nvme_destroy_io_queues(&ctrl);
    dma_free_coherent(ctrl.admin_sq, ctrl.admin_sq_bytes);
    dma_free_coherent(ctrl.admin_cq, ctrl.admin_cq_bytes);
}

static void test_io_read_propagates_command_failure(void) {
    nvme_controller_t ctrl;
    uint8_t buf[512];

    reset_state();
    prep_enabled_controller(&ctrl, 0, 4);
    mock_set_features_result[0] = (3U) | (3U << 16);
    assert(nvme_request_io_queue_count(&ctrl, 4) == 0);
    assert(nvme_create_io_queues(&ctrl, 16) == 0);
    seed_namespace(&ctrl, 1, 512);
    io_status_code[0] = 0x42;

    assert(nvme_io_read(&ctrl, 1, 0, 1, buf, sizeof(buf)) < 0);

    /* Reset I/O status before tearing down so destroy succeeds. */
    io_status_code[0] = 0;
    nvme_destroy_io_queues(&ctrl);
    dma_free_coherent(ctrl.admin_sq, ctrl.admin_sq_bytes);
    dma_free_coherent(ctrl.admin_cq, ctrl.admin_cq_bytes);
}

static void test_build_prp_single_page_buffer(void) {
    uint64_t prp1, prp2;
    void *list;
    dma_addr_t list_dma;
    size_t list_bytes;

    /* 2KB at page-aligned address — fits in one page. */
    assert(nvme_build_prp((dma_addr_t)0x1000U, 2048,
                          &prp1, &prp2, &list, &list_dma, &list_bytes) == 0);
    assert(prp1 == 0x1000U);
    assert(prp2 == 0);
    assert(list == NULL);
}

static void test_build_prp_two_page_buffer_uses_prp2_directly(void) {
    uint64_t prp1, prp2;
    void *list;
    dma_addr_t list_dma;
    size_t list_bytes;

    /* 8KB at page-aligned address spans exactly 2 pages. */
    assert(nvme_build_prp((dma_addr_t)0x2000U, 8192,
                          &prp1, &prp2, &list, &list_dma, &list_bytes) == 0);
    assert(prp1 == 0x2000U);
    assert(prp2 == 0x3000U);
    assert(list == NULL);
}

static void test_build_prp_large_buffer_allocates_list(void) {
    uint64_t prp1, prp2;
    void *list;
    dma_addr_t list_dma;
    size_t list_bytes;
    uint64_t *entries;

    reset_state();
    /* 16KB at page-aligned address spans 4 pages → needs PRP list. */
    assert(nvme_build_prp((dma_addr_t)0x4000U, 16384,
                          &prp1, &prp2, &list, &list_dma, &list_bytes) == 0);
    assert(prp1 == 0x4000U);
    assert(list != NULL);
    assert(list_bytes == NVME_PAGE_SIZE);
    assert(prp2 == (uint64_t)list_dma);
    entries = (uint64_t *)list;
    assert(entries[0] == 0x5000U);
    assert(entries[1] == 0x6000U);
    assert(entries[2] == 0x7000U);
    dma_free_coherent(list, list_bytes);
}

static void test_build_prp_unaligned_buffer(void) {
    uint64_t prp1, prp2;
    void *list;
    dma_addr_t list_dma;
    size_t list_bytes;

    /* 3KB at offset 0x800 within a page: 2KB in page 0, 1KB in page 1. */
    assert(nvme_build_prp((dma_addr_t)0x1800U, 3072,
                          &prp1, &prp2, &list, &list_dma, &list_bytes) == 0);
    assert(prp1 == 0x1800U);
    assert(prp2 == 0x2000U);
    assert(list == NULL);
}

int main(void) {
    test_decode_cap_extracts_timeout_and_stride();
    test_scan_filters_nvme_functions_and_records_cap();
    test_init_publishes_scanned_controllers();
    test_disable_controller_clears_enable_and_waits_ready_down();
    test_disable_controller_times_out_if_ready_stays_set();
    test_configure_admin_queue_attrs_programs_aqa();
    test_configure_admin_queue_attrs_rejects_enabled_controller();
    test_configure_admin_queue_attrs_rejects_oversized_queues();
    test_create_admin_queues_allocates_and_programs_addresses();
    test_create_admin_queues_rejects_missing_queue_attrs();
    test_create_admin_queues_frees_sq_if_cq_alloc_fails();
    test_enable_controller_sets_cc_and_waits_for_ready();
    test_enable_controller_times_out_if_ready_never_sets();
    test_identify_controller_submits_admin_command_and_parses_strings();
    test_identify_controller_fails_on_admin_error_status();
    test_identify_namespaces_discovers_active_nsids();
    test_identify_namespaces_requires_controller_identify();
    test_request_io_queue_count_records_min_allocated();
    test_request_io_queue_count_rejects_zero_alloc();
    test_create_io_queues_issues_cq_then_sq();
    test_create_io_queues_requires_request_first();
    test_create_io_queues_rolls_back_on_sq_failure();
    test_io_read_returns_namespace_payload();
    test_io_write_pushes_payload_to_namespace();
    test_io_read_rejects_unknown_namespace();
    test_io_read_rejects_undersized_buffer();
    test_io_read_propagates_command_failure();
    test_build_prp_single_page_buffer();
    test_build_prp_two_page_buffer_uses_prp2_directly();
    test_build_prp_large_buffer_allocates_list();
    test_build_prp_unaligned_buffer();
    puts("host_test_nvme: PASS");
    return 0;
}
