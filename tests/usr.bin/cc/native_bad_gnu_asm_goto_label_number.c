/*
 * asm goto labels are numbered after every operand, a "+" output counting
 * twice, so with one "+r" output %l0 and %l1 name operands, not labels.
 * gcc rejects this ("'%l' operand isn't a label"); cc took %l0 as the
 * first label.
 */
int main(void) {
	int out = 1;
	__asm__ goto("test %0, %0; jz %l0" : "+r"(out) : : "cc" : done);
	return out;
done:
	return 0;
}
