struct pair {
	int a;
	int b;
};

static struct pair mk(int a, int b) {
	struct pair p;
	p.a = a;
	p.b = b;
	return p;
}

int main(void) {
	struct pair p = mk(4, 9);
	return (p.a + p.b) == 13 ? 0 : 1;
}
