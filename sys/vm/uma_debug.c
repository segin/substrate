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
    if (!(zone->uz_flags & UMA_ZONE_REDZONE)) return;

    uint8_t *pre = (uint8_t *)item - UMA_REDZONE_SIZE;
    uint8_t *post = (uint8_t *)item + zone->uz_size;
    
    /* Check pre-redzone */
    for (int i = 0; i < UMA_REDZONE_SIZE; i++) {
        if (pre[i] != UMA_REDZONE_BYTE) {
            kprintf("UMA: REDZONE UNDERFLOW in zone '%s' at offset %d\n",
                    zone->uz_name, -UMA_REDZONE_SIZE + i);
            kprintf("Item: %p\n", item);
            panic("UMA Redzone Violation");
        }
    }
    
    /* Check post-redzone */
    for (int i = 0; i < UMA_REDZONE_SIZE; i++) {
        if (post[i] != UMA_REDZONE_BYTE) {
            kprintf("UMA: REDZONE OVERFLOW in zone '%s' at offset %d\n",
                    zone->uz_name, (int)(zone->uz_size + i));
            kprintf("Item: %p\n", item);
            panic("UMA Redzone Violation");
        }
    }
}

/*
 * Poison freed memory with pattern
 */
void uma_debug_poison_free_impl(uma_zone_t *zone, void *item) {
    if (!(zone->uz_flags & UMA_ZONE_TRASH)) return;

    uint8_t *p = (uint8_t *)item;
    const uint8_t *pattern = (const uint8_t *)&(uint32_t){ UMA_POISON_FREE };

    for (size_t i = 0; i < zone->uz_size; i++) {
        p[i] = pattern[i % sizeof(uint32_t)];
    }
}

/*
 * Poison allocated memory before constructor
 */
void uma_debug_poison_alloc_impl(uma_zone_t *zone, void *item) {
    if (!(zone->uz_flags & UMA_ZONE_TRASH)) return;

    /* First check if memory still has free pattern (UAF detection) */
    uint8_t *p = (uint8_t *)item;
    const uint8_t *pattern = (const uint8_t *)&(uint32_t){ UMA_POISON_FREE };

    for (size_t i = 0; i < zone->uz_size; i++) {
        if (p[i] != pattern[i % sizeof(uint32_t)]) {
            kprintf("UMA: POISON VIOLATION in zone '%s' at offset %d\n",
                    zone->uz_name, (int)i);
            kprintf("Item: %p\n", item);
            panic("UMA Poison Violation");
        }
    }

    /* Fill with alloc pattern */
    pattern = (const uint8_t *)&(uint32_t){ UMA_POISON_ALLOC };
    for (size_t i = 0; i < zone->uz_size; i++) {
        p[i] = pattern[i % sizeof(uint32_t)];
    }
    
    /* Set up redzones */
    uma_debug_fill_redzone(zone, item);
}
