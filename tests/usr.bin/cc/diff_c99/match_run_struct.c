struct pair {
	int a;
	int b;
};

static int sum(struct pair p) {
	return p.a + p.b;
}

int main(void) {
	struct pair p = {4, 9};
	return sum(p) == 13 ? 0 : 1;
}
