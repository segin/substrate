static long bump(long x) {
	long out = x;
	__asm__ volatile("add $1, %0" : "+r"(out) : : "cc");
	return out;
}

static int g_seen = 0;

static void touch_mem(void) {
	int tmp = 0;
	__asm__ volatile("movl $7, %0" : "=m"(tmp) : : "memory");
	g_seen = tmp;
}

int main(void) {
	touch_mem();
	if (bump(4) != 5)
		return 1;
	if (g_seen != 7)
		return 2;
	return 0;
}
