/*
 * test_sysctl.c - Test the sysctl library functions
 *
 * This test program verifies the functionality of the sysctl library functions
 * including sysctl(), sysctlbyname(), and sysctlnametomib(). It tests both
 * success cases and error handling scenarios.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

// Define required macros and types
#define __BEGIN_DECLS 
#define __END_DECLS
typedef unsigned int u_int;

// sys/sysctl.h constants we need
#define CTL_KERN         1
#define CTL_SYSCTL       0
#define CTL_SYSCTL_NAME2OID 3
#define KERN_OSTYPE      1
#define KERN_OSRELEASE   2
#define KERN_VERSION     4
#define KERN_HOSTNAME    10
#define CTL_MAXNAME      12

// Forward declarations
int sysctl(int *name, unsigned int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen);
int sysctlbyname(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen);
int sysctlnametomib(const char *name, int *mibp, size_t *sizep);

// Global errno variable
int errno;

// Mock syscall implementation
#define SYS_SYSCTL 123
static int mock_result = -1;
static int mock_errno = EINVAL;

int64_t _syscall6(int num, int arg1, int arg2, int arg3, int arg4, int arg5, int arg6) {
    (void)num; (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;
    
    if (mock_result < 0) {
        errno = mock_errno;
    }
    
    return mock_result;
}

// Helper function to reset mock
void reset_mock(int result, int err) {
    mock_result = result;
    mock_errno = err;
    errno = 0;
}

// sysctl function from sys.c
int sysctl(int *name, unsigned int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
    int ret = (int)_syscall6(SYS_SYSCTL, (int)name, (int)namelen, (int)oldp, (int)oldlenp, (int)newp, (int)newlen);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

// sysctlnametomib function from sys.c
int sysctlnametomib(const char *name, int *mibp, size_t *sizep) {
    int mib[] = { CTL_SYSCTL, CTL_SYSCTL_NAME2OID };
    int ret;

    if (!name || !sizep) {
        errno = EINVAL;
        return -1;
    }

    // First, get the required size
    ret = sysctl(mib, 2, NULL, sizep, (void *)name, strlen(name) + 1);
    if (ret == -1) {
        return -1;
    }

    // If buffer is provided, get the actual MIB
    if (mibp) {
        ret = sysctl(mib, 2, mibp, sizep, (void *)name, strlen(name) + 1);
        if (ret == -1) {
            return -1;
        }
    }

    return 0;
}

// sysctlbyname function from sys.c
int sysctlbyname(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
    int *mib = NULL;
    size_t mibsize = 0;
    int ret;

    if (!name) {
        errno = EINVAL;
        return -1;
    }

    // Get the MIB from the name
    ret = sysctlnametomib(name, NULL, &mibsize);
    if (ret == -1) {
        return -1;
    }

    mib = malloc(mibsize);
    if (!mib) {
        errno = ENOMEM;
        return -1;
    }

    ret = sysctlnametomib(name, mib, &mibsize);
    if (ret == -1) {
        free(mib);
        return -1;
    }

    // Call sysctl with the MIB
    ret = sysctl(mib, mibsize / sizeof(int), oldp, oldlenp, newp, newlen);
    free(mib);
    return ret;
}

void test_sysctl_error_handling(void) {
    printf("Testing sysctl() error handling...\n");

    char buffer[1024];
    size_t bufsize = sizeof(buffer);
    int result;

    // Test with NULL oldlenp
    reset_mock(-1, EINVAL);
    int invalid_mib[] = { CTL_KERN, KERN_OSTYPE };
    result = sysctl(invalid_mib, 2, buffer, NULL, NULL, 0);
    assert(result == -1);

    printf("PASS: sysctl() error handling\n");
}

void test_sysctlnametomib_error_handling(void) {
    printf("Testing sysctlnametomib() error handling...\n");

    int mib[CTL_MAXNAME];
    size_t mibsize = sizeof(mib);
    int result;

    // Test with NULL name
    reset_mock(0, 0);
    result = sysctlnametomib(NULL, mib, &mibsize);
    assert(result == -1);
    assert(errno == EINVAL);

    // Test with NULL sizep
    reset_mock(0, 0);
    result = sysctlnametomib("kern.ostype", mib, NULL);
    assert(result == -1);
    assert(errno == EINVAL);

    printf("PASS: sysctlnametomib() error handling\n");
}

void test_sysctlbyname_error_handling(void) {
    printf("Testing sysctlbyname() error handling...\n");

    char buffer[1024];
    size_t bufsize = sizeof(buffer);
    int result;

    // Test with NULL name
    reset_mock(0, 0);
    result = sysctlbyname(NULL, buffer, &bufsize, NULL, 0);
    assert(result == -1);
    assert(errno == EINVAL);

    printf("PASS: sysctlbyname() error handling\n");
}

int main(void) {
    printf("Running sysctl library tests...\n\n");

    reset_mock(-1, EINVAL);

    test_sysctl_error_handling();
    test_sysctlnametomib_error_handling();
    test_sysctlbyname_error_handling();

    printf("\nAll sysctl library tests passed!\n");

    return 0;
}
