#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <stddef.h>

// Include procfs.c
// We rely on Makefile to set include paths correctly so <vfs/vfs.h> etc are found.
#include "../../sys/fs/procfs.c"

// Mock implementations

// Lock
void spinlock_init(spinlock_t *l, const char *name) { (void)l; (void)name; }
void spinlock_acquire(spinlock_t *l) { (void)l; }
void spinlock_release(spinlock_t *l) { (void)l; }

// Memory
void *kmalloc(size_t size) { return malloc(size); }
void kfree(void *ptr, size_t size) { (void)size; free(ptr); }

// Time
uint32_t get_time(void) { return 10000; }

// PMM
uint32_t pmm_get_total_memory(void) { return 256 * 1024 * 1024; }
uint32_t pmm_get_free_memory(void) { return 128 * 1024 * 1024; }

// Cmdline
void cmdline_get(char *buf, size_t buf_len) {
    snprintf(buf, buf_len, "root=/dev/hda1");
}

// Sched
void sched_get_loadavg(unsigned long *loads) {
    loads[0] = 1024; // 0.5
    loads[1] = 2048; // 1.0
    loads[2] = 3072; // 1.5
}
uint32_t sched_count_runnable(void) { return 5; }
uint32_t sched_count_threads(void) { return 50; }

// Proc
int proc_get_last_pid(void) { return 1234; }

// PMAP
int sys_pmap_stats(struct pmap_stats *out) {
    memset(out, 0, sizeof(*out));
    out->faults = 100;
    out->cow_faults = 50;
    out->total_pmaps = 10;
    return 0;
}

// VFS
filesystem_t *vfs_get_filesystems(void) {
    static filesystem_t fs_list[2];
    strcpy(fs_list[0].name, "ext2");
    fs_list[0].next = &fs_list[1];
    strcpy(fs_list[1].name, "procfs");
    fs_list[1].next = NULL;
    return &fs_list[0];
}

void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

// Personality
struct personality *perso_lookup(int id) { (void)id; return NULL; }

// Process List
// Defined in pm.h as extern. We provide definition here.
process_t processes[MAX_PROCS];
process_t *current_process;
mutex_t proctree_lock; // Declared in pm.h

process_t *proc_find(int pid) {
    for(int i=0; i<MAX_PROCS; i++) {
        if(processes[i].pid == pid) return &processes[i];
    }
    return NULL;
}

int main() {
    // Setup valid process for PID tests
    memset(processes, 0, sizeof(processes));
    current_process = &processes[0];
    processes[0].pid = 1;
    strcpy(processes[0].comm, "init");
    processes[0].uid = 0;
    processes[0].gid = 0;
    processes[0].perso_id = 0; // PERS_NATIVE

    char buf[4096];
    uint32_t len;

    printf("Starting ProcFS Host Tests...\n");

    // Test gen_cpuinfo
    len = gen_cpuinfo(buf, sizeof(buf));
    assert(strstr(buf, "GenuineIntel") != NULL);
    assert(strstr(buf, "Substrate Virtual CPU") != NULL);
    printf("PASS: gen_cpuinfo\n");

    // Test gen_meminfo
    len = gen_meminfo(buf, sizeof(buf));
    // Total 256MB = 262144 kB
    // Free 128MB = 131072 kB
    // Used 128MB = 131072 kB
    // "MemTotal:    %8u kB" -> 4 spaces + 2 padding = 6 spaces
    assert(strstr(buf, "MemTotal:      262144 kB") != NULL);
    // "MemFree:     %8u kB" -> 5 spaces + 2 padding = 7 spaces
    assert(strstr(buf, "MemFree:       131072 kB") != NULL);
    printf("PASS: gen_meminfo\n");

    // Test gen_uptime
    len = gen_uptime(buf, sizeof(buf));
    assert(strstr(buf, "10000.00 0.00") != NULL);
    printf("PASS: gen_uptime\n");

    // Test gen_cmdline
    len = gen_cmdline(buf, sizeof(buf));
    assert(strstr(buf, "root=/dev/hda1") != NULL);
    // gen_cmdline ensures newline
    // "root=/dev/hda1" len is 14.
    // If buf_len is large enough, it copies string.
    // Wait, gen_cmdline logic:
    // cmdline_get(buf, size);
    // size_t len = strlen(buf);
    // if (len < size - 1) { buf[len] = '\n'; buf[len + 1] = '\0'; len++; }
    // So output is "root=/dev/hda1\n".
    assert(buf[len-1] == '\n');
    printf("PASS: gen_cmdline\n");

    // Test gen_loadavg
    len = gen_loadavg(buf, sizeof(buf));
    // 0.50 1.00 1.50 5/50 1234
    assert(strstr(buf, "0.50 1.00 1.50 5/50 1234") != NULL);
    printf("PASS: gen_loadavg\n");

    // Test proc_pmap_stats_read
    len = proc_pmap_stats_read(buf, sizeof(buf));
    assert(strstr(buf, "Faults: 100") != NULL);
    assert(strstr(buf, "Total PMAPs: 10") != NULL);
    printf("PASS: proc_pmap_stats_read\n");

    // Test gen_filesystems
    len = gen_filesystems(buf, sizeof(buf));
    assert(strstr(buf, "\text2") != NULL);
    assert(strstr(buf, "nodev\tprocfs") != NULL);
    printf("PASS: gen_filesystems\n");

    printf("All tests passed!\n");
    return 0;
}
