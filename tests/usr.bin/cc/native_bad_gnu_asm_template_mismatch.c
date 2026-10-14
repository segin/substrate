int main(void) {
	int x = 1;
	int y = 2;
	__asm__ volatile("add %2, %0" : "+r"(x) : "r"(y));
	return x;
}
