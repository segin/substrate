int g_val;

int main(void) {
	int x = 11;
	__asm__ volatile("" ::: "memory");
	g_val = x;
	return g_val == 11 ? 0 : 1;
}
