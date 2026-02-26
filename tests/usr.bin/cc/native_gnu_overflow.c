int main(void) {
	long long sr;
	unsigned long long ur;
	int so;
	int uo;
	int mo;

	so = __builtin_add_overflow(9223372036854775807LL, 1LL, &sr);
	if (so != 1)
		return 1;

	uo = __builtin_add_overflow(~0ULL, 1ULL, &ur);
	if (uo != 1)
		return 2;
	if (ur != 0ULL)
		return 3;

	mo = __builtin_mul_overflow(~0ULL, 2ULL, &ur);
	if (mo != 1)
		return 4;

	return 0;
}
