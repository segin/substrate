int main(void) {
	int zero = 0;
	int three = 3;
	int fallback = 9;
	int a = zero ?: fallback;
	int b = three ?: fallback;
	return (a == 9 && b == 3) ? 0 : 1;
}
