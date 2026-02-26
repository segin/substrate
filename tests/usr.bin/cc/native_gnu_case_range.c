static int classify(int v) {
	switch (v) {
	case 1 ... 3:
		return 10;
	case 7:
		return 20;
	default:
		return 30;
	}
}

int main(void) {
	if (classify(2) != 10)
		return 1;
	if (classify(7) != 20)
		return 2;
	if (classify(9) != 30)
		return 3;
	return 0;
}
