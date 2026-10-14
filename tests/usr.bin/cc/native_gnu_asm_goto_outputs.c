int main(void) {
	int out = 0;
	int flag = 1;

	__asm__ goto("test %1, %1\n\tjz %l[zero]\n\tmov $7, %0"
	             : "=r"(out)
	             : "r"(flag)
	             : "cc"
	             : zero);
	return out == 7 ? 0 : 1;
zero:
	return 0;
}
