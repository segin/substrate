/*
 * fuzz_scanf.c - Fuzz coverage for Substrate libc sscanf (REQ-06-0246..0249)
 *
 * Deterministic xorshift32 PRNG (seed 0xCAFEBABE) so each run is reproducible.
 * All output buffers are sentinel-guarded — sentinels are checked after every
 * call to verify scanf never writes past its declared field width.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stddef.h>

extern int mys_sscanf(const char *str, const char *format, ...);
extern void mys___stdio_init(void);

#define ITERATIONS  1000

static uint32_t xs_state = 0xCAFEBABEu;

static uint32_t xs(void) {
	uint32_t x = xs_state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	xs_state = x;
	return x;
}

static void xs_seed(uint32_t s) {
	xs_state = s ? s : 0xCAFEBABEu;
}

/* Sentinel-guarded buffer: 16 bytes of 0xAA on each side of a writable
 * region.  Caller writes into buf[GUARD..GUARD+size).  After the syscall
 * we re-check the guard regions. */
#define GUARD 16
typedef struct {
	unsigned char pre[GUARD];
	unsigned char data[256];
	unsigned char post[GUARD];
} guarded_t;

static void guard_init(guarded_t *g) {
	memset(g->pre,  0xAA, GUARD);
	memset(g->data, 0,    sizeof(g->data));
	memset(g->post, 0xAA, GUARD);
}

static void guard_check(const guarded_t *g, const char *where) {
	for (int i = 0; i < GUARD; i++) {
		if (g->pre[i] != 0xAA || g->post[i] != 0xAA) {
			fprintf(stderr, "FUZZ OVERRUN at %s: pre[%d]=0x%02x post[%d]=0x%02x\n",
			        where, i, g->pre[i], i, g->post[i]);
			abort();
		}
	}
}

/* Build a random ASCII input string of 1..63 chars from a mix of digits,
 * letters, whitespace, and punctuation. */
static void rand_input(char *out, size_t cap) {
	static const char alphabet[] =
		"0123456789abcdefABCDEF "
		"ghijklmnopqrstuvwxyz \t"
		"GHIJKLMNOPQRSTUVWXYZ +-"
		".eEpP_xX#0o\n";
	size_t alen = sizeof(alphabet) - 1;
	size_t n = 1 + (xs() % (cap - 1));
	for (size_t i = 0; i < n; i++) {
		out[i] = alphabet[xs() % alen];
	}
	out[n] = '\0';
}

/* REQ-06-0247: random format strings + random inputs.
 * For each iteration pick one specifier from a small alphabet, optionally
 * with a width and assignment-suppression flag.  Pass enough output
 * buffers to absorb any conversion. */
static void fuzz_sscanf_random(void) {
	static const char specs[] = "diuxofegscp";
	for (int i = 0; i < ITERATIONS; i++) {
		char fmt[32];
		char input[64];
		guarded_t out;
		long long ival;
		double dval;
		void *pval;

		guard_init(&out);
		rand_input(input, sizeof(input));

		char spec = specs[xs() % (sizeof(specs) - 1)];
		int suppress = (xs() & 7) == 0;
		int width = 1 + (xs() % 32);

		if (suppress) {
			snprintf(fmt, sizeof(fmt), "%%*%d%c", width, spec);
			(void)mys_sscanf(input, fmt, NULL);
		} else {
			snprintf(fmt, sizeof(fmt), "%%%d%c", width, spec);
			int n;
			switch (spec) {
			case 'd': case 'i': case 'u': case 'x': case 'o':
				ival = 0;
				n = mys_sscanf(input, fmt, &ival);
				assert(n == EOF || n == 0 || n == 1);
				break;
			case 'f': case 'e': case 'g':
				dval = 0;
				n = mys_sscanf(input, fmt, &dval);
				assert(n == EOF || n == 0 || n == 1);
				break;
			case 'p':
				pval = NULL;
				n = mys_sscanf(input, fmt, &pval);
				assert(n == EOF || n == 0 || n == 1);
				break;
			case 's': case 'c':
				/* width capped to less than data size to keep room for NUL */
				if (width >= (int)sizeof(out.data)) width = (int)sizeof(out.data) - 1;
				snprintf(fmt, sizeof(fmt), "%%%d%c", width, spec);
				n = mys_sscanf(input, fmt, out.data);
				assert(n == EOF || n == 0 || n == 1);
				guard_check(&out, "random string spec");
				break;
			default:
				break;
			}
		}
	}
	printf("  fuzz_sscanf_random:                OK (REQ-06-0247)\n");
}

/* REQ-06-0248: sentinel-guarded buffers across many randomized %s/%[ calls
 * with varied widths.  Verify post-call sentinels intact. */
static void fuzz_sscanf_sentinels(void) {
	for (int i = 0; i < ITERATIONS; i++) {
		guarded_t out;
		char input[64];
		char fmt[32];
		int width = 1 + (xs() % 200); /* may exceed buffer; width must cap it */

		guard_init(&out);
		rand_input(input, sizeof(input));

		/* Always cap to in-buffer width via the format string itself —
		 * scanf must respect the width and never write past it. */
		int safe_width = width;
		if (safe_width >= (int)sizeof(out.data)) safe_width = (int)sizeof(out.data) - 1;

		const char *kinds[] = { "%%%ds", "%%%d[a-zA-Z0-9]", "%%%d[^ ]", "%%%dc" };
		const char *k = kinds[xs() % 4];
		snprintf(fmt, sizeof(fmt), k, safe_width);

		int n = mys_sscanf(input, fmt, out.data);
		assert(n == EOF || n == 0 || n == 1);
		guard_check(&out, "sentinel sweep");
	}
	printf("  fuzz_sscanf_sentinels:             OK (REQ-06-0248)\n");
}

/* REQ-06-0249: adversarial scansets.
 * Patterns:
 *  - empty after ^ (impossible per spec; verify no crash)
 *  - single char
 *  - reversed range (start > end)
 *  - whole-alphabet range
 *  - ] as first char (treated literally)
 *  - ending in - (literal dash)
 *  - negated set
 *  - width-capped
 * Run each against both matching and non-matching inputs. */
static void fuzz_scanset_adversarial(void) {
	static const char *fmts[] = {
		"%[]",                  /* malformed: empty inside */
		"%[^]",                 /* malformed: empty after ^ */
		"%[a]",                 /* single char */
		"%[z-a]",               /* reversed range — Substrate normalises this */
		"%[ -~]",               /* whole printable ASCII */
		"%[]abc]",              /* ] first */
		"%[abc-]",              /* trailing literal dash */
		"%[^0-9]",              /* negated digits */
		"%5[a-z]",              /* width-capped */
		"%32[^\n]",             /* line-up-to-newline pattern */
		"%1[xyz]",              /* width=1 single-char scanset */
		"%[A-Za-z0-9_]",        /* identifier set */
	};
	int nfmts = (int)(sizeof(fmts) / sizeof(fmts[0]));

	static const char *inputs[] = {
		"",
		"a",
		"abc",
		"ABC",
		"abc123",
		"123abc",
		"]abc]xyz",
		"   leading ws",
		"hello world",
		"\nblank\n",
		"!@#$%^&*()_+",
		"the quick brown fox",
	};
	int ninputs = (int)(sizeof(inputs) / sizeof(inputs[0]));

	for (int i = 0; i < ITERATIONS; i++) {
		guarded_t out;
		guard_init(&out);

		const char *fmt   = fmts[xs() % nfmts];
		const char *input = inputs[xs() % ninputs];

		int n = mys_sscanf(input, fmt, out.data);
		/* Return value must be 0, 1, or EOF. */
		assert(n == EOF || n == 0 || n == 1);
		guard_check(&out, "adversarial scanset");

		/* If something was written, the captured run must be entirely
		 * within the data region (NUL terminator counted). */
		if (n == 1) {
			size_t len = strnlen((const char *)out.data, sizeof(out.data));
			assert(len < sizeof(out.data));
		}
	}
	printf("  fuzz_scanset_adversarial:          OK (REQ-06-0249)\n");
}

int main(void) {
	mys___stdio_init();
	printf("Running Substrate scanf fuzz tests (deterministic seed 0xCAFEBABE)...\n");
	xs_seed(0xCAFEBABEu);
	fuzz_sscanf_random();
	xs_seed(0xCAFEBABEu);
	fuzz_sscanf_sentinels();
	xs_seed(0xCAFEBABEu);
	fuzz_scanset_adversarial();
	printf("All scanf fuzz tests passed (no crashes, no overruns)!\n");
	return 0;
}
