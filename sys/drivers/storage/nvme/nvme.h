#ifndef _NVME_H
#define _NVME_H

#include <stddef.h>
#include <stdint.h>

#define NVME_MAX_CONTROLLERS 4

#define NVME_REG_CAP   0x0000U
#define NVME_REG_CSTS  0x001cU
#define NVME_REG_CC    0x0014U

#define NVME_CC_EN     (1U << 0)
#define NVME_CSTS_RDY  (1U << 0)

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
} nvme_controller_t;

void nvme_init(void);
int nvme_decode_cap(uint64_t cap_raw, nvme_capability_t *cap);
size_t nvme_scan_controllers(nvme_controller_t *controllers, size_t max_controllers);
int nvme_disable_controller(nvme_controller_t *ctrl);
size_t nvme_controller_count(void);
const nvme_controller_t *nvme_get_controller(size_t index);

#endif
