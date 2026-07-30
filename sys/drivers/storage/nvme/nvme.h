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
#define NVME_MAX_NAMESPACES          16U

#define NVME_IO_SQ_ENTRY_SIZE        64U
#define NVME_IO_CQ_ENTRY_SIZE        16U
#define NVME_IO_QUEUE_MIN_ENTRIES    2U
#define NVME_IO_QUEUE_MAX_ENTRIES    4096U
#define NVME_IO_QUEUE_DEFAULT_ENTRIES 64U
#define NVME_IO_QID                  1U
#define NVME_PAGE_SIZE               4096U
#define NVME_PAGE_MASK               0xFFFU
#define NVME_PRP_LIST_ENTRIES        (NVME_PAGE_SIZE / sizeof(uint64_t))

typedef struct nvme_capability {
    uint16_t mqes;
    uint8_t timeout;
    uint8_t doorbell_stride;
    uint32_t timeout_ms;
    uint32_t doorbell_stride_bytes;
} nvme_capability_t;

typedef struct nvme_namespace {
    uint32_t nsid;
    uint64_t nsze;
    uint64_t ncap;
    uint64_t nuse;
    uint32_t block_size;
    uint8_t lba_shift;
    uint8_t valid;
} nvme_namespace_t;

typedef struct nvme_io_queue {
    uint16_t qid;
    uint16_t sq_entries;
    uint16_t cq_entries;
    void *sq;
    void *cq;
    dma_addr_t sq_dma;
    dma_addr_t cq_dma;
    size_t sq_bytes;
    size_t cq_bytes;
    uint16_t sq_tail;
    uint16_t cq_head;
    uint16_t cid_next;
    uint8_t cq_phase;
    uint8_t valid;
} nvme_io_queue_t;

typedef struct nvme_controller {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    volatile uint8_t *mmio;
    /* NVME-06: size of the BAR0 mapping, so doorbell offsets can be checked
     * against what is actually mapped.  0 means "unknown", in which case the
     * fixed 1 MiB bound alone applies. */
    uint32_t mmio_size;
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
    uint32_t namespace_count;
    uint32_t namespace_total;
    nvme_namespace_t namespaces[NVME_MAX_NAMESPACES];
    char serial[21];
    char model[41];
    char firmware[9];
    uint16_t max_io_queues_alloc;
    uint16_t io_queue_count_requested;
    nvme_io_queue_t io_queue;
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
int nvme_identify_namespaces(nvme_controller_t *ctrl);
int nvme_request_io_queue_count(nvme_controller_t *ctrl, uint16_t requested);
int nvme_create_io_queues(nvme_controller_t *ctrl, uint16_t entries);
int nvme_destroy_io_queues(nvme_controller_t *ctrl);
int nvme_io_read(nvme_controller_t *ctrl, uint32_t nsid, uint64_t slba,
                 uint16_t nblocks, void *buffer, size_t buffer_len);
int nvme_io_write(nvme_controller_t *ctrl, uint32_t nsid, uint64_t slba,
                  uint16_t nblocks, const void *buffer, size_t buffer_len);
size_t nvme_controller_count(void);
const nvme_controller_t *nvme_get_controller(size_t index);

#endif
