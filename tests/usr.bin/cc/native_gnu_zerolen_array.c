struct packet {
	int len;
	char data[0];
};

int main(void) {
	return sizeof(struct packet) == 4 ? 0 : 1;
}
