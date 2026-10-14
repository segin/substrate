static int add1(int x) {
	return x + 1;
}

int main(void) {
	int (^fp)(int) = add1;
	int (^fp2)(int) = fp;
	return fp2(41) == 42 ? 0 : 1;
}
