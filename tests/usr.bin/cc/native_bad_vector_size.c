/* a vector's size must be a power-of-two multiple of its element's (gcc rejects it too) */
typedef int v3si __attribute__((vector_size(12)));

int main(void) {
	v3si a = {1, 2, 3};
	return a[0];
}
