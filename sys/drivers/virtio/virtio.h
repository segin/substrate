#ifndef _VIRTIO_H
#define _VIRTIO_H

#include <stdint.h>
#include <sys/types.h>

// Vendors
#define VIRTIO_VENDOR_ID 0x1AF4

// Device IDs (Legacy/Transitional often use 0x1000 + ID, or just ID in VirtIO header)
// For PCI subsystem:
// Network: 0x1000
// Block:   0x1001
// Console: 0x1003
// 9P:      0x1009
// But the "Subsystem Device ID" is usually the VirtIO Device ID.
// Let's check Device ID. QEMU uses 0x1000 + ID for DeviceID.
#define VIRTIO_PCI_DEVICE_ID_NET  0x1000
#define VIRTIO_PCI_DEVICE_ID_BLK  0x1001
#define VIRTIO_PCI_DEVICE_ID_9P   0x1009

// I/O Register Offsets (Legacy)
#define VIRTIO_REG_HOST_FEATURES  0x00
#define VIRTIO_REG_GUEST_FEATURES 0x04
#define VIRTIO_REG_QUEUE_ADDR     0x08
#define VIRTIO_REG_QUEUE_SIZE     0x0C
#define VIRTIO_REG_QUEUE_SELECT   0x0E
#define VIRTIO_REG_QUEUE_NOTIFY   0x10
#define VIRTIO_REG_DEVICE_STATUS  0x12
#define VIRTIO_REG_ISR_STATUS     0x13

// Status Byte
#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER      2
#define VIRTIO_STATUS_DRIVER_OK   4
#define VIRTIO_STATUS_FAILED      128

// VirtQueue Descriptors
#define VRING_DESC_F_NEXT      1
#define VRING_DESC_F_WRITE     2
#define VRING_DESC_F_INDIRECT  4

struct vring_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} __attribute__((packed));

struct vring_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct vring_used {
    uint16_t flags;
    uint16_t idx;
    struct vring_used_elem ring[];
} __attribute__((packed));

// Helpers
void virtio_init(void);
uint32_t virtio_pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t virtio_get_io_base(uint8_t bus, uint8_t slot, uint8_t func);

// VirtIO Block
void virtio_blk_init(void);

// VirtIO 9P
void virtio_9p_setup(uint8_t bus, uint8_t slot, uint8_t func);

// VirtIO SCSI
void virtio_scsi_setup(uint8_t bus, uint8_t slot, uint8_t func);
void virtio_scsi_poll(void);

#endif
