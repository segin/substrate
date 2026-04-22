#ifndef _KERN_ISAPNP_H
#define _KERN_ISAPNP_H

#include <stddef.h>
#include <stdint.h>

#define ISAPNP_MAX_CARDS            8
#define ISAPNP_MAX_LOGICAL_DEVICES  8
#define ISAPNP_MAX_COMPAT_IDS       4
#define ISAPNP_MAX_IO               8
#define ISAPNP_MAX_IRQ              2
#define ISAPNP_MAX_DMA              2
#define ISAPNP_MAX_MEM              4

#define ISAPNP_VENDOR(a, b, c) \
    (uint16_t)((((((a) - 'A' + 1) & 0x3fU) << 2) | \
                ((((b) - 'A' + 1) & 0x18U) >> 3) | \
                ((((b) - 'A' + 1) & 0x07U) << 13) | \
                ((((c) - 'A' + 1) & 0x1fU) << 8)))

#define ISAPNP_DEVICE(x) \
    (uint16_t)(((((x) & 0xf000U) >> 8) | \
                (((x) & 0x0f00U) >> 8) | \
                (((x) & 0x00f0U) << 8) | \
                (((x) & 0x000fU) << 8)))

#define ISAPNP_EISA_ID(a, b, c, dev) \
    ((((uint32_t)ISAPNP_DEVICE(dev)) << 16) | (uint32_t)ISAPNP_VENDOR(a, b, c))

typedef struct {
    uint16_t min_base;
    uint16_t max_base;
    uint16_t base;
    uint16_t length;
    uint8_t align;
    uint8_t flags;
} isapnp_io_resource_t;

typedef struct {
    uint16_t mask;
    uint8_t irq;
    uint8_t flags;
} isapnp_irq_resource_t;

typedef struct {
    uint8_t mask;
    uint8_t channel;
    uint8_t flags;
} isapnp_dma_resource_t;

typedef struct {
    uint32_t min_base;
    uint32_t max_base;
    uint32_t base;
    uint32_t length;
    uint32_t align;
    uint8_t flags;
} isapnp_mem_resource_t;

typedef struct {
    uint8_t logical_device;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t eisa_id;
    char id[8];
    char name[64];
    uint32_t compat_ids[ISAPNP_MAX_COMPAT_IDS];
    uint8_t compat_count;
    uint8_t active;
    uint8_t flags;
    uint8_t io_count;
    uint8_t irq_count;
    uint8_t dma_count;
    uint8_t mem_count;
    isapnp_io_resource_t io[ISAPNP_MAX_IO];
    isapnp_irq_resource_t irq[ISAPNP_MAX_IRQ];
    isapnp_dma_resource_t dma[ISAPNP_MAX_DMA];
    isapnp_mem_resource_t mem[ISAPNP_MAX_MEM];
} isapnp_logical_device_t;

typedef struct {
    uint8_t csn;
    uint8_t pnp_version;
    uint8_t product_version;
    uint8_t serial_checksum;
    uint32_t serial;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t eisa_id;
    char id[8];
    char name[64];
    uint8_t logical_count;
    isapnp_logical_device_t logical[ISAPNP_MAX_LOGICAL_DEVICES];
} isapnp_device_t;

void isapnp_init(void);
int isapnp_isolate(void);
int isapnp_read_resources(uint8_t csn, isapnp_device_t *dev);
int isapnp_activate(isapnp_device_t *dev);

void isapnp_eisa_id_to_string(uint32_t eisa_id, char out[8]);

#ifdef HOST_TEST
typedef struct {
    uint32_t card_id;
    uint32_t serial;
    const uint8_t *resource_data;
    size_t resource_len;
} isapnp_test_card_t;

void isapnp_test_reset(void);
int isapnp_test_add_card(const isapnp_test_card_t *card);
int isapnp_test_logical_active(uint8_t csn, uint8_t logical_device);
uint16_t isapnp_test_logical_port(uint8_t csn, uint8_t logical_device, unsigned index);
uint8_t isapnp_test_logical_irq(uint8_t csn, uint8_t logical_device, unsigned index);
#endif

#endif