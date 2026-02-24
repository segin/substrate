#include <stdio.h>
#include <stdbool.h>
#include "../sys/vm/vm_zone.h"
#include "../sys/vm/vm_kmem.h"
#include "../sys/vm/vm_object.h"
#include "../sys/vm/vm_page.h"
#include "../sys/kern/sched.h"

// VM Tests
extern bool test_kmem_basic_alloc(void);
extern bool test_kmem_multiple_alloc(void);
extern bool test_kmem_too_large(void);
extern bool test_zone_create_and_alloc(void);
extern bool test_zone_exhaustion(void);
extern bool test_vm_map_init(void);
extern bool test_vm_map_insert_and_find(void);
extern bool test_vm_map_remove(void);
extern bool test_vm_object_lifecycle(void);
extern bool test_vm_object_page_mgmt(void);
extern bool test_vm_page_queue_ops(void);
extern bool test_vm_page_flags(void);
extern bool test_vm_fault_anonymous(void);
extern bool test_vm_fault_protection_violation(void);
extern bool test_mmap_logic(void);
extern bool test_munmap_logic(void);
extern bool test_swap_lifecycle(void); // Mock-based
extern bool test_swap_full(void);      // Mock-based
extern bool test_vm_swap_real_io(void);
extern bool test_vm_swap_real_full(void);
extern bool test_vm_fault_cow_trigger(void);

// Arch Tests (Mocked)
extern bool test_trampoline_preparation(void);

// Kernel Tests
extern bool test_spinlock_basic(void);
extern bool test_spinlock_initial_state(void);

// kthread Tests
extern bool test_kthread_creation(void);
extern bool test_timer_tick_increments(void);
extern bool test_sched_priority(void);
extern bool test_sched_sleep_wakeup(void);
extern bool test_sched_decay_suite(void);

// Sync Tests
extern bool test_mutex_basic(void);
extern bool test_mutex_contention(void);
extern bool test_sema_basic(void);
extern bool test_sema_blocking(void);
extern bool test_futex_basic(void);
extern bool test_futex_blocking(void);
extern bool test_pthread_exit_logic(void);
extern bool test_reboot_permissions(void);

// Sleepq Tests
extern bool test_sleepq_basic(void);
extern bool test_sleepq_fifo(void);
extern bool test_sleepq_wake_all(void);
extern bool test_sleepq_wake_n(void);
extern bool test_sleepq_private(void);
extern bool test_sleepq_collisions(void);
extern bool test_sleepq_requeue(void);

// Signal Tests
extern bool test_signal_action(void);
extern bool test_signal_mask(void);
extern bool test_signal_delivery_default(void);

// VFS Tests
extern bool test_fd_ref_counting(void);
extern bool test_fd_dup2(void);
extern bool test_vfs_permissions_root(void);
extern bool test_vfs_permissions_user(void);
extern bool test_vfs_permissions_group(void);
extern bool test_vfs_chroot_basic(void);
extern bool test_vfs_chroot_effect(void);
extern bool test_vop_readdir_basic(void);
extern bool test_vop_readdir_notdir(void);
extern bool test_vop_link_basic(void);
extern bool test_vop_link_notdir(void);
extern bool test_vop_link_dir_target(void);
extern bool test_vop_link_notsupp(void);
extern bool test_vop_rename_basic(void);
extern bool test_vop_rename_notsupp(void);
extern bool test_vop_rename_bad_mount(void);
extern bool test_vop_symlink_basic(void);
extern bool test_vop_symlink_notdir(void);
extern bool test_vop_symlink_notsupp(void);
extern bool test_vop_readlink_basic(void);
extern bool test_vop_readlink_notlink(void);
extern bool test_vop_readlink_notsupp(void);

// FUSE Tests
extern bool test_fuse_read(void);

// Scheduling Properties & Fuzzing
extern bool prop_time_is_monotonic(int iterations);
extern bool prop_realtime_preempts_timeshare(void);
extern bool prop_sleep_wakeup_consistency(void);
extern void fuzz_timer_interrupt(const uint8_t *data, size_t size);
extern void fuzz_sched_priority(const uint8_t *data, size_t size);
bool test_sched_properties(void) {
    return prop_time_is_monotonic(1000) && 
           prop_realtime_preempts_timeshare() &&
           prop_sleep_wakeup_consistency();
}

bool test_sched_fuzz(void) {
    uint8_t dummy_data[] = {0x10, 0x00, 0x00, 0x00}; // 16 ticks
    fuzz_timer_interrupt(dummy_data, sizeof(dummy_data));
    
    uint8_t prio_data[] = {0x01, 0x00, 0x40, 0x00}; // tid 1, class 0, prio 64
    fuzz_sched_priority(prio_data, sizeof(prio_data));
    
    return true; // If it didn't crash, it passed for now
}

// Personality Tests
extern bool test_svr3_personality_table(void);
extern bool test_svr4_personality_table(void);

// LibC Tests
extern bool test_libc_strlen(void);
extern bool test_gmtime_negative_years(void);
extern bool test_libc_time(void);
extern bool test_libc_memmove(void);
extern bool test_libc_strcat(void);
extern bool test_libc_strtok(void);

// Div64 Tests
extern bool run_div64_tests(void);

// Pipe Tests
extern int test_pipe_race(void);
bool run_pipe_race(void) {
    return test_pipe_race() == 0;
}

typedef struct {
    const char *name;
    bool (*func)(void);
} test_case_t;

test_case_t tests[] = {
    {"kmem_basic", test_kmem_basic_alloc},
    {"kmem_multi", test_kmem_multiple_alloc},
    {"kmem_large", test_kmem_too_large},
    {"zone_basic", test_zone_create_and_alloc},
    {"zone_exhaust", test_zone_exhaustion},
    {"map_init", test_vm_map_init},
    {"map_insert", test_vm_map_insert_and_find},
    {"map_remove", test_vm_map_remove},
    {"object_life", test_vm_object_lifecycle},
    {"object_page", test_vm_object_page_mgmt},
    {"page_queues", test_vm_page_queue_ops},
    {"page_flags", test_vm_page_flags},
    {"fault_anon", test_vm_fault_anonymous},
    {"fault_prot", test_vm_fault_protection_violation},
    {"mmap_logic", test_mmap_logic},
    {"munmap_logic", test_munmap_logic},
    {"swap_mock_life", test_swap_lifecycle},
    {"swap_mock_full", test_swap_full},
    {"swap_real_io", test_vm_swap_real_io},
    {"swap_real_full", test_vm_swap_real_full},
    {"cow_trigger", test_vm_fault_cow_trigger},
    {"smp_tramp", test_trampoline_preparation},
    {"lock_basic", test_spinlock_basic},
    {"lock_init", test_spinlock_initial_state},
    {"kthread_create", test_kthread_creation},
    {"timer_tick", test_timer_tick_increments},
    {"sched_priority", test_sched_priority},
    {"sched_sleep", test_sched_sleep_wakeup},
    {"sched_decay", test_sched_decay_suite},
    {"mutex_basic", test_mutex_basic},
    {"mutex_contend", test_mutex_contention},
    {"sema_basic", test_sema_basic},
    {"sema_block", test_sema_blocking},
    {"futex_basic", test_futex_basic},
    {"futex_block", test_futex_blocking},
    {"pthread_exit", test_pthread_exit_logic},
    {"reboot_perm", test_reboot_permissions},
    {"sleepq_basic", test_sleepq_basic},
    {"sleepq_fifo", test_sleepq_fifo},
    {"sleepq_wake_all", test_sleepq_wake_all},
    {"sleepq_wake_n", test_sleepq_wake_n},
    {"sleepq_private", test_sleepq_private},
    {"sleepq_collis", test_sleepq_collisions},
    {"sleepq_requeue", test_sleepq_requeue},
    {"sig_action", test_signal_action},
    {"sig_mask", test_signal_mask},
    {"sig_deliver", test_signal_delivery_default},
    {"fd_refcnt", test_fd_ref_counting},
    {"fd_dup2", test_fd_dup2},
    {"vfs_perm_root", test_vfs_permissions_root},
    {"vfs_perm_user", test_vfs_permissions_user},
    {"vfs_perm_group", test_vfs_permissions_group},
    {"vfs_chroot_basic", test_vfs_chroot_basic},
    {"vfs_chroot_effect", test_vfs_chroot_effect},
    {"vfs_readdir_basic", test_vop_readdir_basic},
    {"vfs_readdir_notdir", test_vop_readdir_notdir},
    { "vfs_link_basic", test_vop_link_basic },
    { "vfs_link_notdir", test_vop_link_notdir },
    { "vfs_link_dir_target", test_vop_link_dir_target },
    { "vfs_link_notsupp", test_vop_link_notsupp },
    { "vfs_rename_basic", test_vop_rename_basic },
    { "vfs_rename_notsupp", test_vop_rename_notsupp },
    { "vfs_rename_bad_mount", test_vop_rename_bad_mount },
    { "vfs_symlink_basic", test_vop_symlink_basic },
    { "vfs_symlink_notdir", test_vop_symlink_notdir },
    { "vfs_symlink_notsupp", test_vop_symlink_notsupp },
    { "vfs_readlink_basic", test_vop_readlink_basic },
    { "vfs_readlink_notlink", test_vop_readlink_notlink },
    { "vfs_readlink_notsupp", test_vop_readlink_notsupp },
    {"fuse_read", test_fuse_read},
    {"sched_prop", test_sched_properties},
    {"sched_fuzz", test_sched_fuzz},
    {"svr3_perso", test_svr3_personality_table},
    {"svr4_perso", test_svr4_personality_table},
    {"libc_strlen", test_libc_strlen},
    {"libc_time_neg", test_gmtime_negative_years},
    {"libc_time", test_libc_time},
    {"libc_memmove", test_libc_memmove},
    {"libc_strcat", test_libc_strcat},
    {"libc_strtok", test_libc_strtok},
    {"div64", run_div64_tests},
    {"pipe_race", run_pipe_race},
    {NULL, NULL}
};

#include <string.h>
extern void sleepq_init(void);

int main(int argc, char **argv) {
    const char *target_test = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--test") == 0 && i + 1 < argc) {
            target_test = argv[i+1];
            i++;
        }
    }

    int passed = 0;
    int total = 0;

    // Initialize subsystems
    vm_object_init();
    vm_page_init();
    sleepq_init();
    sched_init();

    printf("Starting Substrate Unit Tests%s%s%s...\n", 
           target_test ? " (Target: " : "",
           target_test ? target_test : "",
           target_test ? ")" : "");
    printf("--------------------------------\n");

    for (int i = 0; tests[i].name != NULL; i++) {
        if (target_test && strcmp(tests[i].name, target_test) != 0) {
            continue;
        }
        total++;
        printf("[%02d] Testing %-20s ... ", i + 1, tests[i].name);
        if (tests[i].func()) {
            printf("PASS\n");
            passed++;
        } else {
            printf("FAIL\n");
        }
    }

    printf("--------------------------------\n");
    if (total == 0 && target_test) {
        printf("Error: Test '%s' not found!\n", target_test);
        return 1;
    }
    printf("Result: %d/%d passed.\n", passed, total);
    return (passed == total) ? 0 : 1;
}
