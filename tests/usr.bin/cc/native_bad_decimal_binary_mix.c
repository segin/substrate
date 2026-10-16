/* a decimal and a binary floating operand cannot meet (C23 6.3.1.8; gcc rejects it too) */
int main(void) {
	_Decimal64 d = 1.5DD;
	double x = 2.0;
	return (int)(d + x);
}
