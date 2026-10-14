int main(void) {
	char src[8] = { 'a', 'b', 'c', 'd', 'e', 'f', 'g', 0 };
	char dst[8] = { 0 };
	unsigned long long s0;
	unsigned long long s1;
	int n = 8;

	s0 = __builtin_object_size(dst, 0);
	s1 = __builtin_object_size(dst + 1, 0);
	if (s0 != 8ULL)
		return 1;
	if (s1 == 0ULL)
		return 2;

	__builtin___memcpy_chk(dst, src, n, __builtin_object_size(dst, 0));
	if (dst[6] != 'g')
		return 3;
	__builtin___memmove_chk(dst + 1, dst, 7, __builtin_object_size(dst + 1, 0));
	if (dst[1] != 'a')
		return 4;
	__builtin___memset_chk(dst, 0, 8, __builtin_object_size(dst, 0));
	if (dst[0] != 0)
		return 5;
	return 0;
}
