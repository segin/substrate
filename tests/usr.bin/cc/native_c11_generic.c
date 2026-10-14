static int pick_int(void) {
	return 7;
}

static int pick_double(void) {
	return 11;
}

#define PICK(x) _Generic((x), int: pick_int(), double: pick_double(), default: 3)

int main(void) {
	int a = PICK(1);
	int b = PICK(1.0);
	int c = PICK("x");
	return (a == 7 && b == 11 && c == 3) ? 0 : 1;
}
