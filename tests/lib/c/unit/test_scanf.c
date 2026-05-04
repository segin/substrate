#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

// Prefixed symbols
extern int mys_sscanf(const char *str, const char *format, ...);
extern int mys_vsscanf(const char *str, const char *format, va_list ap);
extern int mys_fscanf(FILE *stream, const char *format, ...);
extern FILE *mys_tmpfile(void);
extern int mys_fputs(const char *s, FILE *stream);
extern void mys_rewind(FILE *stream);
extern int mys_fclose(FILE *stream);

void test_scanf_int(void) {
	int a, b;
	int n = mys_sscanf("123 456", "%d %d", &a, &b);
	assert(n == 2);
	assert(a == 123);
	assert(b == 456);
	printf("test_scanf_int passed\n");
}

void test_scanf_hex(void) {
	unsigned int x;
	int n = mys_sscanf("0xdeadbeef", "%x", &x);
	assert(n == 1);
	assert(x == 0xdeadbeef);
	printf("test_scanf_hex passed\n");
}

/* REQ-06-0220: %d, %i, %u, %o, %x integer conversions in one pass. */
void test_scanf_int_conversions(void) {
	int d = 0;
	int i = 0;
	unsigned int u = 0;
	unsigned int o = 0;
	unsigned int x = 0;
	int n = mys_sscanf("-42 0x10 4294967295 17 ff",
	                   "%d %i %u %o %x", &d, &i, &u, &o, &x);
	assert(n == 5);
	assert(d == -42);
	assert(i == 0x10);            /* %i parses 0x prefix as hex */
	assert(u == 4294967295U);     /* %u accepts full unsigned range */
	assert(o == 017);             /* %o parses octal: 17 octal == 15 dec */
	assert(x == 0xff);            /* %x parses hex without 0x prefix */
	printf("test_scanf_int_conversions passed\n");
}

void test_scanf_float(void) {
	float f;
	int n = mys_sscanf("3.14159", "%f", &f);
	assert(n == 1);
	assert(f > 3.14 && f < 3.15);
	printf("test_scanf_float passed\n");
}

/* REQ-06-0222: %f, %e, %g floating-point conversions. */
void test_scanf_float_eg(void) {
	float f = 0.0f;
	float e = 0.0f;
	float g = 0.0f;
	int n = mys_sscanf("3.14159 2.5e3 -1.25E-2",
	                   "%f %e %e", &f, &e, &g);
	assert(n == 3);
	assert(f > 3.14f && f < 3.15f);
	assert(e > 2499.9f && e < 2500.1f);    /* 2.5e3 == 2500 */
	assert(g > -0.0126f && g < -0.0124f);  /* -1.25E-2 == -0.0125 */

	/* %g should accept both fixed and scientific notation. */
	float g1 = 0.0f;
	float g2 = 0.0f;
	int n2 = mys_sscanf("0.5 1.5e2", "%g %g", &g1, &g2);
	assert(n2 == 2);
	assert(g1 > 0.49f && g1 < 0.51f);
	assert(g2 > 149.9f && g2 < 150.1f);
	printf("test_scanf_float_eg passed\n");
}

void test_scanf_string(void) {
	char buf[32];
	int n = mys_sscanf("  Hello World  ", "%s", buf);
	assert(n == 1);
	assert(strcmp(buf, "Hello") == 0);
	printf("test_scanf_string passed\n");
}

void test_scanf_scanset(void) {
	char buf[32];
	int n = mys_sscanf("abc123def", "%[a-z]", buf);
	assert(n == 1);
	assert(strcmp(buf, "abc") == 0);
	printf("test_scanf_scanset passed\n");
}

void test_scanf_i_autobase(void) {
	int a = 0, b = 0, c = 0;
	int n = mys_sscanf("0x20 077 42", "%i %i %i", &a, &b, &c);
	assert(n == 3);
	assert(a == 32);
	assert(b == 63);
	assert(c == 42);
	printf("test_scanf_i_autobase passed\n");
}

void test_scanf_char_width(void) {
	char ch[4] = {0};
	int n = mys_sscanf(" abc", "%3c", ch);
	assert(n == 1);
	assert(ch[0] == ' ');
	assert(ch[1] == 'a');
	assert(ch[2] == 'b');
	printf("test_scanf_char_width passed\n");
}

void test_scanf_scanset_variants(void) {
	char a[32] = {0};
	char b[32] = {0};
	char c[32] = {0};
	int n1 = mys_sscanf("abc123", "%[a-z]", a);
	int n2 = mys_sscanf("abc123", "%[^0-9]", b);
	int n3 = mys_sscanf("]abc]xyz", "%[]abc]", c);
	assert(n1 == 1 && strcmp(a, "abc") == 0);
	assert(n2 == 1 && strcmp(b, "abc") == 0);
	assert(n3 == 1 && strcmp(c, "]abc]") == 0);
	printf("test_scanf_scanset_variants passed\n");
}

void test_scanf_n_and_return(void) {
	int value = 0;
	int consumed = -1;
	int n = mys_sscanf("123xyz", "%d%n", &value, &consumed);
	assert(n == 1);
	assert(value == 123);
	assert(consumed == 3);

	value = 0;
	n = mys_sscanf("x 10", "%d %d", &value, &consumed);
	assert(n == 0);
	printf("test_scanf_n_and_return passed\n");
}

/* REQ-06-0228: field width limits applied to multiple conversions. */
void test_scanf_field_width(void) {
	int a = 0;
	int b = 0;
	char s[8] = {0};
	char c[4] = {0};
	int n = mys_sscanf("12345678 abcdefgh", "%3d%5d %5s %2c",
	                   &a, &b, s, c);
	assert(n == 4);
	assert(a == 123);              /* first 3 digits */
	assert(b == 45678);            /* next 5 digits */
	assert(strcmp(s, "abcde") == 0); /* %5s caps token at 5 chars */
	assert(c[0] == 'f' && c[1] == 'g'); /* %2c reads exactly 2 chars */
	printf("test_scanf_field_width passed\n");
}

/* REQ-06-0229: return value equals count of successful assignments. */
void test_scanf_return_value(void) {
	int a = 0, b = 0, c = 0;
	int n;

	n = mys_sscanf("1 2 3", "%d %d %d", &a, &b, &c);
	assert(n == 3);

	/* Partial match: first conversion succeeds, second fails. */
	a = b = 0;
	n = mys_sscanf("42 oops", "%d %d", &a, &b);
	assert(n == 1);
	assert(a == 42);

	/* %*d suppression does not contribute to the count. */
	a = 0;
	n = mys_sscanf("10 20", "%*d %d", &a);
	assert(n == 1);
	assert(a == 20);
	printf("test_scanf_return_value passed\n");
}

void test_scanf_assignment_suppression_and_width(void) {
	int y = 0;
	char s[8] = {0};
	int n = mys_sscanf("12345 777 hello", "%*d %d %3s", &y, s);
	assert(n == 2);
	assert(y == 777);
	assert(strcmp(s, "hel") == 0);
	printf("test_scanf_assignment_suppression_and_width passed\n");
}

void test_scanf_eof_immediate_failure(void) {
	int x = 0;
	int n = mys_sscanf("", "%d", &x);
	assert(n == EOF);
	printf("test_scanf_eof_immediate_failure passed\n");
}

void test_scanf_fscanf_wrapper(void) {
	FILE *f = mys_tmpfile();
	assert(f != NULL);
	assert(mys_fputs("55 test", f) >= 0);
	mys_rewind(f);

	int v = 0;
	char s[16] = {0};
	int n = mys_fscanf(f, "%d %15s", &v, s);
	assert(n == 2);
	assert(v == 55);
	assert(strcmp(s, "test") == 0);
	assert(mys_fclose(f) == 0);
	printf("test_scanf_fscanf_wrapper passed\n");
}

void test_scanf_length_modifiers(void) {
	signed char hh = 0;
	short h = 0;
	long l = 0;
	long long ll = 0;
	intmax_t j = 0;
	size_t z = 0;
	ptrdiff_t t = 0;

	int n = mys_sscanf("-5 -6 -7 -8 -9 10 -11", "%hhd %hd %ld %lld %jd %zu %td", &hh, &h, &l, &ll, &j, &z, &t);
	assert(n == 7);
	assert(hh == -5);
	assert(h == -6);
	assert(l == -7);
	assert(ll == -8);
	assert(j == -9);
	assert(z == 10);
	assert(t == -11);
	printf("test_scanf_length_modifiers passed\n");
}

int main(void) {
	printf("Running Substrate scanf tests...\n");
	test_scanf_int();
	test_scanf_hex();
	test_scanf_int_conversions();
	test_scanf_float();
	test_scanf_float_eg();
	test_scanf_string();
	test_scanf_scanset();
	test_scanf_i_autobase();
	test_scanf_char_width();
	test_scanf_scanset_variants();
	test_scanf_n_and_return();
	test_scanf_field_width();
	test_scanf_return_value();
	test_scanf_assignment_suppression_and_width();
	test_scanf_eof_immediate_failure();
	test_scanf_fscanf_wrapper();
	test_scanf_length_modifiers();
	printf("All scanf tests passed!\n");
	return 0;
}
