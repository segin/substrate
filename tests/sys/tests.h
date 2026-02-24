#ifndef _TESTS_H
#define _TESTS_H

#include <stdbool.h>

void run_kernel_tests(void);
void run_vm_expanded_tests(void);
void run_vm_map_tests(void);
void run_vm_map_benchmark(void);
void run_vm_object_tests(void);
void run_vm_fault_tests(void);
void run_vm_cow_tests(void);
void run_vm_pager_tests(void);
void run_vm_policy_tests(void);
void run_signal_tests(void);
void run_udf_write_tests(void);
void run_uma_tests(void);
void run_string_tests(void);
void run_crc32_tests(void);
void run_kthread_create_tests(void);
bool run_div64_tests(void);
void run_kobject_tests(void);
void run_nanosleep_tests(void);
void run_ldt_tests(void);
void run_reboot_tests(void);
void run_spinlock_tests(void);

#endif
