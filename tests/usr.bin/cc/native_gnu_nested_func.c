int main(void) {
	int inner(int y) {
		return y + 1;
	}
	return inner(4) == 5 ? 0 : 1;
}
