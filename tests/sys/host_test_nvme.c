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
        mock_dma_next += (uintptr_t)((size + 0xFFFU) & ~0xFFFU);
    }
    return ptr;
}

void dma_free_coherent(void *cpu_addr, size_t size) {
    (void)size;
    free(cpu_addr);
}

#include "../../sys/drivers/storage/nvme/nvme.c"

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
    puts("host_test_nvme: PASS");
    return 0;
}
