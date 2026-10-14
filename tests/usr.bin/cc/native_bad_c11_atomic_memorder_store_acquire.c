void bad_store(int *p) {
	__atomic_store_n(p, 1, __ATOMIC_ACQUIRE);
}
