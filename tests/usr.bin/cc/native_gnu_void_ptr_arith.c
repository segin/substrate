int main(void) {
	char buf[8];
	void *p = buf;
	void *q = p + 3;
	long d;

	q--;
	d = (long)(q - p);
	return d == 2 ? 0 : 1;
}
