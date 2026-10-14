#include <stdatomic.h>

static _Atomic int g_counter = ATOMIC_VAR_INIT(1);

int main(void) {
	atomic_int v = ATOMIC_VAR_INIT(2);
	int old;
	atomic_store_explicit(&v, 5, memory_order_release);
	old = atomic_fetch_add_explicit(&g_counter, atomic_load_explicit(&v, memory_order_acquire), memory_order_seq_cst);
	return (old == 1 && atomic_load(&g_counter) == 6) ? 0 : 1;
}
