int main(void) {
	int in = 1;
	int out = 0;
	__asm__ volatile("addl %1, %0" : "=&a"(out) : "a"(in));
	return out;
}
