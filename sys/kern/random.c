/*
 * random.c - Kernel Random Number Generator
 *
 * Implements a cryptographically secure RNG using:
 * - ChaCha20 as the CSPRNG (fast, secure, no weak keys)
 * - Entropy pool with LFSR mixing
 * - Multiple entropy sources (interrupts, HID, disk, network)
 * - Fast-key-erasure for forward secrecy
 *
 * Based on designs from Linux, FreeBSD, and OpenBSD.
 */

#include <sys/random.h>
#include <kern/random.h>
#include <kern/console.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <vfs/vfs.h>
#include <string.h>

/* Global RNG state */
struct rng_state rng_state;

/* Locks */
static spinlock_t entropy_lock;
static spinlock_t output_lock;

/* Wait Channel */
static int random_wait_channel = 0;

/*
 * ChaCha20 Implementation
 */

/* ChaCha20 quarter round */
#define ROTL32(v, n) (((v) << (n)) | ((v) >> (32 - (n))))

#define QR(a, b, c, d) do { \
    a += b; d ^= a; d = ROTL32(d, 16); \
    c += d; b ^= c; b = ROTL32(b, 12); \
    a += b; d ^= a; d = ROTL32(d, 8);  \
    c += d; b ^= c; b = ROTL32(b, 7);  \
} while(0)

/* ChaCha20 block function (20 rounds) */
static void chacha20_block_internal(uint32_t out[16], const uint32_t in[16]) {
    uint32_t x[16];
    
    for (int i = 0; i < 16; i++)
        x[i] = in[i];
    
    /* 20 rounds = 10 double-rounds */
    for (int i = 0; i < 10; i++) {
        /* Column rounds */
        QR(x[0], x[4], x[8],  x[12]);
        QR(x[1], x[5], x[9],  x[13]);
        QR(x[2], x[6], x[10], x[14]);
        QR(x[3], x[7], x[11], x[15]);
        /* Diagonal rounds */
        QR(x[0], x[5], x[10], x[15]);
        QR(x[1], x[6], x[11], x[12]);
        QR(x[2], x[7], x[8],  x[13]);
        QR(x[3], x[4], x[9],  x[14]);
    }
    
    for (int i = 0; i < 16; i++)
        out[i] = x[i] + in[i];
}

/* Initialize ChaCha20 context with key and nonce */
void chacha20_init(struct chacha20_ctx *ctx, const uint8_t *key, const uint8_t *nonce) {
    /* "expand 32-byte k" constant */
    ctx->state[0] = 0x61707865;
    ctx->state[1] = 0x3320646e;
    ctx->state[2] = 0x79622d32;
    ctx->state[3] = 0x6b206574;
    
    /* Key (8 words) */
    for (int i = 0; i < 8; i++) {
        ctx->state[4 + i] = key[i*4] | (key[i*4+1] << 8) | 
                           (key[i*4+2] << 16) | (key[i*4+3] << 24);
    }
    
    /* Counter (starts at 0) */
    ctx->state[12] = 0;
    
    /* Nonce (3 words) */
    for (int i = 0; i < 3; i++) {
        ctx->state[13 + i] = nonce[i*4] | (nonce[i*4+1] << 8) |
                            (nonce[i*4+2] << 16) | (nonce[i*4+3] << 24);
    }
    
    ctx->block_offset = CHACHA20_BLOCK_SIZE; /* Force regeneration on first use */
    ctx->counter = 0;
    ctx->bytes_generated = 0;
}

/* Generate one block of ChaCha20 keystream */
void chacha20_block(struct chacha20_ctx *ctx) {
    uint32_t out[16];
    
    chacha20_block_internal(out, ctx->state);
    
    /* Serialize to bytes (little-endian) */
    for (int i = 0; i < 16; i++) {
        ctx->block[i*4 + 0] = out[i] & 0xff;
        ctx->block[i*4 + 1] = (out[i] >> 8) & 0xff;
        ctx->block[i*4 + 2] = (out[i] >> 16) & 0xff;
        ctx->block[i*4 + 3] = (out[i] >> 24) & 0xff;
    }
    
    /* Increment counter */
    ctx->state[12]++;
    if (ctx->state[12] == 0) {
        /* Counter overflow - should never happen in practice */
        ctx->state[13]++;
    }
    
    ctx->counter++;
    ctx->block_offset = 0;
}

/* Extract random bytes from ChaCha20 */
void chacha20_extract(struct chacha20_ctx *ctx, void *buf, size_t len) {
    uint8_t *out = buf;
    
    while (len > 0) {
        if (ctx->block_offset >= CHACHA20_BLOCK_SIZE) {
            chacha20_block(ctx);
        }
        
        size_t available = CHACHA20_BLOCK_SIZE - ctx->block_offset;
        size_t to_copy = (len < available) ? len : available;
        
        memcpy(out, ctx->block + ctx->block_offset, to_copy);
        
        out += to_copy;
        len -= to_copy;
        ctx->block_offset += to_copy;
        ctx->bytes_generated += to_copy;
    }
}

/* Rekey the CSPRNG for forward secrecy */
void chacha20_rekey(struct chacha20_ctx *ctx) {
    uint8_t new_key[CHACHA20_KEY_SIZE];
    uint8_t new_nonce[CHACHA20_NONCE_SIZE];
    
    /* Generate new key and nonce from current state */
    chacha20_extract(ctx, new_key, CHACHA20_KEY_SIZE);
    chacha20_extract(ctx, new_nonce, CHACHA20_NONCE_SIZE);
    
    /* Reinitialize with new key material */
    chacha20_init(ctx, new_key, new_nonce);
    
    /* Secure erasure */
    memset(new_key, 0, sizeof(new_key));
    memset(new_nonce, 0, sizeof(new_nonce));
}

/* Securely wipe context */
void chacha20_wipe(struct chacha20_ctx *ctx) {
    memset(ctx, 0, sizeof(*ctx));
}

/*
 * Entropy Pool Implementation
 */

/* LFSR twist table for fast mixing */
static const uint32_t twist_table[8] = {
    0x00000000, 0x3b6e20c8, 0x76dc4190, 0x4db26158,
    0xedb88320, 0xd6d6a3e8, 0x9b64c2b0, 0xa00ae278
};

/* Initialize entropy pool */
void pool_init(struct entropy_pool *pool) {
    memset(pool, 0, sizeof(*pool));
}

/* Mix bytes into entropy pool using LFSR with twist table */
void pool_mix_bytes(struct entropy_pool *pool, const void *data, size_t len) {
    const uint8_t *src = data;
    
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = src[i];
        
        /* XOR into pool at current position */
        pool->pool[pool->mix_ptr] ^= byte;
        
        /* Apply twist table feedback */
        uint32_t tap = pool->pool[(pool->mix_ptr + 72) % ENTROPY_POOL_SIZE];
        pool->pool[pool->mix_ptr] ^= twist_table[tap & 7];
        
        /* Advance pointer with wraparound */
        pool->mix_ptr = (pool->mix_ptr + 1) % ENTROPY_POOL_SIZE;
    }
    
    pool->total_harvested += len;
}

/* Simple SHA-256-like compression for extraction (simplified) */
static void pool_compress(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len) {
    /* Simplified compression - XOR folding with rotation */
    memset(out, 0, out_len);
    
    for (size_t i = 0; i < in_len; i++) {
        out[i % out_len] ^= in[i];
        out[(i + 1) % out_len] ^= ROTL32(in[i], (i & 7));
    }
}

/* Extract mixed entropy from pool */
void pool_extract_bytes(struct entropy_pool *pool, void *out, size_t len) {
    uint8_t compressed[64];
    
    /* Compress entire pool into temporary buffer */
    pool_compress(pool->pool, ENTROPY_POOL_SIZE, compressed, sizeof(compressed));
    
    /* Copy requested bytes */
    size_t to_copy = (len < sizeof(compressed)) ? len : sizeof(compressed);
    memcpy(out, compressed, to_copy);
    
    /* Mix extraction back into pool for diffusion */
    pool_mix_bytes(pool, compressed, sizeof(compressed));
    
    /* Secure erasure */
    memset(compressed, 0, sizeof(compressed));
}

/*
 * Hardware RNG Support
 */

void random_detect_hwrng(void) {
    uint32_t eax, ebx, ecx, edx;
    
    rng_state.has_rdrand = 0;
    rng_state.has_rdseed = 0;
    
    /* Check CPUID for feature flags */
    __asm__ volatile("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(1), "c"(0));
    
    if (ecx & (1 << 30)) {
        rng_state.has_rdrand = 1;
        kprint("RNG: RDRAND available\n");
    }
    
    /* Check extended features for RDSEED */
    __asm__ volatile("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(7), "c"(0));
    
    if (ebx & (1 << 18)) {
        rng_state.has_rdseed = 1;
        kprint("RNG: RDSEED available\n");
    }
}

int random_has_rdrand(void) {
    return rng_state.has_rdrand;
}

int random_has_rdseed(void) {
    return rng_state.has_rdseed;
}

/* Get 32-bit random value from RDRAND */
int rdrand32(uint32_t *value) {
    if (!rng_state.has_rdrand) return 0;
    
    uint8_t success = 0;
    for (int retries = 10; retries > 0; retries--) {
        __asm__ volatile(
            "rdrand %0\n\t"
            "setc %1"
            : "=r"(*value), "=qm"(success)
        );
        if (success) return 1;
    }
    return 0;
}

/* Get 64-bit random value from RDRAND (i386: two 32-bit calls) */
int rdrand64(uint64_t *value) {
    uint32_t lo, hi;
    if (!rdrand32(&lo) || !rdrand32(&hi)) return 0;
    *value = ((uint64_t)hi << 32) | lo;
    return 1;
}

/* Get 32-bit random value from RDSEED */
int rdseed32(uint32_t *value) {
    if (!rng_state.has_rdseed) return 0;
    
    uint8_t success = 0;
    /* RDSEED may fail more often than RDRAND, minimal retry */
    for (int retries = 10; retries > 0; retries--) {
        __asm__ volatile(
            "rdseed %0\n\t"
            "setc %1"
            : "=r"(*value), "=qm"(success)
        );
        if (success) return 1;
        /* Pause to let entropy replenish? pause instruction? */
        __asm__ volatile("pause");
    }
    return 0;
}

/* Get 64-bit random value from RDSEED (i386: two 32-bit calls) */
int rdseed64(uint64_t *value) {
    uint32_t lo, hi;
    if (!rdseed32(&lo) || !rdseed32(&hi)) return 0;
    *value = ((uint64_t)hi << 32) | lo;
    return 1;
}

/* Harvest entropy from hardware RNG */
int random_harvest_hwrng(void) {
    uint32_t hw_random;
    int harvested = 0;
    
    /* Try RDSEED first (direct entropy) */
    if (rng_state.has_rdseed && rdseed32(&hw_random)) {
        pool_mix_bytes(&rng_state.input_pool, &hw_random, sizeof(hw_random));
        rng_state.input_pool.entropy_count += 32; /* Full 32 bits */
        harvested = 1;
    } 
    /* Fallback/Supplement with RDRAND */
    else if (rng_state.has_rdrand && rdrand32(&hw_random)) {
        pool_mix_bytes(&rng_state.input_pool, &hw_random, sizeof(hw_random));
        rng_state.input_pool.entropy_count += 32; /* Full 32 bits from HWRNG */
        harvested = 1;
    }
    
    return harvested;
}

/*
 * Entropy Harvesting API
 */

void random_harvest(const void *data, size_t len, unsigned int bits, 
                    enum entropy_source source) {
    if (!data || len == 0) return;
    if (source >= ENTROPY_MAX) return;
    
    spinlock_acquire(&entropy_lock);
    
    pool_mix_bytes(&rng_state.input_pool, data, len);
    
    /* Credit entropy (conservative) */
    uint32_t new_bits = rng_state.input_pool.entropy_count + bits;
    if (new_bits > ENTROPY_POOL_SIZE * 8) {
        new_bits = ENTROPY_POOL_SIZE * 8; /* Cap at pool size */
    }
    rng_state.input_pool.entropy_count = new_bits;
    
    rng_state.harvest_count[source]++;
    
    spinlock_release(&entropy_lock);
}

void random_harvest_fast(const void *data, size_t len) {
    /* ISR-safe: just mix without lock (data races are acceptable for entropy) */
    if (!data || len == 0) return;
    
    const uint8_t *src = data;
    uint32_t ptr = rng_state.input_pool.mix_ptr;
    
    for (size_t i = 0; i < len; i++) {
        rng_state.input_pool.pool[ptr] ^= src[i];
        ptr = (ptr + 1) % ENTROPY_POOL_SIZE;
    }
    
    rng_state.input_pool.mix_ptr = ptr;
}

void random_harvest_direct(const void *data, size_t len, unsigned int bits) {
    random_harvest(data, len, bits, ENTROPY_HWRNG);
}

/*
 * Random Output API
 */

int random_is_seeded(void) {
    return rng_state.seeded;
}

/* Reseed CSPRNG from entropy pool */
static void random_reseed(void) {
    uint8_t seed[CHACHA20_KEY_SIZE + CHACHA20_NONCE_SIZE];
    
    spinlock_acquire(&entropy_lock);
    pool_extract_bytes(&rng_state.input_pool, seed, sizeof(seed));
    rng_state.input_pool.entropy_count = 0; /* Debited on extraction */
    spinlock_release(&entropy_lock);
    
    spinlock_acquire(&output_lock);
    chacha20_init(&rng_state.csprng, seed, seed + CHACHA20_KEY_SIZE);
    rng_state.csprng.bytes_generated = 0;
    rng_state.reseed_count++;
    rng_state.seeded = 1;
    spinlock_release(&output_lock);
    
    rng_state.seeded = 1;
    spinlock_release(&output_lock);
    
    /* Wake up any blocked readers */
    sched_wakeup(&random_wait_channel);

    memset(seed, 0, sizeof(seed));
}

int random_get_bytes(void *buf, size_t len) {
    return random_get_bytes_flags(buf, len, 0);
}

int random_get_bytes_flags(void *buf, size_t len, unsigned int flags) {
    if (!buf || len == 0) return 0;
    
    /* Check if we should block for entropy */
    if (!rng_state.seeded && !(flags & GRND_INSECURE)) {
        if (flags & GRND_NONBLOCK) {
            return -11; /* EAGAIN */
        }
        
        /* Block until seeded */
        while (!rng_state.seeded) {
            /* Try to harvest first */
            random_harvest_hwrng();
            if (rng_state.input_pool.entropy_count >= 256) {
                random_reseed();
                break;
            }
            
            /* If still unseeded, sleep */
            if (!rng_state.seeded) {
                kprint("RNG: Blocking for entropy...\n");
                sched_sleep(&random_wait_channel);
            }
        }
    }
    
    spinlock_acquire(&output_lock);
    
    /* Check if reseeding is needed */
    if (rng_state.csprng.bytes_generated >= RESEED_INTERVAL) {
        spinlock_release(&output_lock);
        random_reseed();
        spinlock_acquire(&output_lock);
    }
    
    chacha20_extract(&rng_state.csprng, buf, len);
    
    spinlock_release(&output_lock);
    
    return len;
}

/*
 * Device Node Callbacks (fs_node_t compatible)
 */

/* /dev/random read - blocks until sufficient entropy */
static size_t random_dev_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    (void)offset;
    
    int ret = random_get_bytes_flags(buffer, size, GRND_RANDOM);
    return (ret > 0) ? (size_t)ret : 0;
}

/* /dev/urandom read - never blocks */
static size_t urandom_dev_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    (void)offset;
    
    int ret = random_get_bytes_flags(buffer, size, GRND_INSECURE);
    return (ret > 0) ? (size_t)ret : 0;
}

/* Write to /dev/random or /dev/urandom adds entropy */
static size_t random_dev_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node;
    (void)offset;
    
    random_harvest(buffer, size, size / 8, ENTROPY_SOFTWARE);
    return size;
}

/* Device nodes */
static fs_node_t random_node;
static fs_node_t urandom_node;

/*
 * Initialization
 */

void random_init(void) {
    kprint("RNG: Initializing random subsystem\n");
    
    /* Initialize state */
    memset(&rng_state, 0, sizeof(rng_state));
    pool_init(&rng_state.input_pool);
    
    /* Initialize locks */
    spinlock_init(&entropy_lock, "entropy_lock");
    spinlock_init(&output_lock, "output_lock");
    
    /* Detect hardware RNG */
    random_detect_hwrng();
    
    /* Initial seeding from hardware RNG if available */
    if (rng_state.has_rdrand) {
        for (int i = 0; i < 8; i++) {
            random_harvest_hwrng();
        }
    }
    
    /* Try to seed the CSPRNG */
    if (rng_state.input_pool.entropy_count >= 256) {
        random_reseed();
        kprint("RNG: CSPRNG seeded\n");
    } else {
        kprint("RNG: Waiting for entropy\n");
    }
    
    /* Register /dev/random */
    extern void devfs_register_device(fs_node_t *node);
    
    memset(&random_node, 0, sizeof(fs_node_t));
    strcpy(random_node.name, "random");
    random_node.flags = FS_CHARDEVICE;
    random_node.read = random_dev_read;
    random_node.write = random_dev_write;
    random_node.rdev = (1 << 8) | 8;
    devfs_register_device(&random_node);
    
    /* Register /dev/urandom */
    memset(&urandom_node, 0, sizeof(fs_node_t));
    strcpy(urandom_node.name, "urandom");
    urandom_node.flags = FS_CHARDEVICE;
    urandom_node.read = urandom_dev_read;
    urandom_node.write = random_dev_write;
    urandom_node.rdev = (1 << 8) | 9;
    devfs_register_device(&urandom_node);
    
    kprint("RNG: Registered /dev/random and /dev/urandom\n");
}
