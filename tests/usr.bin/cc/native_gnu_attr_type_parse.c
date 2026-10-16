typedef union __attribute__((transparent_union, may_alias)) trans_u {
	int i;
	void *p;
} trans_u_t;

/* cc has no vector types, and says so to __has_attribute */
#if __has_attribute(vector_size)
typedef int vec4_t __attribute__((vector_size(16)));
#endif

static int pick(trans_u_t u) {
	return u.i;
}

int main(void) {
#if __has_attribute(vector_size)
	vec4_t v = {0};
	(void)v;
#endif
	return pick((trans_u_t)6) == 6 ? 0 : 1;
}
