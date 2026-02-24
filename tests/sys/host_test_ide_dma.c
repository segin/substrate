#define HOST_TEST

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

/* Forward decls */
void (*intr_wait_hook)(void) = NULL;
void (*sched_sleep_hook)(void *chan) = NULL;

/* Mock Console */
#define _KERN_CONSOLE_STUB_H
#define _KERN_CONSOLE_H
void kprint(const char *s) { printf("%s", s); }
void kprintf(const char *fmt, ...) { }

/* Mock Random */
#define _SYS_RANDOM_H
void random_harvest_fast(void *buf, size_t len) {}

/* Mock Blkdev */
#define _BLKDEV_H
typedef struct blkdev {
    char name[32];
    uint32_t sector_size;
    uint64_t total_sectors;
    void *priv;
    int (*read)(struct blkdev *dev, uint64_t sector, uint32_t count, void *buffer);
    int (*write)(struct blkdev *dev, uint64_t sector, uint32_t count, const void *buffer);
} blkdev_t;
void blkdev_register(blkdev_t *dev) {}

/* Mock Time */
#define _TIME_H_KERN
int64_t get_uptime_ms(void) { return 1000; }

/* Mock IO */
#define _IO_H
static inline void outb(uint16_t port, uint8_t val) {}
static inline uint8_t inb(uint16_t port) { return 0; }
static inline void outw(uint16_t port, uint16_t val) {}
static inline uint16_t inw(uint16_t port) { return 0; }
static inline void outl(uint16_t port, uint32_t val) {}
static inline uint32_t inl(uint16_t port) { return 0; }
static inline void insw(uint16_t port, void *addr, uint32_t count) {}
static inline void outsw(uint16_t port, const void *addr, uint32_t count) {}
static inline void io_wait(void) {}

int intr_disable_count = 0;
int intr_enable_count = 0;
int intr_wait_count = 0;
int cpu_relax_count = 0;

static inline void intr_disable(void) { intr_disable_count++; }
static inline void intr_enable(void) { intr_enable_count++; }
static inline void intr_wait(void) {
    intr_wait_count++;
    if (intr_wait_hook) intr_wait_hook();
}
static inline void cpu_relax(void) { cpu_relax_count++; }

/* Mock Sched */
#define _KERN_SCHED_H
int sched_sleep_count = 0;
void sched_sleep(void *chan) {
    sched_sleep_count++;
    if (sched_sleep_hook) sched_sleep_hook(chan);
}
void sched_wakeup(void *chan) {}
void sched_yield(void) {}

/* Include source, skipping irq handler to avoid inline asm issues */
#define ide_irq_handler unused_ide_irq_handler
#include "../../sys/drivers/storage/ide/ide.c"
#undef ide_irq_handler

// Re-implement clean irq handler
void ide_irq_handler(int irq) {
    uint8_t channel = (irq == 15) ? 1 : 0;
    ide_bm_clear_interrupt(channel);
    ide_irq_complete[channel] = 1;
    sched_wakeup(&ide_irq_complete[channel]);
}

void hook_set_complete(void) {
    if (intr_wait_count >= 5) {
        ide_irq_complete[0] = 1;
        printf("  [Hook] Setting complete flag at wait count %d\n", intr_wait_count);
    }
}

void hook_sleep_complete(void *chan) {
    if (sched_sleep_count >= 5) {
        ide_irq_complete[0] = 1;
        printf("  [Hook] Setting complete flag at sleep count %d\n", sched_sleep_count);
    }
}

int main() {
    printf("=== IDE DMA Host Test ===\n");

    // Setup
    ide_channels[0].dma_capable = 1;
    ide_channels[0].bm_base = 0x1000;
    uint8_t buffer[512];

    // Test: Sched Sleep (Optimized behavior)
    printf("Test: Checking for sched_sleep() usage\n");
    ide_irq_complete[0] = 0;
    sched_sleep_count = 0;
    intr_wait_count = 0;
    intr_disable_count = 0;
    intr_enable_count = 0;

    // Hook sched_sleep to simulate IRQ completion after some sleeps
    sched_sleep_hook = hook_sleep_complete;
    intr_wait_hook = NULL; // Should not be called

    ide_dma_read(0, 0, 0, 1, buffer);

    printf("  sched_sleep called: %d\n", sched_sleep_count);
    printf("  intr_wait called: %d\n", intr_wait_count);
    printf("  intr_disable called: %d\n", intr_disable_count);
    printf("  intr_enable called: %d\n", intr_enable_count);

    if (intr_wait_count > 0) {
        printf("  FAIL: Still using busy wait (intr_wait called).\n");
        return 1;
    }

    if (sched_sleep_count > 0) {
        printf("  PASS: sched_sleep IS used.\n");
    } else {
        printf("  FAIL: sched_sleep NOT used.\n");
        return 1;
    }

    if (intr_disable_count == 0 || intr_enable_count == 0) {
        printf("  FAIL: Interrupts not disabled/enabled around sleep.\n");
        return 1;
    }

    return 0;
}
