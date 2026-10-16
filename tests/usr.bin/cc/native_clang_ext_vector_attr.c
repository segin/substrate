/*
 * __has_attribute(ext_vector_type) answers truly: where it says yes the
 * vector types work (clang), and where it says no (cc, which has no
 * vector types and rejects the attribute) the fallback is taken.  The
 * test once initialized a vector from, and cast one to, a scalar, which
 * clang and gcc reject.
 */
#if __has_attribute(ext_vector_type)
typedef int v4i __attribute__((ext_vector_type(4)));

static int sum(void) {
	v4i a = {1, 2, 3, 4};
	return sizeof a == 16 ? a[0] + a[3] : -1;
}
#else
static int sum(void) {
	int a[4] = {1, 2, 3, 4};
	return a[0] + a[3];
}
#endif

int main(void) {
	return sum() == 5 ? 0 : 1;
}
