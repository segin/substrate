int main(void) {
	int a = (int)u'a';
	int b = (int)U'b';
	const char *s = u8"ok";
	return (a == 'a' && b == 'b' && s[0] == 'o' && s[1] == 'k') ? 0 : 1;
}
