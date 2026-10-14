__attribute__((nonnull)) int takes_ptr(int *p) {
	return *p;
}

int main(void) {
	return takes_ptr(0);
}
