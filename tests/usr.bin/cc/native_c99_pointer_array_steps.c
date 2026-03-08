int main(void) {
	int x[2];
	int *p;
	char arr[2][4], (*rows)[4], *q;
	int v[4];

	x[1] = 7;
	p = &x[0];
	p = p + 1;
	if (*p != 7)
		return 1;
	if (&x[1] - &x[0] != 1)
		return 2;

	rows = arr;
	q = &arr[1][3];
	arr[1][3] = 2;
	v[0] = 2;
	if (arr[1][3] != 2)
		return 3;
	if (rows[1][3] != 2)
		return 4;
	if (*q != 2)
		return 5;
	if (*v != 2)
		return 6;

	return 0;
}
