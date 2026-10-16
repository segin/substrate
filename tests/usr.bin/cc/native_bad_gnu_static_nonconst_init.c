/*
 * A static object's initializer must be a constant expression (C11
 * 6.7.9p4); gcc rejects a call there ("initializer element is not
 * constant").  cc ran it once, under a guard, as C++ would.
 */
int calls;

int seed(void) {
	calls = calls + 1;
	return 41;
}

int next_value(void) {
	static int x = seed();
	x = x + 1;
	return x;
}

int main(void) {
	int a = next_value();
	int b = next_value();
	return (a == 42 && b == 43 && calls == 1) ? 0 : 1;
}
