#pragma once
#include <stdint.h>
static inline uint32_t intr_disable() { return 0; }
static inline void intr_restore(uint32_t f) { (void)f; }
