__attribute__((hot, noinline, weak)) int hot_fn(void) {
	return 7;
}

__attribute__((cold, weak)) int cold_fn(void) {
	return 3;
}

__attribute__((always_inline)) static inline int inl(int x) {
	return x + 1;
}

int main(void) {
	return (hot_fn() + cold_fn() + inl(1)) == 12 ? 0 : 1;
}
