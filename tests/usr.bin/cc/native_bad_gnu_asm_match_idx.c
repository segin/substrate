int main(void) {
	int x = 1;
	__asm__ volatile("add %1, %0" : "=r"(x) : "9"(x));
	return x;
}
