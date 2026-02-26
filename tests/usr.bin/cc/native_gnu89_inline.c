extern inline int add_inline(int a, int b) {
	return a + b;
}

int main(void) {
	return add_inline(2, 3) == 5 ? 0 : 1;
}
