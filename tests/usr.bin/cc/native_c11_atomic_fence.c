int main(void) {
	int v = 0;

	__atomic_store_n(&v, 1, __ATOMIC_RELEASE);
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	__atomic_signal_fence(__ATOMIC_ACQUIRE);
	return __atomic_load_n(&v, __ATOMIC_ACQUIRE) == 1 ? 0 : 1;
}
