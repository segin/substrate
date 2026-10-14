int f(x)
int x;
{
	return x + 1;
}

int main(void) {
	return f(2) == 3 ? 0 : 1;
}
