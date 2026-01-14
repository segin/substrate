#!/bin/bash
set -e

cat <<EOF > sys/tests/host_runner.c
#include <stddef.h> 
#include <stdint.h>
#include <sys/proc.h> 

thread_t *current_thread = NULL;
process_t *current_process = NULL;
thread_t mock_thread_storage;

// Manual declarations to avoid including host stdlib/stdio which conflict with kernel headers
extern int printf(const char *format, ...);
extern int fflush(void *stream);
extern void *stdout;
extern void exit(int status);

void test_wait_logic(void);

int main() {
    // Setup globals
    current_thread = &mock_thread_storage;
    current_thread->sig_pending = 0;
    current_thread->sig_mask = 0;

    printf("Running wait logic tests on host...\n");
    test_wait_logic();
    printf("Success!\n");
    return 0;
}

void kprint(const char* s) { 
    printf("%s", s); 
    fflush(stdout); 
}

void panic(const char* s) { 
    printf("PANIC: %s\n", s); 
    fflush(stdout);
    exit(1); 
}
EOF

# Compile
# Use -fno-builtin to avoid compiler inserting builtin calls that might conflict
gcc -m32 -g -o wait_test_host sys/tests/host_runner.c sys/tests/test_wait_logic.c sys/pm/wait.c \
    -I sys/include -I sys/pm -I sys/tests -I sys -I sys/include/sys \
    -DHOST_TEST \
    -Wno-incompatible-pointer-types -Wno-int-conversion -Wno-implicit-function-declaration \
    -fno-builtin

./wait_test_host
rm sys/tests/host_runner.c wait_test_host
