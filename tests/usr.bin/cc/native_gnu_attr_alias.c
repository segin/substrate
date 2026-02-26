int target_fn(void) {
	return 9;
}

extern int alias_fn(void) __attribute__((alias("target_fn")));

int target_obj = 4;
extern int alias_obj __attribute__((alias("target_obj")));

int main(void) {
	return (alias_fn() == 9 && alias_obj == 4) ? 0 : 1;
}
