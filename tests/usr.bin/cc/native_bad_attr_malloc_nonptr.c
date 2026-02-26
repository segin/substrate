__attribute__((malloc)) int bad_malloc(void) {
	return 1;
}

int main(void) {
	return bad_malloc();
}
