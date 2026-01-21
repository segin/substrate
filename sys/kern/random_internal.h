/*
 * random_internal.h - Kernel RNG Internal Structures
 *
 * Internal data structures and constants for the random subsystem.
 * Not for use outside sys/kern/random.c
 */

#ifndef _KERN_RANDOM_INTERNAL_H
#define _KERN_RANDOM_INTERNAL_H

#include <sys/random.h>
#include <sys/lock.h>
#include <stdint.h>

/*
 * Configuration constants
 */
#define ENTROPY_POOL_SIZE       512     /* Pool size in bytes (4096 bits) */
#define CHACHA20_KEY_SIZE       32      /* 256-bit key */
#define CHACHA20_NONCE_SIZE     12      /* 96-bit nonce */
#define CHACHA20_BLOCK_SIZE     64      /* 512-bit output block */
#define RESEED_INTERVAL         (1024 * 1024)  /* Rekey after 1MB output */

/*
 * Entropy pool structure
 * Uses LFSR-based mixing with twist table feedback.
 */
struct entropy_pool {
    uint8_t pool[ENTROPY_POOL_SIZE];    /* Raw entropy pool */
    uint32_t mix_ptr;                    /* Current mixing position */
    uint32_t entropy_count;              /* Estimated entropy bits */
    uint32_t total_harvested;            /* Total bytes harvested since boot */
};

/*
 * ChaCha20 CSPRNG context
 * Implements fast-key-erasure for forward secrecy.
 */
struct chacha20_ctx {
    uint32_t state[16];                  /* ChaCha20 state matrix */
    uint8_t block[CHACHA20_BLOCK_SIZE];  /* Current output block */
    uint32_t block_offset;               /* Offset into current block */
    uint64_t counter;                    /* Block counter */
    uint64_t bytes_generated;            /* Total bytes since last rekey */
};

/*
 * Global RNG state
 */
struct rng_state {
    struct entropy_pool input_pool;      /* Input entropy pool */
    struct chacha20_ctx csprng;          /* ChaCha20 CSPRNG context */
    
    int seeded;                          /* True if RNG has initial seed */
    int fully_seeded;                    /* True if sufficient entropy collected */
    uint32_t reseed_count;               /* Number of reseeds performed */
    
    /* Feature flags */
    int has_rdrand;                      /* RDRAND instruction available */
    int has_rdseed;                      /* RDSEED instruction available */
    
    /* Per-source rate limiting */
    uint32_t harvest_count[ENTROPY_MAX]; /* Events per source */
    uint32_t last_harvest_time[ENTROPY_MAX]; /* Last harvest timestamp */
};

/*
 * Global RNG state instance
 */
extern struct rng_state rng_state;

/*
 * Internal functions
 */

/* ChaCha20 core operations */
void chacha20_init(struct chacha20_ctx *ctx, const uint8_t *key, const uint8_t *nonce);
void chacha20_block(struct chacha20_ctx *ctx);
void chacha20_extract(struct chacha20_ctx *ctx, void *buf, size_t len);
void chacha20_rekey(struct chacha20_ctx *ctx);
void chacha20_wipe(struct chacha20_ctx *ctx);

/* Entropy pool operations */
void pool_init(struct entropy_pool *pool);
void pool_mix_bytes(struct entropy_pool *pool, const void *data, size_t len);
void pool_extract_bytes(struct entropy_pool *pool, void *out, size_t len);

/* Hardware RNG support */
void random_detect_hwrng(void);
int random_harvest_hwrng(void);

#endif /* _KERN_RANDOM_INTERNAL_H */
