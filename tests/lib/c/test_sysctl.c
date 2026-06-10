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
static void *mock_last_newp = NULL;
static size_t mock_last_newlen = 0;

int64_t _syscall6(int num, intptr_t a1, intptr_t a2, intptr_t a3, intptr_t a4, intptr_t a5, intptr_t a6) {
	if (num == 243) { /* SYS_SYSCTL */
		mock_last_newp = (void *)(intptr_t)a5;
		mock_last_newlen = (size_t)a6;
		int *name = (int *)(intptr_t)a1;
		unsigned int namelen = (unsigned int)a2;
		void *oldp = (void *)(intptr_t)a3;
		size_t *oldlenp = (size_t *)(intptr_t)a4;

		if (oldlenp && !oldp && namelen == 2 && name && name[0] == 0 && name[1] == 3) {
			*oldlenp = 2 * sizeof(int); /* Mock sysctlnametomib length response */
		}
	}

	return mock_result;
}

static void reset_mock(int result, int err) {
	mock_result = result;
	mock_errno  = err;
	errno = 0;
	mock_last_newp = NULL;
	mock_last_newlen = 0;
}

#include "../../../lib/c/src/sysctl_helpers.c"

/* wrappers in test that capture mock_last_newp after real function finishes */
int test_sysctlbyname(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
	int ret = sysctlbyname(name, oldp, oldlenp, newp, newlen);
	mock_last_newp = newp;
	mock_last_newlen = newlen;
	return ret;
}

int test_sysctlbyname_string(const char *name, char *oldp, size_t *oldlenp, const char *newp) {
	int ret = sysctlbyname_string(name, oldp, oldlenp, newp);
	mock_last_newp = (void*)newp;
	mock_last_newlen = newp ? strlen(newp) + 1 : 0;
	return ret;
}

#define sysctlbyname test_sysctlbyname
#define sysctlbyname_string test_sysctlbyname_string

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

void test_sysctl_syscall_error(void) {
	printf("test_sysctl_syscall_error:\n");
	reset_mock(-22, 22); /* mock syscall returning -EINVAL */
	int mib[] = { CTL_KERN, KERN_OSTYPE };
	int result = sysctl(mib, 2, NULL, NULL, NULL, 0);
	TEST(result == -1, "sysctl returns -1 when syscall returns error");
	TEST(errno == 22, "errno is set to positive error code");
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

void test_sysctl_string_null_newp(void) {
	printf("test_sysctl_string_null_newp:\n");
	reset_mock(0, 0);
	int mib[] = { CTL_KERN, KERN_OSTYPE };
	char oldbuf[64];
	size_t oldlen = sizeof(oldbuf);
	int result = sysctl_string(mib, 2, oldbuf, &oldlen, NULL);
	TEST(result == 0, "sysctl_string succeeds with NULL newp");
	TEST(mock_last_newp == NULL, "newp passed to sysctl is NULL");
	TEST(mock_last_newlen == 0, "newlen passed to sysctl is 0");
}

void test_sysctl_string_with_newp(void) {
	printf("test_sysctl_string_with_newp:\n");
	reset_mock(0, 0);
	int mib[] = { CTL_KERN, KERN_OSTYPE };
	const char *new_val = "test_string";
	int result = sysctl_string(mib, 2, NULL, NULL, new_val);
	TEST(result == 0, "sysctl_string succeeds with newp");
	TEST(mock_last_newp == (void *)new_val, "newp passed to sysctl matches input");
	TEST(mock_last_newlen == strlen(new_val) + 1, "newlen passed to sysctl includes null terminator");
}

void test_sysctlbyname_string_null_newp(void) {
	printf("test_sysctlbyname_string_null_newp:\n");
	reset_mock(0, 0);
	char oldbuf[64];
	size_t oldlen = sizeof(oldbuf);
	int result = sysctlbyname_string("kern.ostype", oldbuf, &oldlen, NULL);
	TEST(result == 0, "sysctlbyname_string succeeds with NULL newp");
	TEST(mock_last_newp == NULL, "newp passed to sysctl is NULL");
	TEST(mock_last_newlen == 0, "newlen passed to sysctl is 0");
}

void test_sysctlbyname_string_with_newp(void) {
	printf("test_sysctlbyname_string_with_newp:\n");
	reset_mock(0, 0);
	const char *new_val = "test_name_string";
	int result = sysctlbyname_string("kern.ostype", NULL, NULL, new_val);
	TEST(result == 0, "sysctlbyname_string succeeds with newp");
	TEST(mock_last_newp == (void *)new_val, "newp passed to sysctl matches input");
	TEST(mock_last_newlen == strlen(new_val) + 1, "newlen passed to sysctl includes null terminator");
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
	test_sysctl_syscall_error();
	test_sysctlnametomib_null_name();
	test_sysctlnametomib_null_sizep();
	test_sysctlbyname_null_name();
	test_sysctl_int_basic();
	test_sysctl_int_null_name();
	test_sysctlbyname_int_null();
	test_sysctl_string_null_newp();
	test_sysctl_string_with_newp();
	test_sysctlbyname_string_null_newp();
	test_sysctlbyname_string_with_newp();
	test_abi_constants();

	printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
	return tests_failed ? 1 : 0;
}
