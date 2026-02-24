#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <stdatomic.h>
#include <sched.h>

// Mocks
#define ATA_SR_BSY 0x80
#define ATA_REG_STATUS 0x07

volatile uint8_t mock_status = ATA_SR_BSY;

uint8_t ide_read_reg(uint8_t channel, uint8_t reg) {
    // Simulate I/O delay ~1us
    for(volatile int k=0; k<500; k++);
    return mock_status;
}

uint64_t get_uptime_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void kprint(const char *msg) {
    // printf("%s", msg); // Avoid spamming
}

// Original implementation
static void ide_wait_bsy_original(uint8_t channel) {
    while (ide_read_reg(channel, ATA_REG_STATUS) & ATA_SR_BSY) {
        __asm__ volatile("pause");
    }
}

// Optimized implementation
static void ide_wait_bsy_optimized(uint8_t channel) {
    uint64_t start = get_uptime_ms();
    while (ide_read_reg(channel, ATA_REG_STATUS) & ATA_SR_BSY) {
        if (get_uptime_ms() - start > 1000) {
            kprint("IDE: BSY timeout\n");
            break;
        }
        for (int i = 0; i < 1000; i++) {
             if (!(ide_read_reg(channel, ATA_REG_STATUS) & ATA_SR_BSY)) return;
             __asm__ volatile("pause");
        }
        sched_yield();
    }
}

// Benchmark setup
atomic_long worker_counter = 0;
volatile int stop_worker = 0;

void *worker_thread(void *arg) {
    while (!stop_worker) {
        atomic_fetch_add(&worker_counter, 1);
        // Do some work
        for(volatile int i=0; i<100; i++);
    }
    return NULL;
}

void *device_thread(void *arg) {
    // Simulate device busy for 100ms
    usleep(100000); // 100ms
    mock_status = 0; // Clear BSY
    return NULL;
}

void run_benchmark(const char *name, void (*wait_func)(uint8_t)) {
    pthread_t worker, device;

    mock_status = ATA_SR_BSY;
    atomic_store(&worker_counter, 0);
    stop_worker = 0;

    pthread_create(&worker, NULL, worker_thread, NULL);
    pthread_create(&device, NULL, device_thread, NULL);

    uint64_t start = get_uptime_ms();
    wait_func(0); // Run the wait function in main thread
    uint64_t end = get_uptime_ms();

    stop_worker = 1;
    pthread_join(worker, NULL);
    pthread_join(device, NULL);

    printf("%s: Waited %lu ms, Worker iterations: %ld\n",
           name, end - start, atomic_load(&worker_counter));
}

int main() {
    printf("Benchmarking ide_wait_bsy strategies...\n");

    // Run original
    run_benchmark("Original (Spin)", ide_wait_bsy_original);

    // Run optimized
    run_benchmark("Optimized (Yield)", ide_wait_bsy_optimized);

    return 0;
}
