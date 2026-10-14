__attribute__((nonnull)) int bad_nonnull(void) {
	return 0;
}

int main(void) {
	return bad_nonnull();
}
