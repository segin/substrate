int bad_load(int *p) {
	return __atomic_load_n(p, __ATOMIC_RELEASE);
}
