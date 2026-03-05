struct pair {
	int a;
	long b;
};

int main(void) {
	int x = 3;
	int y = 1;
	int old = 0;
	int prev = 0;

	if (__builtin_expect(x, 3) != 3)
		return 1;
	if (!__builtin_constant_p(5))
		return 2;
	if (__builtin_ctz(8) != 3)
		return 3;
	if (__builtin_ffs(0) != 0 || __builtin_ffs(8) != 4)
		return 31;
	if (__builtin_ffsl(16L) != 5 || __builtin_ffsll(1LL << 40) != 41)
		return 32;
	if (__builtin_bswap32(0x11223344u) != 0x44332211u)
		return 4;
	if (__builtin_offsetof(struct pair, b) != 8)
		return 5;

	if (!__sync_bool_compare_and_swap(&x, 3, 9))
		return 6;
	if (x != 9)
		return 7;
	old = __sync_fetch_and_add(&y, 2);
	if (old != 1 || y != 3)
		return 8;
	__sync_synchronize();

	__atomic_store_n(&y, 11, __ATOMIC_SEQ_CST);
	if (__atomic_load_n(&y, __ATOMIC_SEQ_CST) != 11)
		return 9;
	prev = __atomic_exchange_n(&y, 4, __ATOMIC_SEQ_CST);
	if (prev != 11 || y != 4)
		return 10;
	old = __atomic_fetch_sub(&y, 1, __ATOMIC_SEQ_CST);
	if (old != 4 || y != 3)
		return 11;

	return 0;
}
