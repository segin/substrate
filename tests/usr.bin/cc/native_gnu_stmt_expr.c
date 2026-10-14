int main(void) {
	int x = 3;
	int y = ({
		int t = x + 4;
		t * 2;
	});
	return y == 14 ? 0 : 1;
}
