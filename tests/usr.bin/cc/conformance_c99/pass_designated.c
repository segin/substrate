struct pair {
	int x;
	int y;
};

int main(void) {
	struct pair p = {.y = 7, .x = 5};
	return p.x + p.y == 12 ? 0 : 1;
}
