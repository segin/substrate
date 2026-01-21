/*
 * sys/random.h - Kernel Random Number Generator Public API
 *
 * Provides cryptographically secure random number generation.
 * Based on ChaCha20 CSPRNG with entropy pool mixing.
 */

#ifndef _SYS_RANDOM_H
#define _SYS_RANDOM_H

#include <stddef.h>
#include <stdint.h>

/*
 * Flags for getrandom() syscall
 */
#define GRND_NONBLOCK   0x0001  /* Don't block if no entropy available */
#define GRND_RANDOM     0x0002  /* Use /dev/random (blocking) behavior */
#define GRND_INSECURE   0x0004  /* Allow non-cryptographic random (boot-time) */

/*
 * Entropy source identifiers for random_harvest()
 */
enum entropy_source {
    ENTROPY_KEYBOARD = 0,   /* Keyboard HID events */
    ENTROPY_MOUSE,          /* Mouse/pointing device events */
    ENTROPY_DISK,           /* Disk I/O completion timing */
    ENTROPY_NET,            /* Network packet timing */
    ENTROPY_IRQ,            /* General interrupt timing */
    ENTROPY_HWRNG,          /* Hardware RNG (RDRAND/RDSEED) */
    ENTROPY_TIMER,          /* Timer interrupt jitter */
    ENTROPY_SOFTWARE,       /* Software sources (PID, addresses) */
    ENTROPY_BOOT,           /* Boot-time seeds */
    ENTROPY_MAX
};

/*
 * Kernel API - Random number generation
 */

/* Initialize RNG subsystem (called from kmain) */
void random_init(void);

/* Get cryptographically secure random bytes (may block) */
int random_get_bytes(void *buf, size_t len);

/* Get random bytes with flags (getrandom syscall backend) */
int random_get_bytes_flags(void *buf, size_t len, unsigned int flags);

/* Check if RNG is seeded with sufficient entropy */
int random_is_seeded(void);

/*
 * Kernel API - Entropy harvesting (called from drivers/ISRs)
 */

/* General entropy harvesting (may acquire lock) */
void random_harvest(const void *data, size_t len, unsigned int bits, 
                    enum entropy_source source);

/* Fast entropy harvesting (ISR-safe, no lock, lower quality) */
void random_harvest_fast(const void *data, size_t len);

/* High-quality entropy harvesting (trusted source, full credit) */
void random_harvest_direct(const void *data, size_t len, unsigned int bits);

/*
 * Hardware RNG detection and usage
 */

/* Check if RDRAND is available */
int random_has_rdrand(void);

/* Check if RDSEED is available */
int random_has_rdseed(void);

/* Get random 32-bit value from hardware RNG (returns 0 on failure) */
int rdrand32(uint32_t *value);

/* Get random 64-bit value from hardware RNG (returns 0 on failure) */
int rdrand64(uint64_t *value);

#endif /* _SYS_RANDOM_H */
