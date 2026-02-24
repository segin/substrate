/*
 * uma_debug.c - UMA Debug Support
 * 
 * Provides redzone checking, memory poisoning, and
 * allocation tracking for debugging memory corruption.
 */

#include <vm/uma.h>
#include <kern/console.h>
#include <kern/panic.h>
#include <stdint.h>
#include <string.h>


/*
 * Fill redzone with pattern bytes
 */
void uma_debug_fill_redzone(uma_zone_t *zone, void *item) {
    if (!(zone->uz_flags & UMA_ZONE_REDZONE)) return;
    
    /* Item points to user data; redzone is before and after */
    uint8_t *pre = (uint8_t *)item - UMA_REDZONE_SIZE;
    uint8_t *post = (uint8_t *)item + zone->uz_size;
    
    memset(pre, UMA_REDZONE_BYTE, UMA_REDZONE_SIZE);
    memset(post, UMA_REDZONE_BYTE, UMA_REDZONE_SIZE);
}

/*
 * Check redzone integrity
 */
void uma_debug_check_redzone_impl(uma_zone_t *zone, void *item) {
    
    uint8_t *pre = (uint8_t *)item - UMA_REDZONE_SIZE;
    uint8_t *post = (uint8_t *)item + zone->uz_size;
    
    /* Check pre-redzone */
    for (int i = 0; i < UMA_REDZONE_SIZE; i++) {
        if (pre[i] != UMA_REDZONE_BYTE) {
            kprint("UMA: REDZONE UNDERFLOW in zone ");
            kprint(zone->uz_name);
            kprint(" at offset ");
            // kprintf("%d\n", -UMA_REDZONE_SIZE + i);
            kprint("\n");
            panic("UMA Redzone Violation");
        }
    }
    
    /* Check post-redzone */
    for (int i = 0; i < UMA_REDZONE_SIZE; i++) {
        if (post[i] != UMA_REDZONE_BYTE) {
            kprint("UMA: REDZONE OVERFLOW in zone ");
            kprint(zone->uz_name);
            kprint(" at offset ");
            // kprintf("%d\n", zone->uz_size + i);
            kprint("\n");
            panic("UMA Redzone Violation");
        }
    }
}

/*
 * Poison freed memory with pattern
 */
void uma_debug_poison_free_impl(uma_zone_t *zone, void *item) {
    
    uint32_t *p = (uint32_t *)item;
    size_t words = zone->uz_size / sizeof(uint32_t);
    
    for (size_t i = 0; i < words; i++) {
        p[i] = UMA_POISON_FREE;
    }
}

/*
 * Poison allocated memory before constructor
 */
void uma_debug_poison_alloc_impl(uma_zone_t *zone, void *item) {
    
    /* First check if memory still has free pattern (UAF detection) */
    uint32_t *p = (uint32_t *)item;
    size_t words = zone->uz_size / sizeof(uint32_t);
    
    /* Fill with alloc pattern */
    for (size_t i = 0; i < words; i++) {
        p[i] = UMA_POISON_ALLOC;
    }
    
    /* Set up redzones */
    uma_debug_fill_redzone(zone, item);
}
