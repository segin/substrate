static int add_imm(int x) {
	__asm__ volatile("add %1, %0" : "+r"(x) : "i"(5) : "cc");
	return x;
}

static int add_named_tied(int a, int b) {
	int lhs = a;
	__asm__ volatile("add %[rhs], %[lhs]" : [lhs] "=&r"(lhs) : "0"(lhs), [rhs] "r"(b) : "cc", "memory");
	return lhs;
}

static int fixed_reg_inc(int x) {
	__asm__ volatile("add $1, %0" : "+a"(x) : : "cc");
	return x;
}

static int mem_roundtrip(int v) {
	int out = 0;
	__asm__ volatile("mov %1, %0" : "=m"(out) : "r"(v) : "memory");
	return out;
}

int main(void) {
	return (add_imm(7) == 12 && add_named_tied(5, 9) == 14 && fixed_reg_inc(3) == 4 && mem_roundtrip(11) == 11) ? 0
	                                                                                                               : 1;
}
