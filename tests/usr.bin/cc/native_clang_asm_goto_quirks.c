static int branchy(int x) {
	int out = x;
	__asm__ goto volatile("test %[v], %[v]; jz %l[zero]" : [v] "+r"(out) : : "cc" : zero);
	return out + 5;
zero:
	return 9;
}

int main(void) {
	if (branchy(0) != 9)
		return 1;
	if (branchy(2) != 7)
		return 2;
	return 0;
}
