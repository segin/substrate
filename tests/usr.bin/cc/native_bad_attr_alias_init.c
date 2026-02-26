int real_obj = 7;
extern int bad_alias __attribute__((alias("real_obj"))) = 2;

int main(void) {
	return bad_alias;
}
