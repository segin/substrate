/* the operands of a vector operation must have one vector type (gcc rejects it too) */
typedef int v4si __attribute__((vector_size(16)));
typedef short v8hi __attribute__((vector_size(16)));

int main(void) {
	v4si a = {1, 2, 3, 4};
	v8hi b = {1};
	v4si c = a + b;
	return c[0];
}
