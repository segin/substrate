__attribute__((hot, cold)) int bad_hot_cold(void) {
	return 1;
}

int main(void) {
	return bad_hot_cold();
}
