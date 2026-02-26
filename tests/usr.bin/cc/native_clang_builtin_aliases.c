static int use_aliases(int *p) {
	int *q = (int *)__builtin_assume_aligned(p, 4);
	int v = __builtin_unpredictable(q[0]);
	__builtin_assume(v >= 0 || v < 0);
	return v;
}

int main(void) {
	int x = 7;
	return use_aliases(&x) == 7 ? 0 : 1;
}
