int main(void) {
	int same = __builtin_types_compatible_p(int, int);
	int diff = __builtin_types_compatible_p(int, long long);
	int pick_true = __builtin_choose_expr(1, 7, does_not_exist);
	int pick_false = __builtin_choose_expr(0, does_not_exist2, 11);
	if (same != 1)
		return 1;
	if (diff != 0)
		return 2;
	if (pick_true != 7)
		return 3;
	if (pick_false != 11)
		return 4;
	return 0;
}
