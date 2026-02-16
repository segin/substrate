#include <pthread.h>
#include <stdio.h>
#include <stdint.h>

void *thread_main(void *arg) {
    uintptr_t val = (uintptr_t)arg;
    pthread_exit((void *)(val * 2));
    return NULL;
}

int main() {
    pthread_t t1;
    void *retval;

    printf("Starting thread...\n");
    if (pthread_create(&t1, NULL, thread_main, (void *)21) != 0) {
        printf("Failed to create thread\n");
        return 1;
    }

    printf("Joining thread %d...\n", t1);
    if (pthread_join(t1, &retval) != 0) {
        printf("Failed to join thread\n");
        return 1;
    }

    if ((uintptr_t)retval == 42) {
        printf("Test Passed: retval = 42\n");
    } else {
        printf("Test Failed: retval = %p (expected %p)\n", retval, (void*)42);
        return 1;
    }

    return 0;
}
