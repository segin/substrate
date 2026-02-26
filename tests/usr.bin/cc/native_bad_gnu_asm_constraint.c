int main(void) {
	int x = 0;
	__asm__ volatile("nop" : "=z"(x));
	return x;
}
