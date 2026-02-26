_Static_assert(sizeof(int) >= 4, "int-too-small");

int main(void) {
	_Static_assert(sizeof(long long) == 8, "ll-size");
	return 0;
}
