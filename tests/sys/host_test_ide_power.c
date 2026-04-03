#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include <kern/device.h>
#include <kern/driver.h>
#include <sys/proc.h>
#include <sys/irq.h>
#include <arch/i386/pmap.h>
#include <drivers/storage/blkdev.h>
#include <drivers/storage/ide/ide.h>

thread_t *current_thread = NULL;
process_t *current_process = NULL;

static uint8_t mock_io[0x10000];
static uint16_t last_outw_port;
static uint16_t last_outl_port;
static uint32_t last_outl_value;
static uint16_t last_command_port;
static uint8_t last_command_value;
static uint16_t last_device_port;
static uint8_t last_device_value;
static uint16_t last_sec_count_port;
static uint8_t last_sec_count_value;

uint8_t mock_inb(uint16_t port) {
    if ((port & 0x7) == ATA_REG_STATUS) {
        return mock_io[port];
    }
    return mock_io[port];
}

uint16_t mock_inw(uint16_t port) {
    (void)port;
    return 0;
}

uint32_t mock_inl(uint16_t port) {
    (void)port;
    return 0;
}

void mock_outb(uint16_t port, uint8_t value) {
    if ((port & 0x7) == ATA_REG_COMMAND) {
        last_command_port = port;
        last_command_value = value;
        return;
    }
    if ((port & 0x7) == ATA_REG_SEC_COUNT) {
        last_sec_count_port = port;
        last_sec_count_value = value;
    }
    if ((port & 0x7) == ATA_REG_DEVICE) {
        last_device_port = port;
        last_device_value = value;
    }
    mock_io[port] = value;
}

void mock_outw(uint16_t port, uint16_t value) {
    last_outw_port = port;
    (void)value;
}

void mock_outl(uint16_t port, uint32_t value) {
    last_outl_port = port;
    last_outl_value = value;
}

void mock_insw(uint16_t port, void *addr, uint32_t cnt) {
    (void)port;
    (void)addr;
    (void)cnt;
}

void mock_outsw(uint16_t port, const void *addr, uint32_t cnt) {
    (void)port;
    (void)addr;
    (void)cnt;
}

int64_t get_uptime_ms(void) {
    static int64_t now;
    return now++;
}

void sched_yield(void) {}
void kprint(const char *str) { (void)str; }
int kprintf(const char *fmt, ...) {
    (void)fmt;
    return 0;
}
void *pmm_alloc_block(void) { return NULL; }
void pmm_free_block(void *block) { (void)block; }
uintptr_t pmap_extract(pmap_t pmap, uintptr_t va) { (void)pmap; return va; }
pmap_t pmap_kernel(void) { return NULL; }
uint64_t get_ticks(void) { return 0; }
uint32_t get_hz(void) { return 100; }
void *kmalloc(size_t size) { (void)size; return NULL; }
void *kzalloc(size_t size) { (void)size; return NULL; }
void kfree(void *ptr, size_t size) { (void)ptr; (void)size; }
void blkdev_register(blkdev_t *dev) { (void)dev; }
void blkdev_unregister(blkdev_t *dev) { (void)dev; }
void blkdev_scan_partitions(blkdev_t *dev) { (void)dev; }
int irq_install_handler(int irq, irq_handler_t handler) { (void)irq; (void)handler; return 0; }
int request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags,
                const char *name, void *dev) {
    (void)irq;
    (void)handler;
    (void)flags;
    (void)name;
    (void)dev;
    return 0;
}
int irq_register_shared(unsigned int irq, irq_handler_t handler,
                        void *dev_id, const char *name) {
    (void)irq;
    (void)handler;
    (void)dev_id;
    (void)name;
    return 0;
}
void irq_unregister_shared(unsigned int irq, irq_handler_t handler,
                           void *dev_id) {
    (void)irq;
    (void)handler;
    (void)dev_id;
}
int cmdline_debug_enabled(const char *name) { (void)name; return 0; }
int sched_sleep_until(void *chan, uint64_t deadline_tick) {
    (void)chan;
    (void)deadline_tick;
    return 0;
}
void sched_wakeup(void *chan) { (void)chan; }
int device_register(struct device *dev, struct bus_type *bus) {
    (void)dev;
    (void)bus;
    return 0;
}
int device_unregister(struct device *dev) { (void)dev; return 0; }
int driver_register(struct driver *drv, struct bus_type *bus) {
    (void)drv;
    (void)bus;
    return 0;
}
int driver_unregister(struct driver *drv) { (void)drv; return 0; }
void device_runtime_enable(struct device *dev, uint32_t idle_timeout) {
    (void)dev;
    (void)idle_timeout;
}
struct bus_type isa_bus_type;
struct bus_type pci_bus_type;
struct device *isa_first_device(void) { return NULL; }
struct device *isa_next_device(struct device *dev) { (void)dev; return NULL; }
size_t ide_pci_configure_channels(ide_channel_t channels[MAX_IDE_CHANNELS],
                                  uint8_t irq_shared[MAX_IDE_CHANNELS]) {
    (void)channels;
    (void)irq_shared;
    return 0;
}
void ide_parse_identify_data(ide_device_t *dev, const uint16_t *buffer,
                             uint8_t type, uint8_t channel, uint8_t drive) {
    (void)dev;
    (void)buffer;
    (void)type;
    (void)channel;
    (void)drive;
}
int ide_select_dma_transfer_mode(const ide_device_t *dev, uint8_t *mode) {
    (void)dev;
    (void)mode;
    return -1;
}
size_t ide_decode_error(uint8_t error, char *buf, size_t size) {
    (void)error;
    if (buf != NULL && size != 0) {
        buf[0] = '\0';
    }
    return 0;
}
int pci_present(void) { return 0; }
uint64_t i386_cpu_cycle_counter(void) { return 0; }
void random_harvest_fast(const void *data, size_t len) {
    (void)data;
    (void)len;
}

static void reset_state(void) {
    memset(mock_io, 0, sizeof(mock_io));
    last_outw_port = 0;
    last_outl_port = 0;
    last_outl_value = 0;
    last_command_port = 0;
    last_command_value = 0;
    last_device_port = 0;
    last_device_value = 0;
    last_sec_count_port = 0;
    last_sec_count_value = 0;
}

#include "../../sys/drivers/storage/ide/ide.c"

static void test_standby_immediate_primary_master(void) {
    int rc;

    reset_state();
    ide_channels[0].io_base = ATA_PRIMARY_IO;
    ide_channels[0].ctrl_base = ATA_PRIMARY_CTRL;
    mock_io[ATA_PRIMARY_IO + ATA_REG_STATUS] = ATA_SR_DRDY;
    mock_io[ATA_PRIMARY_CTRL + ATA_REG_ALTSTATUS] = ATA_SR_DRDY;

    rc = ide_standby_immediate(ATA_PRIMARY_IO, 0);
    assert(rc == 0);
    assert(last_device_port == ATA_PRIMARY_IO + ATA_REG_DEVICE);
    assert(last_device_value == 0xA0);
    assert(last_command_port == ATA_PRIMARY_IO + ATA_REG_COMMAND);
    assert(last_command_value == ATA_CMD_STANDBY_IMMEDIATE);
}

static void test_idle_immediate_primary_slave(void) {
    int rc;

    reset_state();
    ide_channels[0].io_base = ATA_PRIMARY_IO;
    ide_channels[0].ctrl_base = ATA_PRIMARY_CTRL;
    mock_io[ATA_PRIMARY_IO + ATA_REG_STATUS] = ATA_SR_DRDY;
    mock_io[ATA_PRIMARY_CTRL + ATA_REG_ALTSTATUS] = ATA_SR_DRDY;

    rc = ide_idle_immediate(ATA_PRIMARY_IO, 1);
    assert(rc == 0);
    assert(last_device_port == ATA_PRIMARY_IO + ATA_REG_DEVICE);
    assert(last_device_value == 0xB0);
    assert(last_command_port == ATA_PRIMARY_IO + ATA_REG_COMMAND);
    assert(last_command_value == ATA_CMD_IDLE_IMMEDIATE);
}

static void test_check_power_mode_reads_sector_count(void) {
    int rc;
    uint8_t mode = 0;

    reset_state();
    ide_channels[0].io_base = ATA_PRIMARY_IO;
    ide_channels[0].ctrl_base = ATA_PRIMARY_CTRL;
    mock_io[ATA_PRIMARY_IO + ATA_REG_STATUS] = ATA_SR_DRDY;
    mock_io[ATA_PRIMARY_CTRL + ATA_REG_ALTSTATUS] = ATA_SR_DRDY;
    mock_io[ATA_PRIMARY_IO + ATA_REG_SEC_COUNT] = ATA_POWER_MODE_IDLE;

    rc = ide_check_power_mode(ATA_PRIMARY_IO, 0, &mode);
    assert(rc == 0);
    assert(mode == ATA_POWER_MODE_IDLE);
    assert(last_command_port == ATA_PRIMARY_IO + ATA_REG_COMMAND);
    assert(last_command_value == ATA_CMD_CHECK_POWER_MODE);
}

static void test_check_power_mode_rejects_null_output(void) {
    reset_state();
    assert(ide_check_power_mode(ATA_PRIMARY_IO, 0, NULL) < 0);
}

static void test_configure_spindown_timer_programs_sector_count(void) {
    int rc;

    reset_state();
    ide_channels[0].io_base = ATA_PRIMARY_IO;
    ide_channels[0].ctrl_base = ATA_PRIMARY_CTRL;
    mock_io[ATA_PRIMARY_IO + ATA_REG_STATUS] = ATA_SR_DRDY;
    mock_io[ATA_PRIMARY_CTRL + ATA_REG_ALTSTATUS] = ATA_SR_DRDY;

    rc = ide_configure_spindown_timer(ATA_PRIMARY_IO, 0, 0x12);
    assert(rc == 0);
    assert(last_sec_count_port == ATA_PRIMARY_IO + ATA_REG_SEC_COUNT);
    assert(last_sec_count_value == 0x12);
    assert(last_command_port == ATA_PRIMARY_IO + ATA_REG_COMMAND);
    assert(last_command_value == ATA_CMD_STANDBY);
}

static void test_standby_immediate_rejects_unknown_bus(void) {
    reset_state();
    assert(ide_standby_immediate(0x1234, 0) < 0);
}

static void test_standby_immediate_rejects_absent_device(void) {
    reset_state();
    ide_channels[0].io_base = ATA_PRIMARY_IO;
    ide_channels[0].ctrl_base = ATA_PRIMARY_CTRL;
    mock_io[ATA_PRIMARY_IO + ATA_REG_STATUS] = 0;

    assert(ide_standby_immediate(ATA_PRIMARY_IO, 0) < 0);
}

int main(void) {
    test_standby_immediate_primary_master();
    test_idle_immediate_primary_slave();
    test_check_power_mode_reads_sector_count();
    test_check_power_mode_rejects_null_output();
    test_configure_spindown_timer_programs_sector_count();
    test_standby_immediate_rejects_unknown_bus();
    test_standby_immediate_rejects_absent_device();
    puts("host_test_ide_power: PASS");
    return 0;
}
