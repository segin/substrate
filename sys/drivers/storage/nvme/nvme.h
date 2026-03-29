#ifndef _NVME_H
#define _NVME_H

#include <stddef.h>
#include <stdint.h>

#include <sys/dma.h>

#define NVME_MAX_CONTROLLERS 4

#define NVME_REG_CAP   0x0000U
#define NVME_REG_CSTS  0x001cU
#define NVME_REG_CC    0x0014U
#define NVME_REG_AQA   0x0024U
#define NVME_REG_ASQ   0x0028U
#define NVME_REG_ACQ   0x0030U
#define NVME_REG_DBS   0x1000U

#define NVME_CC_EN     (1U << 0)
#define NVME_CSTS_RDY  (1U << 0)
#define NVME_CC_IOSQES_SHIFT 16U
#define NVME_CC_IOCQES_SHIFT 20U

#define NVME_ADMIN_QUEUE_MIN_ENTRIES 2U
#define NVME_ADMIN_QUEUE_MAX_ENTRIES 4096U
#define NVME_ADMIN_SQ_ENTRY_SIZE     64U
#define NVME_ADMIN_CQ_ENTRY_SIZE     16U
#define NVME_ADMIN_SQ_ENTRY_EXP      6U
#define NVME_ADMIN_CQ_ENTRY_EXP      4U

typedef struct nvme_capability {
    uint16_t mqes;
    uint8_t timeout;
    uint8_t doorbell_stride;
    uint32_t timeout_ms;
    uint32_t doorbell_stride_bytes;
} nvme_capability_t;

typedef struct nvme_controller {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    volatile uint8_t *mmio;
    uint64_t cap_raw;
    nvme_capability_t cap;
    uint8_t present;
    uint8_t enabled;
    uint16_t admin_sq_entries;
    uint16_t admin_cq_entries;
    void *admin_sq;
    void *admin_cq;
    dma_addr_t admin_sq_dma;
    dma_addr_t admin_cq_dma;
    size_t admin_sq_bytes;
    size_t admin_cq_bytes;
    uint16_t admin_sq_tail;
    uint16_t admin_cq_head;
    uint16_t admin_cid_next;
    uint8_t admin_cq_phase;
    uint8_t identify_valid;
    uint16_t controller_id;
    char serial[21];
    char model[41];
    char firmware[9];
} nvme_controller_t;

void nvme_init(void);
int nvme_decode_cap(uint64_t cap_raw, nvme_capability_t *cap);
size_t nvme_scan_controllers(nvme_controller_t *controllers, size_t max_controllers);
int nvme_disable_controller(nvme_controller_t *ctrl);
int nvme_configure_admin_queue_attrs(nvme_controller_t *ctrl,
                                     uint16_t sq_entries,
                                     uint16_t cq_entries);
int nvme_create_admin_queues(nvme_controller_t *ctrl);
int nvme_enable_controller(nvme_controller_t *ctrl);
int nvme_identify_controller(nvme_controller_t *ctrl);
size_t nvme_controller_count(void);
const nvme_controller_t *nvme_get_controller(size_t index);

#endif
