int f(void) {
	register int x = 1;
	int *p = &x;
	return *p;
}
