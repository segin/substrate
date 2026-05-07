#ifndef _PROCFS_H
#define _PROCFS_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t (*procfs_entry_generator_t)(char *buf, size_t size, void *opaque);

int procfs_register_entry(const char *name, procfs_entry_generator_t generator, void *opaque);
void procfs_init(void);
/* Reclaim the lazy /proc/<pid> entries when a process is reaped. */
void procfs_release_pid_nodes(int pid);

#endif
