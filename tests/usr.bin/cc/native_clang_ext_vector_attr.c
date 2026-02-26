#if !__has_attribute(ext_vector_type)
#error ext_vector_type attribute probe failed
#endif

typedef int v4i __attribute__((ext_vector_type(4)));
typedef int v4j __attribute__((vector_size(16)));

int main(void) {
	v4i a = 0;
	v4j b = 0;
	return ((int)a + (int)b) == 0 ? 0 : 1;
}
