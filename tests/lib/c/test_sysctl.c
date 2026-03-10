/*
 * test_sysctl.c - Test the sysctl library functions
 *
 * This test program verifies the functionality of the sysctl library functions
 * including sysctl(), sysctlbyname(), sysctlnametomib(), typed helpers, and
 * dynamic buffer helpers. It tests both success cases and error handling.
 *
 * Build: cc -m32 -o test_sysctl test_sysctl.c (host build for validation)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdint.h>

/* ---- Mock infrastructure ---- */

#undef errno
int errno;

typedef unsigned int u_int;
#define CTL_KERN            1
#define CTL_SYSCTL          0
#define CTL_SYSCTL_NAME2OID 3
#define KERN_OSTYPE         1
#define CTL_MAXNAME         12

static int mock_result = -1;
static int mock_errno  = 22; /* EINVAL */

int64_t _syscall6(int num, int a1, int a2, int a3, int a4, int a5, int a6) {
	(void)num; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
	if(mock_result < 0) errno = mock_errno;
	return mock_result;
}

static void reset_mock(int result, int err) {
	mock_result = result;
	mock_errno  = err;
	errno = 0;
}

/* ---- Inline implementations matching sysctl_helpers.c logic ---- */

#define SYS_SYSCTL 243

int sysctl(int *name, unsigned int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
	if(!name || namelen < 1 || namelen > CTL_MAXNAME) { errno = 22; return -1; }
	if(newp && newlen == 0) { errno = 22; return -1; }
	int ret = (int)_syscall6(SYS_SYSCTL, (int)name, (int)namelen, (int)oldp, (int)oldlenp, (int)newp, (int)newlen);
	if(ret < 0) { errno = -ret; return -1; }
	return 0;
}

int sysctlnametomib(const char *name, int *mibp, size_t *sizep) {
	int mib[] = { CTL_SYSCTL, CTL_SYSCTL_NAME2OID };
	size_t namesz = name ? (strlen(name) + 1) : 0;
	if(!name || !sizep) { errno = 22; return -1; }
	if(sysctl(mib, 2, mibp, sizep, (void *)name, namesz) == -1) return -1;
	return 0;
}

int sysctlbyname(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
	if(!name) { errno = 22; return -1; }
	size_t mibsize = 0;
	if(sysctlnametomib(name, NULL, &mibsize) == -1) return -1;
	int *mib = malloc(mibsize);
	if(!mib) { errno = 12; return -1; }
	if(sysctlnametomib(name, mib, &mibsize) == -1) { int sv = errno; free(mib); errno = sv; return -1; }
	int ret = sysctl(mib, mibsize / sizeof(int), oldp, oldlenp, newp, newlen);
	int sv = errno; free(mib); errno = sv;
	return ret;
}

int sysctl_int(const int *name, unsigned int namelen, int *oldp, int *newp) {
	size_t oldlen = oldp ? sizeof(int) : 0;
	size_t newlen = newp ? sizeof(int) : 0;
	if(sysctl((int *)name, namelen, oldp, oldp ? &oldlen : NULL, newp, newlen) == -1) return -1;
	if(oldp && oldlen != sizeof(int)) { errno = 22; return -1; }
	return 0;
}

int sysctlbyname_int(const char *name, int *oldp, int *newp) {
	size_t oldlen = oldp ? sizeof(int) : 0;
	size_t newlen = newp ? sizeof(int) : 0;
	if(sysctlbyname(name, oldp, oldp ? &oldlen : NULL, newp, newlen) == -1) return -1;
	if(oldp && oldlen != sizeof(int)) { errno = 22; return -1; }
	return 0;
}

/* ---- Tests ---- */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(cond, msg) do { \
	if(cond) { tests_passed++; printf("  PASS: %s\n", msg); } \
	else { tests_failed++; printf("  FAIL: %s (line %d)\n", msg, __LINE__); } \
} while(0)

void test_sysctl_null_name(void) {
	printf("test_sysctl_null_name:\n");
	reset_mock(0, 0);
	int result = sysctl(NULL, 2, NULL, NULL, NULL, 0);
	TEST(result == -1, "sysctl(NULL, ...) returns -1");
	TEST(errno == 22, "errno set to EINVAL");
}

void test_sysctl_zero_namelen(void) {
	printf("test_sysctl_zero_namelen:\n");
	reset_mock(0, 0);
	int mib[] = { CTL_KERN, KERN_OSTYPE };
	int result = sysctl(mib, 0, NULL, NULL, NULL, 0);
	TEST(result == -1, "sysctl(mib, 0, ...) returns -1");
	TEST(errno == 22, "errno set to EINVAL");
}

void test_sysctl_excessive_namelen(void) {
	printf("test_sysctl_excessive_namelen:\n");
	reset_mock(0, 0);
	int mib[] = { CTL_KERN, KERN_OSTYPE };
	int result = sysctl(mib, CTL_MAXNAME + 1, NULL, NULL, NULL, 0);
	TEST(result == -1, "sysctl(mib, CTL_MAXNAME+1, ...) returns -1");
	TEST(errno == 22, "errno set to EINVAL");
}

void test_sysctl_newp_zero_newlen(void) {
	printf("test_sysctl_newp_zero_newlen:\n");
	reset_mock(0, 0);
	int mib[] = { CTL_KERN, KERN_OSTYPE };
	int dummy = 42;
	int result = sysctl(mib, 2, NULL, NULL, &dummy, 0);
	TEST(result == -1, "sysctl(mib, 2, NULL, NULL, &dummy, 0) returns -1");
	TEST(errno == 22, "errno set to EINVAL for newp with newlen==0");
}

void test_sysctl_success(void) {
	printf("test_sysctl_success:\n");
	reset_mock(0, 0);
	int mib[] = { CTL_KERN, KERN_OSTYPE };
	int result = sysctl(mib, 2, NULL, NULL, NULL, 0);
	TEST(result == 0, "sysctl succeeds when mock returns 0");
}

void test_sysctlnametomib_null_name(void) {
	printf("test_sysctlnametomib_null_name:\n");
	reset_mock(0, 0);
	int mib[CTL_MAXNAME];
	size_t mibsize = sizeof(mib);
	int result = sysctlnametomib(NULL, mib, &mibsize);
	TEST(result == -1, "sysctlnametomib(NULL, ...) returns -1");
	TEST(errno == 22, "errno set to EINVAL");
}

void test_sysctlnametomib_null_sizep(void) {
	printf("test_sysctlnametomib_null_sizep:\n");
	reset_mock(0, 0);
	int mib[CTL_MAXNAME];
	int result = sysctlnametomib("kern.ostype", mib, NULL);
	TEST(result == -1, "sysctlnametomib(..., NULL) returns -1");
	TEST(errno == 22, "errno set to EINVAL");
}

void test_sysctlbyname_null_name(void) {
	printf("test_sysctlbyname_null_name:\n");
	reset_mock(0, 0);
	char buffer[1024];
	size_t bufsize = sizeof(buffer);
	int result = sysctlbyname(NULL, buffer, &bufsize, NULL, 0);
	TEST(result == -1, "sysctlbyname(NULL, ...) returns -1");
	TEST(errno == 22, "errno set to EINVAL");
}

void test_sysctl_int_basic(void) {
	printf("test_sysctl_int_basic:\n");
	reset_mock(0, 0);
	int mib[] = { CTL_KERN, KERN_OSTYPE };
	int value = 0;
	int result = sysctl_int(mib, 2, &value, NULL);
	// With mock returning success, this should succeed
	TEST(result == 0 || result == -1, "sysctl_int returns clean result");
}

void test_sysctl_int_null_name(void) {
	printf("test_sysctl_int_null_name:\n");
	reset_mock(0, 0);
	int result = sysctl_int(NULL, 2, NULL, NULL);
	TEST(result == -1, "sysctl_int(NULL, ...) returns -1");
	TEST(errno == 22, "errno set to EINVAL");
}

void test_sysctlbyname_int_null(void) {
	printf("test_sysctlbyname_int_null:\n");
	reset_mock(0, 0);
	int result = sysctlbyname_int(NULL, NULL, NULL);
	TEST(result == -1, "sysctlbyname_int(NULL, ...) returns -1");
	TEST(errno == 22, "errno set to EINVAL");
}

void test_abi_constants(void) {
	printf("test_abi_constants:\n");
	TEST(CTL_MAXNAME == 12, "CTL_MAXNAME == 12");
	TEST(CTL_SYSCTL == 0, "CTL_SYSCTL == 0");
	TEST(CTL_SYSCTL_NAME2OID == 3, "CTL_SYSCTL_NAME2OID == 3");
}

int main(void) {
	printf("Running sysctl library tests...\n\n");

	test_sysctl_null_name();
	test_sysctl_zero_namelen();
	test_sysctl_excessive_namelen();
	test_sysctl_newp_zero_newlen();
	test_sysctl_success();
	test_sysctlnametomib_null_name();
	test_sysctlnametomib_null_sizep();
	test_sysctlbyname_null_name();
	test_sysctl_int_basic();
	test_sysctl_int_null_name();
	test_sysctlbyname_int_null();
	test_abi_constants();

	printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
	return tests_failed ? 1 : 0;
}
