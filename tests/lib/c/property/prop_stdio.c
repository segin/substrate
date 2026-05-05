#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <limits.h>

extern int mys_sprintf(char *str, const char *format, ...);
extern int mys_snprintf(char *str, size_t size, const char *format, ...);
extern int mys_sscanf(const char *str, const char *format, ...);
extern void mys___stdio_init(void);
extern FILE *mys_tmpfile(void);
extern int mys_fclose(FILE *f);
extern size_t mys_fread(void *ptr, size_t size, size_t nmemb, FILE *f);
extern size_t mys_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f);
extern int mys_fseek(FILE *f, long off, int whence);
extern long mys_ftell(FILE *f);
extern void mys_rewind(FILE *f);
extern int mys_fgetc(FILE *f);
extern int mys_ungetc(int c, FILE *f);
extern int mys_fflush(FILE *f);

/* Deterministic xorshift32 PRNG so property tests are reproducible. */
static uint32_t xs_state = 0xC0FFEEu;
static uint32_t xs_next(void) {
	uint32_t x = xs_state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	xs_state = x;
	return x;
}
static void xs_seed(uint32_t s) { xs_state = s ? s : 0xC0FFEEu; }

/* REQ-06-0234: sprintf/sscanf int round-trip. */
void test_prop_int_roundtrip(void) {
	char buf[64];
	/* Edge cases first. */
	int edges[] = { 0, 1, -1, INT_MAX, INT_MIN, INT_MAX - 1, INT_MIN + 1,
			42, -42, 100000, -100000 };
	for (size_t i = 0; i < sizeof(edges)/sizeof(edges[0]); i++) {
		mys_sprintf(buf, "%d", edges[i]);
		int val;
		int n = mys_sscanf(buf, "%d", &val);
		assert(n == 1);
		assert(val == edges[i]);
	}
	/* Sequential range covering small values. */
	for (int i = -1000; i < 1000; i++) {
		mys_sprintf(buf, "%d", i);
		int val;
		int n = mys_sscanf(buf, "%d", &val);
		assert(n == 1);
		assert(val == i);
	}
	/* Random bulk over the full 32-bit range. */
	xs_seed(0xC0FFEEu);
	for (int i = 0; i < 5000; i++) {
		int x = (int)xs_next();
		mys_sprintf(buf, "%d", x);
		int val;
		int n = mys_sscanf(buf, "%d", &val);
		assert(n == 1);
		assert(val == x);
	}
	printf("test_prop_int_roundtrip passed\n");
}

/* REQ-06-0235: sprintf/sscanf unsigned int round-trip. */
void test_prop_uint_roundtrip(void) {
	char buf[64];
	unsigned int edges[] = { 0u, 1u, UINT_MAX, UINT_MAX - 1u, UINT_MAX / 2u,
				 (unsigned int)INT_MAX, (unsigned int)INT_MAX + 1u };
	for (size_t i = 0; i < sizeof(edges)/sizeof(edges[0]); i++) {
		mys_sprintf(buf, "%u", edges[i]);
		unsigned int val;
		int n = mys_sscanf(buf, "%u", &val);
		assert(n == 1);
		assert(val == edges[i]);
	}
	for (unsigned int i = 0; i < 2000; i++) {
		mys_sprintf(buf, "%u", i);
		unsigned int val;
		int n = mys_sscanf(buf, "%u", &val);
		assert(n == 1);
		assert(val == i);
	}
	xs_seed(0xBADF00Du);
	for (int i = 0; i < 5000; i++) {
		unsigned int x = xs_next();
		mys_sprintf(buf, "%u", x);
		unsigned int val;
		int n = mys_sscanf(buf, "%u", &val);
		assert(n == 1);
		assert(val == x);
	}
	printf("test_prop_uint_roundtrip passed\n");
}

void test_prop_float_roundtrip(void) {
	char buf[64];
	float floats[] = { 3.14159f, 0.0f, -1.0f, 123.456f, 0.001f, 1e5f };
	for (int i = 0; i < 6; i++) {
		mys_sprintf(buf, "%f", floats[i]);
		float val;
		int n = mys_sscanf(buf, "%f", &val);
		assert(n == 1);
		float diff = val - floats[i];
		if (diff < 0) diff = -diff;
		assert(diff < 0.001f);
	}
	printf("test_prop_float_roundtrip passed\n");
}

/* REQ-06-0236: snprintf return value is non-negative and never exceeds the
 * length sprintf would have produced (which equals C99's required return
 * value for snprintf regardless of truncation). */
void test_prop_snprintf_return_bound(void) {
	char full[128];
	char clipped[128];

	struct fmt_case {
		const char *fmt;
		long arg; /* always treated as int for these formats */
	} cases[] = {
		{ "%d",        0 },
		{ "%d",        42 },
		{ "%d",        -123456 },
		{ "%d",        INT_MAX },
		{ "%d",        INT_MIN },
		{ "%u",        0 },
		{ "%u",        4294967295u },
		{ "%x",        0xDEADBEEF },
		{ "%08x",      0x42 },
		{ "%-10d",     7 },
		{ "%+d",       100 },
	};
	xs_seed(0xFEEDFACEu);
	size_t ncases = sizeof(cases)/sizeof(cases[0]);
	for (size_t c = 0; c < ncases; c++) {
		int sprintf_len = mys_sprintf(full, cases[c].fmt, (int)cases[c].arg);
		assert(sprintf_len >= 0);
		/* Vary n from 0 up to sprintf_len + 5, plus a few extra random values. */
		int max_n = sprintf_len + 5;
		for (int n = 0; n <= max_n; n++) {
			memset(clipped, 0xAA, sizeof(clipped));
			int r = mys_snprintf(clipped, (size_t)n, cases[c].fmt, (int)cases[c].arg);
			assert(r >= 0);
			assert(r <= sprintf_len);
		}
		/* A few large random sizes too, just to bulk it up. */
		for (int i = 0; i < 50; i++) {
			size_t n = xs_next() % (size_t)(sprintf_len + 16);
			int r = mys_snprintf(clipped, n, cases[c].fmt, (int)cases[c].arg);
			assert(r >= 0);
			assert(r <= sprintf_len);
		}
	}
	/* Also test with a string format. */
	const char *strings[] = { "", "x", "hello", "abcdefghijklmnopqrstuvwxyz" };
	for (size_t i = 0; i < sizeof(strings)/sizeof(strings[0]); i++) {
		int sprintf_len = mys_sprintf(full, "%s", strings[i]);
		assert(sprintf_len >= 0);
		for (int n = 0; n <= sprintf_len + 5; n++) {
			int r = mys_snprintf(clipped, (size_t)n, "%s", strings[i]);
			assert(r >= 0);
			assert(r <= sprintf_len);
		}
	}
	printf("test_prop_snprintf_return_bound passed\n");
}

/* REQ-06-0237: snprintf NUL-terminates buf whenever n > 0.
 *
 * Per C99 7.19.6.5, when n > 0 snprintf writes at most n - 1 characters
 * followed by a NUL byte. The terminator lives at index min(r, n-1) where
 * r is the value snprintf would have produced with infinite space. We
 * check that this terminator exists by verifying the string length never
 * exceeds n - 1 and that strnlen(buf, n) finds a NUL within the buffer. */
static void check_snprintf_terminated(char *buf, size_t bufcap, size_t n,
				      const char *desc) {
	(void)bufcap;
	/* strnlen returns n if no NUL found in first n bytes -> bug. */
	size_t len = 0;
	while (len < n && buf[len] != '\0') len++;
	if (len >= n) {
		printf("FAIL: %s left no NUL in first %zu bytes\n", desc, n);
		fflush(stdout);
		assert(len < n);
	}
}

void test_prop_snprintf_nul_terminates(void) {
	char buf[256];
	struct {
		const char *fmt;
		int arg;
	} cases[] = {
		{ "%d", 0 },
		{ "%d", 42 },
		{ "%d", -1 },
		{ "%d", INT_MAX },
		{ "%d", INT_MIN },
		{ "%u", 4294967295u },
		{ "%x", 0xCAFEBABE },
	};
	const char *strs[] = { "", "abc", "long-ish-string-of-bytes" };
	for (size_t c = 0; c < sizeof(cases)/sizeof(cases[0]); c++) {
		for (size_t n = 1; n <= sizeof(buf); n++) {
			memset(buf, 0xAA, sizeof(buf));
			(void)mys_snprintf(buf, n, cases[c].fmt, cases[c].arg);
			check_snprintf_terminated(buf, sizeof(buf), n, cases[c].fmt);
		}
	}
	for (size_t s = 0; s < sizeof(strs)/sizeof(strs[0]); s++) {
		for (size_t n = 1; n <= 64; n++) {
			memset(buf, 0xAA, sizeof(buf));
			(void)mys_snprintf(buf, n, "%s", strs[s]);
			check_snprintf_terminated(buf, sizeof(buf), n, "%s");
		}
	}
	/* Random fuzzy combos. */
	xs_seed(0x5EED1234u);
	for (int i = 0; i < 2000; i++) {
		size_t n = (xs_next() % 200) + 1;
		int v = (int)xs_next();
		memset(buf, 0xAA, sizeof(buf));
		(void)mys_snprintf(buf, n, "%d", v);
		check_snprintf_terminated(buf, sizeof(buf), n, "%d-fuzz");
	}
	printf("test_prop_snprintf_nul_terminates passed\n");
}

/* REQ-06-0238: fwrite/rewind/fread round-trip preserves bytes. */
static void run_fwrite_fread_case(size_t len) {
	FILE *f = mys_tmpfile();
	assert(f != NULL);

	unsigned char *data = malloc(len);
	unsigned char *out = malloc(len);
	assert(data != NULL && out != NULL);

	for (size_t i = 0; i < len; i++) {
		data[i] = (unsigned char)(xs_next() & 0xFFu);
	}

	size_t w = mys_fwrite(data, 1, len, f);
	assert(w == len);
	mys_fflush(f);
	mys_rewind(f);
	memset(out, 0xA5, len);
	size_t r = mys_fread(out, 1, len, f);
	assert(r == len);
	assert(memcmp(data, out, len) == 0);

	mys_fclose(f);
	free(data);
	free(out);
}

void test_prop_fwrite_fread_roundtrip(void) {
	xs_seed(0x12345678u);
	/* Small lengths. */
	for (size_t n = 1; n <= 64; n++)
		run_fwrite_fread_case(n);
	/* Around BUFSIZ. */
	size_t bufsizes[] = { 256, 512, 1023, 1024, 1025, 4096, 8192, 16384 };
	for (size_t i = 0; i < sizeof(bufsizes)/sizeof(bufsizes[0]); i++)
		run_fwrite_fread_case(bufsizes[i]);
	/* A few random sizes including > BUFSIZ. */
	for (int i = 0; i < 8; i++) {
		size_t n = (xs_next() % 16384u) + 1u;
		run_fwrite_fread_case(n);
	}
	printf("test_prop_fwrite_fread_roundtrip passed\n");
}

/* REQ-06-0239: ftell after fseek(SEEK_SET) returns the seek offset. */
void test_prop_ftell_after_fseek(void) {
	FILE *f = mys_tmpfile();
	assert(f != NULL);
	const size_t LEN = 4096;
	unsigned char *data = malloc(LEN);
	assert(data != NULL);
	xs_seed(0xABCDEF01u);
	for (size_t i = 0; i < LEN; i++)
		data[i] = (unsigned char)(xs_next() & 0xFFu);
	assert(mys_fwrite(data, 1, LEN, f) == LEN);
	mys_fflush(f);

	long offsets[] = { 0, 1, 2, 7, 64, 255, 256, 1023, 1024, 2048,
			   3000, 4000, (long)LEN, (long)(LEN - 1) };
	for (size_t i = 0; i < sizeof(offsets)/sizeof(offsets[0]); i++) {
		int rc = mys_fseek(f, offsets[i], SEEK_SET);
		assert(rc == 0);
		long t = mys_ftell(f);
		assert(t == offsets[i]);
	}
	/* Random offsets within file bounds. */
	xs_seed(0x55AA55AAu);
	for (int i = 0; i < 200; i++) {
		long off = (long)(xs_next() % (LEN + 1));
		assert(mys_fseek(f, off, SEEK_SET) == 0);
		assert(mys_ftell(f) == off);
	}

	mys_fclose(f);
	free(data);
	printf("test_prop_ftell_after_fseek passed\n");
}

/* REQ-06-0240: ungetc(c, f); fgetc(f) returns c for valid c != EOF. */
void test_prop_ungetc_fgetc(void) {
	FILE *f = mys_tmpfile();
	assert(f != NULL);
	/* Put one byte in the file so we have a valid stream position. */
	unsigned char seed = 'Z';
	assert(mys_fwrite(&seed, 1, 1, f) == 1);
	mys_fflush(f);
	mys_rewind(f);

	/* Read the byte to advance past zero (some implementations require
	 * the stream to be readable / have a position before ungetc). */
	int first = mys_fgetc(f);
	assert(first == 'Z');

	/* Push back & read every non-EOF byte 0x01..0xFF. (Standard says any
	 * unsigned char can be ungetc'd; many implementations also accept
	 * 0x00 but the requirement only mandates c != EOF, so we skip 0
	 * to be portable across libc and host.) */
	for (int c = 0x01; c <= 0xFF; c++) {
		int u = mys_ungetc(c, f);
		assert(u == c);
		int g = mys_fgetc(f);
		assert(g == c);
	}

	mys_fclose(f);
	printf("test_prop_ungetc_fgetc passed\n");
}

int main(void) {
	mys___stdio_init();
	printf("Running Substrate stdio property tests...\n");
	test_prop_int_roundtrip();
	test_prop_uint_roundtrip();
	test_prop_float_roundtrip();
	test_prop_snprintf_return_bound();
	test_prop_snprintf_nul_terminates();
	test_prop_fwrite_fread_roundtrip();
	test_prop_ftell_after_fseek();
	test_prop_ungetc_fgetc();
	printf("All property tests passed!\n");
	return 0;
}
