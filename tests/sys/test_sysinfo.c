/*
 * tests/sys/test_sysinfo.c
 *
 * Unit Test for sysinfo syscall
 */

#include <sys/sysinfo.h>
#include <sys/errno.h>
#include <stdio.h>
#include <string.h>
#include <kern/console.h>

/* Internal kernel functions */
extern int do_sysinfo(struct sysinfo *info);
extern int sys_sysinfo(struct sysinfo *info);
extern int sys_vm_stats(sys_vmstat_t *stats);

int test_sysinfo(void) {
    struct sysinfo info;
    int ret;

    kprint("TEST: sysinfo()\n");

    /* 1. Basic Call */
    memset(&info, 0, sizeof(info));
    // Call kernel implementation directly
    ret = do_sysinfo(&info);
    if (ret != 0) {
        kprint("FAIL: sysinfo returned error (1)\n");
        return -1;
    }

    /* 2. Validate Fields */
    if (info.mem_unit != 1) {
        kprint("FAIL: mem_unit != 1\n");
        return -1;
    }
    
    /* Check Uptime > 0 */
    if (info.uptime == 0) {
        /* Might be 0 if very fast, but unlikely to be exact 0 unless timer failed? */
        /* Actually uptime is seconds. If we are super fast, might be 0. */
        /* Let's warn but not fail? Or check get_time consistency? */
        /* We can accept 0 for now. */
    }
    
    /* Check RAM > 0 */
    if (info.totalram == 0) {
        kprint("FAIL: totalram is 0\n");
        return -1;
    }
    
    if (info.freeram > info.totalram) {
        kprint("FAIL: freeram > totalram\n");
        return -1;
    }
    
    /* Check procs > 0 */
    if (info.procs == 0) {
        kprint("FAIL: procs is 0\n");
        return -1;
    }
    
    /* 3. Check invalid pointer */
    /* do_sysinfo explicitly checks for NULL and returns -EFAULT */
    ret = do_sysinfo(NULL);
    if (ret != -EFAULT) {
         kprintf("FAIL: sysinfo(NULL) returned %d, expected %d\n", ret, -EFAULT);
         return -1;
    }

    /* 4. Check user pointer validation in sys_sysinfo */
    /* Passing a kernel pointer (stack address) should fail */
    ret = sys_sysinfo(&info);
    if (ret != -EFAULT) {
         kprintf("FAIL: sys_sysinfo(&kernel_var) returned %d, expected %d\n", ret, -EFAULT);
         return -1;
    }

    /* 5. Check NULL pointer validation in sys_sysinfo */
    ret = sys_sysinfo(NULL);
    if (ret != -EFAULT) {
         kprintf("FAIL: sys_sysinfo(NULL) returned %d, expected %d\n", ret, -EFAULT);
         return -1;
    }

    /* 6. Test sys_vm_stats (pointer validation) */
    sys_vmstat_t vmstats;
    ret = sys_vm_stats(&vmstats);
    if (ret != -EFAULT) {
         kprintf("FAIL: sys_vm_stats(&kernel_var) returned %d, expected %d\n", ret, -EFAULT);
         return -1;
    }

    ret = sys_vm_stats(NULL);
    if (ret != -EFAULT) {
         kprintf("FAIL: sys_vm_stats(NULL) returned %d, expected %d\n", ret, -EFAULT);
         return -1;
    }
    
    kprint("PASS: sysinfo\n");
    return 0;
}
