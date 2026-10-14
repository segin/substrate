static int f(int x) {
	if (x == 1)
		__builtin_unreachable();
	if (x == 2)
		__builtin_trap();
	return 0;
}

int main(void) {
	return f(0);
}
