static inline int smp_send_stop(void) {
	return 7;
}

extern int smp_send_stop(void);

int main(void) {
	return smp_send_stop() == 7 ? 0 : 1;
}
