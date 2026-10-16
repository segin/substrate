/*
 * GNU attribute placement: on a label, and as a null statement (which gcc
 * accepts with a warning and clang rejects -- gcc behaviour, so a gnu test).
 */
int main(void) {
	int out = 0;
label_a: __attribute__((unused));
	if (out == 0)
		__attribute__((unused));
	out = 1;
	if (out == 1)
		goto label_c;
	return 2;
label_c:
	return 0;
}
