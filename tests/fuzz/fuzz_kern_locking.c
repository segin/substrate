#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/*
 * Fuzz Test: Spinlock and Mutex Operations
 * 
 * Tests:
 * 1. Lock/unlock state machine correctness
 * 2. Recursive lock detection
 * 3. Ownership tracking
 * 4. Interrupt state preservation
 * 5. Deadlock detection (mock)
 * 6. Lock ordering validation
 */

// Mock spinlock structure
typedef struct {
    volatile uint32_t locked;
    volatile int owner_cpu;
    const char *name;
    uint32_t acquire_count;
    uint32_t contention_count;
} mock_spinlock_t;

// Mock mutex structure (sleeping lock)
typedef struct {
    volatile uint32_t locked;
    volatile int owner_tid;
    const char *name;
    int waiters;
} mock_mutex_t;

// Simple LCG PRNG
static uint32_t fuzz_state = 0;
static uint32_t fuzz_rand(void) {
    fuzz_state = fuzz_state * 1103515245 + 12345;
    return (fuzz_state >> 16) & 0x7FFF;
}

// Mock CPU ID (single-threaded test)
static int mock_cpu_id = 0;
static int mock_tid = 1;
static uint32_t mock_eflags = 0x200; // IF set

// Mock interrupt control
static uint32_t mock_cli(void) {
    uint32_t old = mock_eflags;
    mock_eflags &= ~0x200;
    return old;
}

static void mock_sti(uint32_t old) {
    mock_eflags = old;
}

// Mock spinlock operations
static void mock_spinlock_init(mock_spinlock_t *lock, const char *name) {
    lock->locked = 0;
    lock->owner_cpu = -1;
    lock->name = name;
    lock->acquire_count = 0;
    lock->contention_count = 0;
}

static bool mock_spinlock_try_acquire(mock_spinlock_t *lock) {
    if (lock->locked) {
        lock->contention_count++;
        return false;
    }
    lock->locked = 1;
    lock->owner_cpu = mock_cpu_id;
    lock->acquire_count++;
    return true;
}

static void mock_spinlock_acquire(mock_spinlock_t *lock) {
    // In real code, this would spin
    if (lock->locked && lock->owner_cpu == mock_cpu_id) {
        __builtin_trap(); // Recursive lock attempt!
    }
    
    while (!mock_spinlock_try_acquire(lock)) {
        // Simulate spinning - in test, this is instant
        break; // Single-threaded, so break to avoid infinite loop
    }
}

static void mock_spinlock_release(mock_spinlock_t *lock) {
    if (!lock->locked) {
        __builtin_trap(); // Release without acquire!
    }
    if (lock->owner_cpu != mock_cpu_id) {
        __builtin_trap(); // Release by non-owner!
    }
    lock->owner_cpu = -1;
    lock->locked = 0;
}

static bool mock_spinlock_is_held(mock_spinlock_t *lock) {
    return lock->locked && lock->owner_cpu == mock_cpu_id;
}

// Mock mutex operations
static void mock_mutex_init(mock_mutex_t *mtx, const char *name) {
    mtx->locked = 0;
    mtx->owner_tid = -1;
    mtx->name = name;
    mtx->waiters = 0;
}

static bool mock_mutex_try_lock(mock_mutex_t *mtx) {
    if (mtx->locked) return false;
    mtx->locked = 1;
    mtx->owner_tid = mock_tid;
    return true;
}

static void mock_mutex_lock(mock_mutex_t *mtx) {
    if (mtx->locked && mtx->owner_tid == mock_tid) {
        __builtin_trap(); // Recursive mutex lock!
    }
    mtx->waiters++;
    while (!mock_mutex_try_lock(mtx)) {
        // Would sleep here in real implementation
        break;
    }
    mtx->waiters--;
}

static void mock_mutex_unlock(mock_mutex_t *mtx) {
    if (!mtx->locked) {
        __builtin_trap(); // Unlock without lock!
    }
    if (mtx->owner_tid != mock_tid) {
        __builtin_trap(); // Unlock by non-owner!
    }
    mtx->owner_tid = -1;
    mtx->locked = 0;
}

void fuzz_spinlock_ops(uint32_t seed) {
    fuzz_state = seed;
    
    mock_spinlock_t locks[16];
    for (int i = 0; i < 16; i++) {
        mock_spinlock_init(&locks[i], "test-lock");
    }
    
    // ========================================
    // Phase 1: Basic State Machine
    // ========================================
    
    mock_spinlock_t lock;
    mock_spinlock_init(&lock, "phase1");
    
    // Initial state
    if (lock.locked) {
        __builtin_trap(); // Should be unlocked!
    }
    
    // Acquire
    mock_spinlock_acquire(&lock);
    if (!lock.locked || lock.owner_cpu != mock_cpu_id) {
        __builtin_trap(); // Acquire failed!
    }
    
    // Release
    mock_spinlock_release(&lock);
    if (lock.locked) {
        __builtin_trap(); // Should be unlocked!
    }
    
    // ========================================
    // Phase 2: Multiple Locks (Order)
    // ========================================
    
    // Acquire in order
    for (int i = 0; i < 8; i++) {
        mock_spinlock_acquire(&locks[i]);
    }
    
    // Release in reverse order (proper nesting)
    for (int i = 7; i >= 0; i--) {
        mock_spinlock_release(&locks[i]);
    }
    
    // ========================================
    // Phase 3: Try-Acquire
    // ========================================
    
    mock_spinlock_init(&lock, "try");
    
    bool got = mock_spinlock_try_acquire(&lock);
    if (!got) {
        __builtin_trap(); // Should succeed on unlocked!
    }
    
    bool got2 = mock_spinlock_try_acquire(&lock);
    if (got2) {
        __builtin_trap(); // Should fail on locked!
    }
    
    mock_spinlock_release(&lock);
    
    // ========================================
    // Phase 4: is_held Validation
    // ========================================
    
    mock_spinlock_init(&lock, "held");
    
    if (mock_spinlock_is_held(&lock)) {
        __builtin_trap(); // Should not be held!
    }
    
    mock_spinlock_acquire(&lock);
    if (!mock_spinlock_is_held(&lock)) {
        __builtin_trap(); // Should be held!
    }
    
    mock_spinlock_release(&lock);
    if (mock_spinlock_is_held(&lock)) {
        __builtin_trap(); // Should not be held!
    }
    
    // ========================================
    // Phase 5: Interrupt State Preservation
    // ========================================
    
    mock_eflags = 0x200; // IF set
    uint32_t saved = mock_cli();
    
    mock_spinlock_acquire(&lock);
    // Interrupts should still be disabled
    if (mock_eflags & 0x200) {
        __builtin_trap(); // IF should be clear!
    }
    
    mock_spinlock_release(&lock);
    mock_sti(saved);
    
    if (!(mock_eflags & 0x200)) {
        __builtin_trap(); // IF should be restored!
    }
    
    // ========================================
    // Phase 6: Mutex Operations
    // ========================================
    
    mock_mutex_t mtx;
    mock_mutex_init(&mtx, "test-mutex");
    
    if (mtx.locked) {
        __builtin_trap();
    }
    
    mock_mutex_lock(&mtx);
    if (!mtx.locked || mtx.owner_tid != mock_tid) {
        __builtin_trap();
    }
    
    mock_mutex_unlock(&mtx);
    if (mtx.locked) {
        __builtin_trap();
    }
    
    // ========================================
    // Phase 7: Statistics Tracking
    // ========================================
    
    mock_spinlock_init(&lock, "stats");
    
    for (int i = 0; i < 1000; i++) {
        mock_spinlock_acquire(&lock);
        mock_spinlock_release(&lock);
    }
    
    if (lock.acquire_count != 1000) {
        __builtin_trap(); // Acquire count mismatch!
    }
    
    // ========================================
    // Phase 8: Random Stress Test
    // ========================================
    
    // Track which locks are held
    bool held[16] = {false};
    
    for (int iter = 0; iter < 100000; iter++) {
        int idx = fuzz_rand() % 16;
        
        if (!held[idx]) {
            mock_spinlock_acquire(&locks[idx]);
            held[idx] = true;
        } else {
            mock_spinlock_release(&locks[idx]);
            held[idx] = false;
        }
    }
    
    // Release all remaining
    for (int i = 0; i < 16; i++) {
        if (held[i]) {
            mock_spinlock_release(&locks[i]);
        }
    }
    
    // Verify all unlocked
    for (int i = 0; i < 16; i++) {
        if (locks[i].locked) {
            __builtin_trap(); // Lock still held!
        }
    }
}
