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
static uint8_t mock_mmio[4][64];
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
static void *mock_dma_ptrs[32];
static dma_addr_t mock_dma_addrs[32];
static size_t mock_dma_sizes[32];

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

#include "../../sys/drivers/storage/nvme/nvme.c"

void nvme_test_admin_kick(nvme_controller_t *ctrl, uint16_t sq_tail) {
    nvme_admin_cmd_t *sq;
    nvme_cqe_t *cq;
    nvme_admin_cmd_t *cmd;
    int idx;
    uint8_t *id_buf;

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
    }

    memset(&cq[ctrl->admin_cq_head], 0, sizeof(cq[0]));
    cq[ctrl->admin_cq_head].cid = cmd->cid;
    cq[ctrl->admin_cq_head].sq_id = 0;
    cq[ctrl->admin_cq_head].sq_head = ctrl->admin_sq_tail;
    cq[ctrl->admin_cq_head].status =
        (uint16_t)((admin_status_code[idx] << 1) | ctrl->admin_cq_phase);
    admin_completion_seen[idx]++;
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
    puts("host_test_nvme: PASS");
    return 0;
}
