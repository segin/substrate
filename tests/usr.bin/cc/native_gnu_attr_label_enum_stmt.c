enum marker {
	MARK_A __attribute__((unused)) = 3,
	MARK_B __attribute__((deprecated)) = 4
};

int main(void) {
	int x = 0;
entry: __attribute__((unused));
	if (x == 0) {
		x = MARK_A;
		goto done;
	}
	goto entry;
done:
	return x == 3 ? 0 : 1;
}
