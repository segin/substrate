#ifndef __STDC_NO_THREADS__
#error __STDC_NO_THREADS__ must be defined in c11+
#endif

#ifndef __STDC_NO_COMPLEX__
#error __STDC_NO_COMPLEX__ must be defined when complex support is incomplete
#endif

#ifdef __STDC_NO_VLA__
#error __STDC_NO_VLA__ should not be defined
#endif

int main(void) {
	return (__STDC_NO_THREADS__ == 1 && __STDC_NO_COMPLEX__ == 1) ? 0 : 1;
}
