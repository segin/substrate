#include <limits.h>

/*
 * The operands come from volatile objects so that the checks are computed
 * at run time (setb, imul): constants are folded at compile time.
 */
static volatile long long s_max = 9223372036854775807LL;
static volatile long long s_one = 1LL;
static volatile long long s_neg7 = -7LL;
static volatile long long s_zero = 0LL;
static volatile unsigned long long u_max = ~0ULL;
static volatile unsigned long long u_one = 1ULL;
static volatile unsigned long long u_two = 2ULL;
static volatile unsigned long long u_big = 123456789ULL;
static volatile unsigned long long u_zero = 0ULL;
static volatile int i_base = 2147483640;
static volatile int i_seven = 7;
static volatile int i_eight = 8;

int main(void) {
	int ir;
	long long sr;
	unsigned long long ur;
	int io;
	int so;
	int uo;
	int mo;
	int mz;
	int smz;

	so = __builtin_add_overflow(s_max, s_one, &sr);
	if (so != 1)
		return 1;

	uo = __builtin_add_overflow(u_max, u_one, &ur);
	if (uo != 1)
		return 2;
	if (ur != 0ULL)
		return 3;

	mo = __builtin_mul_overflow(u_max, u_two, &ur);
	if (mo != 1)
		return 4;

	mz = __builtin_mul_overflow(u_big, u_zero, &ur);
	if (mz != 0)
		return 5;
	if (ur != 0ULL)
		return 6;

	smz = __builtin_mul_overflow(s_neg7, s_zero, &sr);
	if (smz != 0)
		return 7;
	if (sr != 0LL)
		return 8;

	io = __builtin_add_overflow(i_base, i_eight, &ir);
	if (io != 1)
		return 9;
	if (ir != INT_MIN)
		return 10;

	io = __builtin_add_overflow(i_base, i_seven, &ir);
	if (io != 0)
		return 11;
	if (ir != INT_MAX)
		return 12;

	return 0;
}
