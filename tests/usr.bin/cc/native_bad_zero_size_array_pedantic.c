struct tag {
	unsigned size;
	char string[0];
};

int tag_size(void) { return (int)sizeof(struct tag); }
