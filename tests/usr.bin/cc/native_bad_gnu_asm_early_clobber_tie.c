int main(void) {
	int x = 1;
	__asm__ volatile("" : "=&r"(x) : "0"(x));
	return x;
}
