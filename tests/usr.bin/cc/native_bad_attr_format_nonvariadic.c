__attribute__((format(printf, 1, 2))) int bad_fmt(const char *fmt) {
	(void)fmt;
	return 0;
}

int main(void) {
	return bad_fmt("%d");
}
