#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define _IO_H
#define HOST_TEST 1

#include "../../sys/drivers/storage/scsi/scsi.h"
#include "../../sys/drivers/virtio/virtio.h"

static uint16_t fake_io_base = 0x1000;
static uint16_t selected_queue;
static uint16_t queue_sizes[8];
static uint32_t config32[0x40];
static uint16_t config16[0x40];
static uint32_t queue_pfns[8];
static uint16_t last_notified_queue;
static int scan_bus_calls;
static uint8_t last_scan_bus;
static int register_link_calls;
static scsi_link_t *registered_link;
static int mock_cpu_id;
static int next_page_idx;
static uint8_t page_pool[16][4096] __attribute__((aligned(4096)));
static uint8_t read_data_buf[128];
static uint8_t write_data_buf[32];
static int auto_complete_req;
static uint8_t mock_req_response;
static uint8_t mock_req_status;
static uint32_t mock_req_residual;
static uint32_t mock_req_sense_len;
static uint8_t mock_req_sense[8];

static void outb(uint16_t port, uint8_t val) {
    (void)port;
    (void)val;
}

static uint8_t __attribute__((unused)) inb(uint16_t port) {
    (void)port;
    return 0;
}

static void outw(uint16_t port, uint16_t val);
static uint16_t inw(uint16_t port);
static void outl(uint16_t port, uint32_t val);
static uint32_t inl(uint16_t port);

void kprint(const char *str) {
    (void)str;
}

uint16_t virtio_get_io_base(uint8_t bus, uint8_t slot, uint8_t func) {
    (void)bus;
    (void)slot;
    (void)func;
    return fake_io_base;
}

void *pmm_alloc_block(void) {
    if (next_page_idx >= (int)(sizeof(page_pool) / sizeof(page_pool[0]))) {
        return NULL;
    }

    memset(page_pool[next_page_idx], 0, sizeof(page_pool[next_page_idx]));
    return page_pool[next_page_idx++];
}

void *pmm_alloc_contiguous(size_t pages) {
    void *base;

    if (pages == 0 || next_page_idx + (int)pages > (int)(sizeof(page_pool) / sizeof(page_pool[0]))) {
        return NULL;
    }

    base = page_pool[next_page_idx];
    memset(base, 0, pages * 4096U);
    next_page_idx += (int)pages;
    return base;
}

void pmm_free_block(void *block) {
    (void)block;
}

void pmm_free_contiguous(void *block, size_t pages) {
    (void)block;
    (void)pages;
}

int scsi_register_link(scsi_link_t *link) {
    register_link_calls++;
    registered_link = link;
    return 0;
}

int scsi_scan_bus(scsi_link_t *link, uint8_t bus) {
    (void)link;
    scan_bus_calls++;
    last_scan_bus = bus;
    return 0;
}

int percpu_get_cpu_id(void) {
    return mock_cpu_id;
}

#include "../../sys/drivers/virtio/virtio_scsi.c"

static void outw(uint16_t port, uint16_t val) {
    uint16_t offset = port - fake_io_base;

    if (offset == VIRTIO_REG_QUEUE_SELECT) {
        selected_queue = val;
        return;
    }

    if (offset == VIRTIO_REG_QUEUE_NOTIFY) {
        last_notified_queue = val;

        if (val >= 2 && auto_complete_req) {
            uint16_t queue_index = (uint16_t)(val - 2);
            virtio_scsi_queue_t *queue = vscsi_get_req_queue(&vscsi_dev, queue_index);
            uint16_t head;
            uint16_t resp_idx;

            assert(queue != NULL);
            head = queue->avail->ring[(uint16_t)(queue->avail->idx - 1) % queue->size];
            resp_idx = head;

            while (queue->desc[resp_idx].flags & VRING_DESC_F_NEXT) {
                resp_idx = queue->desc[resp_idx].next;
            }

            struct virtio_scsi_resp_hdr *resp =
                (struct virtio_scsi_resp_hdr *)(uintptr_t)queue->desc[resp_idx].addr;

            memset(resp, 0, sizeof(*resp));
            resp->response = mock_req_response;
            resp->status = mock_req_status;
            resp->residual = mock_req_residual;
            resp->sense_len = mock_req_sense_len;
            memcpy(resp->sense, mock_req_sense,
                   mock_req_sense_len > sizeof(mock_req_sense) ? sizeof(mock_req_sense) : mock_req_sense_len);

            queue->used->ring[queue->used->idx % queue->size].id = head;
            queue->used->idx++;
        }
    }
}

static uint16_t inw(uint16_t port) {
    uint16_t offset = port - fake_io_base;

    if (offset == VIRTIO_REG_QUEUE_SIZE) {
        return queue_sizes[selected_queue];
    }

    return config16[offset];
}

static void outl(uint16_t port, uint32_t val) {
    uint16_t offset = port - fake_io_base;

    if (offset == VIRTIO_REG_QUEUE_ADDR && selected_queue < (uint16_t)(sizeof(queue_pfns) / sizeof(queue_pfns[0]))) {
        queue_pfns[selected_queue] = val;
    }
}

static uint32_t inl(uint16_t port) {
    uint16_t offset = port - fake_io_base;
    return config32[offset];
}

static void setup_ring(virtio_scsi_queue_t *queue, void *page, uint16_t size) {
    uint32_t avail_end = 16 * size + 6 + 2 * size;
    uint32_t used_offset = (avail_end + 4095) & ~4095;

    memset(page, 0, 4096);
    queue->size = size;
    queue->mem = page;
    queue->desc = (struct vring_desc *)page;
    queue->avail = (struct vring_avail *)((uint8_t *)page + 16 * size);
    queue->used = (struct vring_used *)((uint8_t *)page + used_offset);
    queue->last_used_idx = 0;
}

static void reset_state(void) {
    memset(&vscsi_dev, 0, sizeof(vscsi_dev));
    vscsi_initialized = 0;
    memset(queue_sizes, 0, sizeof(queue_sizes));
    memset(config32, 0, sizeof(config32));
    memset(config16, 0, sizeof(config16));
    memset(queue_pfns, 0, sizeof(queue_pfns));
    memset(page_pool, 0, sizeof(page_pool));
    selected_queue = 0;
    last_notified_queue = 0xffff;
    scan_bus_calls = 0;
    last_scan_bus = 0xff;
    register_link_calls = 0;
    registered_link = NULL;
    mock_cpu_id = 0;
    next_page_idx = 0;
    auto_complete_req = 0;
    mock_req_response = VIRTIO_SCSI_S_OK;
    mock_req_status = SCSI_STATUS_GOOD;
    mock_req_residual = 0;
    mock_req_sense_len = 0;
    memset(mock_req_sense, 0, sizeof(mock_req_sense));

    queue_sizes[0] = 8;
    queue_sizes[1] = 8;
    queue_sizes[2] = 8;
    queue_sizes[3] = 8;
    config32[VIRTIO_REG_HOST_FEATURES] = VIRTIO_SCSI_F_HOTPLUG;
    config32[VIRTIO_SCSI_REG_NUM_QUEUES] = 1;
    config32[VIRTIO_SCSI_REG_SEG_MAX] = 8;
    config32[VIRTIO_SCSI_REG_MAX_SECTORS] = 128;
    config32[VIRTIO_SCSI_REG_CMD_PER_LUN] = 4;
    config32[VIRTIO_SCSI_REG_SENSE_SIZE] = 96;
    config32[VIRTIO_SCSI_REG_CDB_SIZE] = 32;
    config32[VIRTIO_SCSI_REG_MAX_LUN] = 7;
    config16[VIRTIO_SCSI_REG_MAX_CHANNEL] = 0;
    config16[VIRTIO_SCSI_REG_MAX_TARGET] = 15;
}

static void test_execute_builds_dma_descriptor_chain(void) {
    scsi_device_t sdev;
    scsi_request_t req;
    struct virtio_scsi_req_hdr *hdr;

    reset_state();
    setup_ring(&vscsi_dev.req_queue, pmm_alloc_contiguous(2), 8);
    vscsi_dev.req_buf = page_pool[next_page_idx++];
    vscsi_dev.io_base = fake_io_base;
    vscsi_dev.link.priv = &vscsi_dev;

    auto_complete_req = 1;
    mock_req_response = VIRTIO_SCSI_S_OK;
    mock_req_status = SCSI_STATUS_GOOD;
    mock_req_residual = 16;
    mock_req_sense_len = 2;
    mock_req_sense[0] = 0x70;
    mock_req_sense[1] = 0x05;

    memset(&sdev, 0, sizeof(sdev));
    sdev.target = 5;
    sdev.lun = 2;

    memset(&req, 0, sizeof(req));
    memset(read_data_buf, 0, sizeof(read_data_buf));
    req.device = &sdev;
    req.data = read_data_buf;
    req.data_len = sizeof(read_data_buf);
    req.flags = SCSI_REQ_READ;
    req.cdb_len = 16;
    for (int i = 0; i < 16; i++) {
        req.cdb[i] = (uint8_t)(0x20 + i);
    }

    assert(vscsi_execute(&vscsi_dev.link, &req) == 0);
    assert(last_notified_queue == 2);
    assert(req.status == SCSI_STATUS_GOOD);
    assert(req.data_xfer == sizeof(read_data_buf) - mock_req_residual);
    assert(req.sense_len == mock_req_sense_len);
    assert(req.sense[0] == 0x70);

    hdr = (struct virtio_scsi_req_hdr *)vscsi_dev.req_buf;
    assert(hdr->lun[0] == 1);
    assert(hdr->lun[1] == sdev.target);
    assert(hdr->lun[2] == 0);
    assert(hdr->lun[3] == sdev.lun);
    assert(memcmp(hdr->cdb, req.cdb, req.cdb_len) == 0);
    assert(vscsi_dev.req_queue.desc[0].addr == (uint32_t)(uintptr_t)hdr);
    assert(vscsi_dev.req_queue.desc[1].addr == (uint32_t)(uintptr_t)read_data_buf);
    assert((vscsi_dev.req_queue.desc[1].flags & VRING_DESC_F_WRITE) != 0);
    assert((vscsi_dev.req_queue.desc[2].flags & VRING_DESC_F_WRITE) != 0);
}

static void test_execute_marks_write_payload_read_only_to_device(void) {
    scsi_device_t sdev;
    scsi_request_t req;

    reset_state();
    setup_ring(&vscsi_dev.req_queue, pmm_alloc_contiguous(2), 8);
    vscsi_dev.req_buf = page_pool[next_page_idx++];
    vscsi_dev.io_base = fake_io_base;
    vscsi_dev.link.priv = &vscsi_dev;
    auto_complete_req = 1;

    memset(&sdev, 0, sizeof(sdev));
    sdev.target = 1;

    memset(&req, 0, sizeof(req));
    req.device = &sdev;
    memset(write_data_buf, 0, sizeof(write_data_buf));
    req.data = write_data_buf;
    req.data_len = sizeof(write_data_buf);
    req.flags = SCSI_REQ_WRITE;
    req.cdb_len = 10;

    assert(vscsi_execute(&vscsi_dev.link, &req) == 0);
    assert((vscsi_dev.req_queue.desc[1].flags & VRING_DESC_F_WRITE) == 0);
}

static void test_event_buffers_rescan_and_requeue(void) {
    reset_state();
    setup_ring(&vscsi_dev.event_queue, pmm_alloc_contiguous(2), 8);
    vscsi_dev.event_bufs = (struct virtio_scsi_event *)page_pool[next_page_idx++];
    vscsi_dev.io_base = fake_io_base;
    vscsi_dev.link.bus_id = 3;

    vscsi_setup_event_buffers(&vscsi_dev);
    assert(last_notified_queue == 1);
    assert(vscsi_dev.event_queue.avail->idx == VIRTIO_SCSI_EVENT_SLOTS);
    assert(vscsi_dev.event_queue.desc[0].addr == (uint32_t)(uintptr_t)&vscsi_dev.event_bufs[0]);
    assert((vscsi_dev.event_queue.desc[0].flags & VRING_DESC_F_WRITE) != 0);

    vscsi_dev.event_bufs[1].event = VIRTIO_SCSI_T_TRANSPORT_RESET;
    vscsi_dev.event_queue.used->ring[0].id = 1;
    vscsi_dev.event_queue.used->idx = 1;

    vscsi_process_events(&vscsi_dev);
    assert(scan_bus_calls == 1);
    assert(last_scan_bus == 3);
    assert(vscsi_dev.event_queue.last_used_idx == 1);
    assert(vscsi_dev.event_queue.avail->ring[VIRTIO_SCSI_EVENT_SLOTS] == 1);
    assert(vscsi_dev.event_queue.avail->idx == VIRTIO_SCSI_EVENT_SLOTS + 1);
}

static void test_setup_initializes_scsi_link_and_queues(void) {
    reset_state();

    virtio_scsi_setup(0, 1, 0);

    assert(vscsi_initialized == 1);
    assert(register_link_calls == 1);
    assert(registered_link == &vscsi_dev.link);
    assert(vscsi_dev.req_queue_count == 1);
    assert(strcmp(vscsi_dev.link.name, "virtio-scsi0") == 0);
    assert(vscsi_dev.link.bus_id == 0);
    assert(vscsi_dev.link.max_targets == 16);
    assert(vscsi_dev.link.max_luns == 8);
    assert(vscsi_dev.link.adapter_queue_depth == 4);
    assert((vscsi_dev.link.flags & SCSI_LINK_DMA) != 0);
    assert(queue_pfns[0] != 0);
    assert(queue_pfns[1] != 0);
    assert(queue_pfns[2] != 0);
    assert(scan_bus_calls == 1);
    assert(last_scan_bus == 0);
    assert(vscsi_dev.event_queue.avail->idx == VIRTIO_SCSI_EVENT_SLOTS);
}

static void test_multi_queue_uses_cpu_selected_request_queue(void) {
    scsi_device_t sdev;
    scsi_request_t req;

    reset_state();
    config32[VIRTIO_SCSI_REG_NUM_QUEUES] = 2;

    virtio_scsi_setup(0, 1, 0);

    assert(vscsi_initialized == 1);
    assert(vscsi_dev.req_queue_count == 2);
    assert(queue_pfns[3] != 0);

    mock_cpu_id = 1;
    auto_complete_req = 1;
    memset(&sdev, 0, sizeof(sdev));
    memset(&req, 0, sizeof(req));
    memset(read_data_buf, 0, sizeof(read_data_buf));
    sdev.target = 2;
    req.device = &sdev;
    req.data = read_data_buf;
    req.data_len = sizeof(read_data_buf);
    req.flags = SCSI_REQ_READ;
    req.cdb_len = 6;
    req.cdb[0] = SCSI_CMD_INQUIRY;

    assert(vscsi_execute(&vscsi_dev.link, &req) == 0);
    assert(last_notified_queue == 3);
}

int main(void) {
    test_execute_builds_dma_descriptor_chain();
    test_execute_marks_write_payload_read_only_to_device();
    test_event_buffers_rescan_and_requeue();
    test_setup_initializes_scsi_link_and_queues();
    test_multi_queue_uses_cpu_selected_request_queue();
    puts("host_test_virtio_scsi: PASS");
    return 0;
}