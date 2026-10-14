int main(void) {
	int x = 7;
	int y = 3;
	int out = 0;

	__asm__ volatile("mov %2, %0\n\tadd %1, %0" : "=&r"(out) : "r"(x), "r"(y));
	return out == 10 ? 0 : 1;
}
