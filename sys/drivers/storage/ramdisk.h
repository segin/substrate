#ifndef _RAMDISK_H
#define _RAMDISK_H

#include <stddef.h>

int ramdisk_create(void *addr, size_t size);
void ramdisk_init(void *addr, size_t size);

#endif
