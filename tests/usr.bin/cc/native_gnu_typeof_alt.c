int main(void) {
	int x = 3;
	__typeof__(x) y = 4;
	__typeof_unqual__(y) z = 5;
	return (x + y + z) - 12;
}
