int bad_load(int *p, int mo) {
	return __atomic_load_n(p, mo);
}
