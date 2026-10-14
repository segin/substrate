int main(void) {
	int x = 3;
	int inner(int y) {
		return x + y;
	}
	return inner(4);
}
