__attribute__((always_inline, noinline)) int bad_inline(int x) {
	return x;
}

int main(void) {
	return bad_inline(1);
}
