/* % needs integer vector elements (gcc rejects it too) */
typedef float v4sf __attribute__((vector_size(16)));

int main(void) {
	v4sf a = {1, 2, 3, 4}, b = {1, 1, 1, 1};
	v4sf c = a % b;
	return (int)c[0];
}
