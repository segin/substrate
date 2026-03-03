int sum(int *restrict a, int *restrict b) {
	return a[0] + b[0];
}

int main(void) {
	int x = 2;
	int y = 5;
	return sum(&x, &y) == 7 ? 0 : 1;
}
