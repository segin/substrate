static int branch_if_zero(int x) {
	int out = x;
	/* labels follow the operands; the "+r" output is also input 1, so is_zero is %l2 */
	__asm__ goto("test %0, %0; jz %l2" : "+r"(out) : : "cc" : is_zero);
	return out + 7;
is_zero:
	return 0;
}

static int branch_named_with_output(int x) {
	int out = x;
	__asm__ goto("test %[val], %[val]; jz %l[zero]" : [val] "+r"(out) : : "cc" : zero);
	return out + 3;
zero:
	return 11;
}

int main(void) {
	if (branch_if_zero(5) != 12)
		return 1;
	if (branch_if_zero(0) != 0)
		return 2;
	if (branch_named_with_output(4) != 7)
		return 3;
	if (branch_named_with_output(0) != 11)
		return 4;
	return 0;
}
