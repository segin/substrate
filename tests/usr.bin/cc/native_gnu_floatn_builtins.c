#ifndef __FLT_EVAL_METHOD_TS_18661_3__
#error missing __FLT_EVAL_METHOD_TS_18661_3__
#endif

int main(void) {
	float f = __builtin_nansf("");
	double d = __builtin_nans("");
	long double ld = __builtin_nansl("");
	float inf_f = __builtin_inff();
	double inf_d = __builtin_inf();
	long double inf_ld = __builtin_infl();

	if (f == f)
		return 1;
	if (d == d)
		return 2;
	if (ld == ld)
		return 3;
	if (!(inf_f > 1.0f))
		return 4;
	if (!(inf_d > 1.0))
		return 5;
	if (!(inf_ld > 1.0L))
		return 6;
	return 0;
}
