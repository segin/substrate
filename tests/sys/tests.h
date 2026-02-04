#ifndef _TESTS_H
#define _TESTS_H

void run_kernel_tests(void);
void run_vm_expanded_tests(void);
void run_vm_map_tests(void);
void run_vm_object_tests(void);
void run_vm_fault_tests(void);
void run_vm_cow_tests(void);
void run_vm_pager_tests(void);
void run_vm_policy_tests(void);
void run_signal_tests(void);
void run_udf_write_tests(void);
void run_string_tests(void);
void run_kthread_create_tests(void);

#endif
