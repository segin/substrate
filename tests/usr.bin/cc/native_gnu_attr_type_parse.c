typedef union __attribute__((transparent_union, may_alias)) trans_u {
	int i;
	void *p;
} trans_u_t;

typedef int vec4_t __attribute__((vector_size(16)));

static int pick(trans_u_t u) {
	return u.i;
}

int main(void) {
	vec4_t v = 0;
	(void)v;
	return pick((trans_u_t)6) == 6 ? 0 : 1;
}
