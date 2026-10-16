/*
 * GNU89 inline (-fgnu89-inline), the reverse of C99: plain `inline`
 * (add_inline) is an external definition, and `extern inline` (add_ext,
 * not called) is for inlining only, with no out-of-line copy.
 */
inline int add_inline(int a, int b) {
	return a + b;
}

extern inline int add_ext(int a, int b) {
	return a + b + 1;
}

int main(void) {
	return add_inline(2, 3) == 5 ? 0 : 1;
}
