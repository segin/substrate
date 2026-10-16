/* % needs integer operands, a decimal floating one is not (gcc rejects it too) */
int main(void) {
	_Decimal64 d = 7.5DD;
	return (int)(d % 2);
}
