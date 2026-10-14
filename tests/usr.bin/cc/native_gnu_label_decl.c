int main(void) {
	__label__ first, second;
	void *p = &&first;
	goto *p;
second:
	return 0;
first:
	p = &&second;
	goto *p;
}
