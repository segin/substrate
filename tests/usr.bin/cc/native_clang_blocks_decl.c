/*
 * Block pointer declarators (clang -fblocks): declared, copied, in an
 * array, and the size of a pointer.  The test once assigned a function to
 * a block pointer, which clang rejects -- a block is not a function.
 */
int main(void) {
	int (^fp)(int) = 0;
	int (^fp2)(int) = fp;
	int (^arr[2])(void) = {0, 0};
	return (fp2 == 0 && arr[1] == 0 && sizeof fp == sizeof(void *)) ? 0 : 1;
}
