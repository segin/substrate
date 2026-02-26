_Thread_local static int tls_counter;
_Alignas(16) static int aligned_value = 1;

_Noreturn static void never_returns(void) {
	for(;;)
		;
}

int main(void) {
	int ok = 0;
	if(_Alignof(long long) >= 8)
		ok++;
	tls_counter = aligned_value;
	if(tls_counter == 1)
		ok++;
	if(ok == 12345)
		never_returns();
	return ok == 2 ? 0 : 1;
}
