int bad_fetch(int *p) {
	return __atomic_fetch_add(p, 1, 99);
}
