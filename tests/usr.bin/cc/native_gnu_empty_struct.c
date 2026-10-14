struct empty {};

int main(void) {
	struct empty e;
	(void)e;
	return sizeof(struct empty) == 0 ? 0 : 1;
}
