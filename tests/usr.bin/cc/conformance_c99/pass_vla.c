int sum(int n) {
	int a[n];
	int i;
	int s = 0;

	for (i = 0; i < n; ++i) {
		a[i] = i + 1;
		s += a[i];
	}
	return s;
}

int main(void) {
	return sum(4) == 10 ? 0 : 1;
}
