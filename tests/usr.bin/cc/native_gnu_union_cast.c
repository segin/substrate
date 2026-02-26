union U {
	int i;
	float f;
};

int main(void) {
	union U u = (union U)5;
	return u.i == 5 ? 0 : 1;
}
