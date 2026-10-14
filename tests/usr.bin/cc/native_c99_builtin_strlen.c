static const char *pick(int sel) {
	if(sel)
		return "abcd";
	return "xy";
}

int main(void) {
	const char *s = pick(1);
	return __builtin_strlen(s) == 4 ? 0 : 1;
}
