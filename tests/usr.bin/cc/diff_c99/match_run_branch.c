int main(void) {
	int x = 0;
	int i;
	for (i = 0; i < 5; ++i)
		x += i;
	return x == 10 ? 0 : 1;
}
