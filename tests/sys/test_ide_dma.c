#include <stdint.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <sys/proc.h>
#include <drivers/storage/ide/ide.h>
#include <vm/vm_kmem.h>
#include "tests.h"

/* Volatile counter incremented by background thread */
static volatile uint64_t bg_counter = 0;
static volatile int stop_bg_thread = 0;

/* Background thread function */
static void bg_thread_func(void *arg) {
    (void)arg;
    while (!stop_bg_thread) {
        bg_counter++;
        /* Yield to be nice, but we want to burn CPU if possible to measure availability */
        /* If we don't yield, and we are on same CPU, we might starve if preemptive sched is not aggressive */
        /* Ideally we want to see how much progress this thread makes while IDE thread is waiting */
        /* If IDE thread busy waits, this thread gets NO CPU (if 1 CPU and no preemption) or little CPU */
        /* If IDE thread sleeps, this thread gets ALL CPU. */
        /* sched_yield() here ensures we don't hog CPU if IDE thread is ready */
        sched_yield();
    }

    /* Exit thread logic - loop forever halted as thread_exit is not exposed */
    while (1) {
        sched_yield();
        __asm__ volatile("hlt");
    }
}

void test_ide_dma(void) {
    kprint("\n=== IDE DMA Multitasking Benchmark ===\n");

    /* 1. Allocate buffer */
    uint8_t *buffer = (uint8_t *)kmalloc(4096); // 8 sectors
    if (!buffer) {
        kprint("Failed to allocate buffer\n");
        return;
    }

    /* 2. Reset counters */
    bg_counter = 0;
    stop_bg_thread = 0;

    /* 3. Spawn background thread */
    void *stack = kmalloc(4096);
    if (!stack) {
        kprint("Failed to allocate stack\n");
        kfree(buffer, 4096);
        return;
    }

    /* Stack grows down, so pass top */
    thread_t *t = sched_create_thread(current_process, bg_thread_func, (void*)((uintptr_t)stack + 4096), NULL);
    if (!t) {
        kprint("Failed to create thread\n");
        kfree(buffer, 4096);
        kfree(stack, 4096);
        return;
    }

    /* Mark thread as ready/running - sched_create_thread does this */

    /* 4. Perform IDE DMA Writes */
    /* We'll try Primary Master (0, 0) first. */

    int channel = 0;
    int drive = 0;
    uint64_t lba = 0;
    uint16_t count = 1;
    int iterations = 100;

    kprint("Starting 100 DMA writes (sector 0)...\n");

    for (int i = 0; i < iterations; i++) {
        /* Write dummy data */
        int ret = ide_dma_write(channel, drive, lba, count, buffer);
        if (ret < 0) {
            kprintf("IDE DMA Write failed at iter %d. Is DMA enabled?\n", i);
            /* If failed, abort to avoid hang or misleading results */
            break;
        }
    }

    /* 5. Stop background thread */
    stop_bg_thread = 1;

    /* Give it a moment to stop? */
    sched_yield();

    /* 6. Report */
    kprintf("Background thread increments: %llu\n", (unsigned long long)bg_counter);

    if (bg_counter > 5000) {
        kprint("Result: High concurrency detected (Good!)\n");
    } else {
        kprint("Result: Low concurrency detected (Bad - likely busy waiting)\n");
    }

    /* Cleanup */
    kfree(buffer, 4096);
    /* Stack leaked as thread is halted */
}
