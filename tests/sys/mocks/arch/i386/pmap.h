#ifndef _PMAP_H
#define _PMAP_H
#include <stdint.h>
uintptr_t pmap_extract(void *pmap, uintptr_t va);
#endif
