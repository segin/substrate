typedef unsigned long row_t[4];

static unsigned long sum_row(unsigned long *p) {
	return p[0] + p[1] + p[2] + p[3];
}

static int test_direct(unsigned long (*rows)[4]) {
	if(sizeof(*rows) != sizeof(unsigned long) * 4)
		return 1;
	if(sum_row(rows[1]) != 26)
		return 2;
	return 0;
}

static int test_typedef(row_t *rows) {
	if(sizeof(*rows) != sizeof(unsigned long) * 4)
		return 4;
	if(sum_row(rows[1]) != 26)
		return 8;
	return 0;
}

int main(void) {
	unsigned long buf[8];

	buf[0] = 1;
	buf[1] = 2;
	buf[2] = 3;
	buf[3] = 4;
	buf[4] = 5;
	buf[5] = 6;
	buf[6] = 7;
	buf[7] = 8;

	return test_direct((unsigned long (*)[4])buf) | test_typedef((row_t *)buf);
}
