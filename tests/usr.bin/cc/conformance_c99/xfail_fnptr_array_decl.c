int f(int x) {
	return x;
}

int main(void) {
	int (*a[2])(int) = {f, f};
	return a[1](3) - 3;
}
